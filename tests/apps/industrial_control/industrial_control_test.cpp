/**
 * \file            industrial_control_test.cpp
 * \brief           Real owner completion, periodic readiness and joined stop
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-02
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "industrial_application.h"
#include "industrial_spi.h"
#include "nexus/arch/arch.h"
#include "nexus/components/settings.h"
#include "nexus/io/native/model.h"
#include "nexus/os/native.h"
#include "nexus_bindings.h"
#include "nexus_factory_strong.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <thread>

/* Narrow host-only fault seams delegate every ordinary call to the real API. */
static std::atomic<bool> inject_cold_failure{false};
static std::atomic<bool> inject_join_failure{false};
static std::atomic<size_t> settings_closes{0U};
static std::atomic<bool> inspect_owner_progress{false};
static std::atomic<bool> force_owner_busy{false};
static std::atomic<bool> fail_owner_transfer{false};
static std::atomic<size_t> owner_waits{0U};
static std::atomic<size_t> waits_with_polling_result{0U};
static std::atomic<size_t> maximum_service_steps{0U};
static thread_local nx_bus_owner_t* observed_owner;
static thread_local size_t service_steps;
extern "C" nx_platform_start_result_t __real_nx_platform_start(void);
extern "C" nx_platform_start_result_t __wrap_nx_platform_start(void) {
    if (inject_cold_failure.exchange(false)) {
        return {NX_ERROR_IO, NX_ERROR_IO, 1U};
    }
    return __real_nx_platform_start();
}
extern "C" void __real_nx_settings_close(nx_settings_t* settings);
extern "C" void __wrap_nx_settings_close(nx_settings_t* settings) {
    settings_closes.fetch_add(1U);
    __real_nx_settings_close(settings);
}
extern "C" nx_result_t __real_nx_native_task_join(nx_native_task_t* task);
extern "C" nx_result_t __wrap_nx_native_task_join(nx_native_task_t* task) {
    const auto result = __real_nx_native_task_join(task);
    if (result == NX_SUCCESS && force_owner_busy.exchange(false)) {
        nx_native_spi_model_fault(nx_factory_spi_ref(NX_SPI_REF_SPI0), SIZE_MAX,
                                  false);
    }
    return result == NX_SUCCESS && inject_join_failure.exchange(false)
               ? NX_ERROR_IO
               : result;
}
extern "C" nx_result_t __real_nx_bus_owner_service(nx_bus_owner_t* owner);
extern "C" nx_result_t __wrap_nx_bus_owner_service(nx_bus_owner_t* owner) {
    if (inspect_owner_progress.load()) {
        observed_owner = owner;
        ++service_steps;
        if (force_owner_busy.load() || fail_owner_transfer.load()) {
            nx_native_spi_model_fault(nx_factory_spi_ref(NX_SPI_REF_SPI0),
                                      fail_owner_transfer.load() ? 0U
                                                                 : SIZE_MAX,
                                      force_owner_busy.load());
        }
    }
    return __real_nx_bus_owner_service(owner);
}
extern "C" nx_wait_port_t
__real_nx_native_notify_port(nx_native_notify_t* notification);

/* Observe the real worker's wait boundary, without progressing its owner. */
static nx_result_t observed_wait(void* context, uint32_t sequence,
                                 nx_time_us_t deadline) {
    if (inspect_owner_progress.load() && observed_owner != nullptr &&
        observed_owner->wake.context == context) {
        ++owner_waits;
        const auto* executor = static_cast<const nx_spi_owner_executor_t*>(
            observed_owner->executor.context);
        if (executor->pending) {
            ++waits_with_polling_result;
        }
        if (service_steps > maximum_service_steps.load()) {
            maximum_service_steps.store(service_steps);
        }
        service_steps = 0U;
    }
    const auto real =
        __real_nx_native_notify_port(static_cast<nx_native_notify_t*>(context));
    return real.wait(real.context, sequence, deadline);
}
extern "C" nx_wait_port_t
__wrap_nx_native_notify_port(nx_native_notify_t* notification) {
    auto port = __real_nx_native_notify_port(notification);
    if (inspect_owner_progress.load()) {
        port.wait = observed_wait;
    }
    return port;
}

namespace {
uintptr_t enter(void*) {
    return nx_arch_irq_save().value;
}
void leave(void*, uintptr_t value) {
    nx_arch_irq_restore(nx_arch_irq_state_t{static_cast<uint32_t>(value)});
}
nx_time_us_t now(void*) {
    return nx_time_now_us();
}

class IndustrialCompletion : public testing::Test {
  protected:
    nx_bus_owner_t owner{};
    nx_owner_slot_t slot{};
    nx_spi_owner_executor_t executor{};
    nx_native_notify_t notification{};
    example_industrial_spi_t bridge{};
    std::array<uint8_t, 128> registers{};
    std::atomic<bool> progress{false};
    std::atomic<bool> exiting{false};
    std::atomic<bool> timer_observed{false};
    std::atomic<bool> cancel_observed{false};
    std::thread worker;
    bool notification_open = false;
    bool platform_open = false;

    void SetUp() override {
        const auto startup = nx_platform_start();
        ASSERT_EQ(startup.primary, NX_SUCCESS);
        ASSERT_EQ(startup.cleanup, NX_SUCCESS);
        ASSERT_EQ(startup.remaining_effects, 0U);
        platform_open = true;
        ASSERT_EQ(nx_native_clock_configure(false, 0U), NX_SUCCESS);
        registers[0] = 0x58U;
        registers[1] = 0xA6U;
        ASSERT_EQ(nx_native_spi_model_configure(
                      nx_factory_spi_device_ref(NX_SPI_DEVICE_REF_SENSOR0),
                      registers.data(), registers.size()),
                  NX_SUCCESS);
        ASSERT_EQ(nx_native_notify_init(&notification), NX_SUCCESS);
        notification_open = true;
        const auto port = nx_spi_owner_executor_port(
            &executor, nx_factory_spi_ref(NX_SPI_REF_SPI0));
        ASSERT_EQ(
            nx_bus_owner_init(&owner, &slot, 1U,
                              nx_owner_guard_port_t{nullptr, enter, leave},
                              port, nx_clock_t{now, nullptr}, nullptr),
            NX_SUCCESS);
        worker = std::thread([this] {
            while (!exiting.load(std::memory_order_acquire)) {
                if (progress.load(std::memory_order_acquire)) {
                    (void)nx_bus_owner_service(&owner);
                }
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        });
    }

    void TearDown() override {
        if (worker.joinable()) {
            nx_bus_owner_stop(&owner);
            progress.store(true, std::memory_order_release);
            const auto deadline = nx_deadline_after(now(nullptr), 1000000U);
            while (!nx_bus_owner_idle(&owner) && now(nullptr) < deadline) {
                std::this_thread::yield();
            }
            if (!nx_bus_owner_idle(&owner)) {
                ADD_FAILURE() << "Owner retains fixture storage; aborting "
                                 "instead of reclaiming live loans";
                std::abort();
            }
            exiting.store(true, std::memory_order_release);
            worker.join();
        }
        if (notification_open) {
            EXPECT_EQ(nx_native_notify_destroy(&notification), NX_SUCCESS);
        }
        if (platform_open) {
            EXPECT_EQ(nx_platform_stop(), NX_SUCCESS);
        }
    }

    static uint32_t arm(void* context) {
        auto& self = *static_cast<IndustrialCompletion*>(context);
        if (self.bridge.timer_events != 0U) {
            self.timer_observed.store(true, std::memory_order_release);
        }
        if (self.bridge.cancel_requested) {
            self.cancel_observed.store(true, std::memory_order_release);
        }
        const auto underlying = nx_native_notify_port(&self.notification);
        return underlying.arm(underlying.context);
    }
    static nx_result_t wait(void* context, uint32_t sequence,
                            nx_time_us_t deadline) {
        auto& self = *static_cast<IndustrialCompletion*>(context);
        const auto underlying = nx_native_notify_port(&self.notification);
        return underlying.wait(underlying.context, sequence, deadline);
    }
    static nx_result_t signal(void* context) {
        auto& self = *static_cast<IndustrialCompletion*>(context);
        const auto underlying = nx_native_notify_port(&self.notification);
        return underlying.wake(underlying.context);
    }
    nx_result_t open(uint32_t period = 2000U) {
        return example_industrial_spi_open(
            &bridge, &owner,
            nx_factory_spi_device_ref(NX_SPI_DEVICE_REF_SENSOR0),
            nx_wait_port_t{this, arm, wait, signal}, nx_clock_t{now, nullptr},
            period);
    }

    bool observed(const std::atomic<bool>& barrier) {
        const auto deadline = nx_deadline_after(now(nullptr), 500000U);
        while (!barrier.load(std::memory_order_acquire) &&
               now(nullptr) < deadline) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        return barrier.load(std::memory_order_acquire);
    }

    bool queued() {
        const auto deadline = nx_deadline_after(now(nullptr), 500000U);
        while (nx_request_state(&bridge.request) == NX_REQUEST_READY &&
               now(nullptr) < deadline) {
            std::this_thread::yield();
        }
        return nx_request_state(&bridge.request) == NX_REQUEST_QUEUED;
    }
};

TEST_F(IndustrialCompletion, ExactEndpointUsesRealSpiOwnerAndCopiesBack) {
    ASSERT_EQ(open(), NX_SUCCESS);
    progress.store(true, std::memory_order_release);
    const uint8_t tx[3] = {0x80U, 0U, 0U};
    uint8_t rx[3] = {};
    const auto transport = example_industrial_spi_transport(&bridge);
    ASSERT_NE(transport.exchange, nullptr);
    ASSERT_EQ(transport.exchange(transport.context, tx, rx, sizeof(tx),
                                 nx_deadline_after(now(nullptr), 500000U)),
              NX_SUCCESS);
    EXPECT_EQ(rx[1], 0x58U);
    EXPECT_EQ(rx[2], 0xA6U);
    EXPECT_EQ(bridge.completed, 1U);
    EXPECT_FALSE(bridge.pending);
}

TEST_F(IndustrialCompletion, TimerAndCompletionShareOneActualWaitPort) {
    ASSERT_EQ(open(), NX_SUCCESS);
    progress.store(false, std::memory_order_release);
    nx_result_t result = NX_ERROR_STATE;
    const auto transport = example_industrial_spi_transport(&bridge);
    std::thread consumer([&] {
        const uint8_t tx[2] = {0x80U, 0U};
        uint8_t rx[2] = {};
        result = transport.exchange(transport.context, tx, rx, sizeof(tx),
                                    nx_deadline_after(now(nullptr), 500000U));
    });
    const bool admitted = queued();
    const bool timer_selected = observed(timer_observed);
    progress.store(true, std::memory_order_release);
    consumer.join();
    ASSERT_TRUE(admitted);
    EXPECT_TRUE(timer_selected);
    EXPECT_EQ(result, NX_SUCCESS);
    EXPECT_GE(bridge.timer_events, 1U);
    EXPECT_EQ(bridge.completed, 1U);
    EXPECT_GT(bridge.next_tick, now(nullptr) - 100000U);
}

TEST_F(IndustrialCompletion, TimeoutNeverRetainsTheDriversStackPointers) {
    ASSERT_EQ(open(), NX_SUCCESS);
    progress.store(false, std::memory_order_release);
    uint8_t tx[2] = {0x80U, 0U};
    uint8_t rx[2] = {0xCCU, 0xCCU};
    const auto transport = example_industrial_spi_transport(&bridge);
    EXPECT_EQ(transport.exchange(transport.context, tx, rx, sizeof(tx),
                                 nx_deadline_after(now(nullptr), 10000U)),
              NX_ERROR_TIMEOUT);
    ASSERT_TRUE(bridge.pending);
    EXPECT_EQ(bridge.operation.tx, bridge.tx);
    EXPECT_EQ(bridge.operation.rx, bridge.rx);
    EXPECT_NE(bridge.operation.tx, tx);
    EXPECT_NE(bridge.operation.rx, rx);
    tx[0] = 0xCDU;
    EXPECT_EQ(bridge.tx[0], 0x80U);
    EXPECT_EQ(rx[0], 0xCCU);
    EXPECT_EQ(rx[1], 0xCCU);
    EXPECT_EQ(transport.exchange(transport.context, tx, rx, sizeof(tx),
                                 nx_deadline_after(now(nullptr), 10000U)),
              NX_ERROR_BUSY);
    EXPECT_EQ(example_industrial_spi_join(
                  &bridge, nx_deadline_after(now(nullptr), 2000U)),
              NX_ERROR_TIMEOUT);
    EXPECT_TRUE(bridge.pending);
    progress.store(true, std::memory_order_release);
    EXPECT_EQ(example_industrial_spi_join(
                  &bridge, nx_deadline_after(now(nullptr), 500000U)),
              NX_SUCCESS);
    EXPECT_FALSE(bridge.pending);
    EXPECT_EQ(rx[0], 0xCCU);
    EXPECT_EQ(rx[1], 0xCCU);
}

TEST_F(IndustrialCompletion, StopPublishesCancelsAndJoinsWithoutServicingSpi) {
    ASSERT_EQ(open(), NX_SUCCESS);
    progress.store(false, std::memory_order_release);
    nx_result_t result = NX_ERROR_STATE;
    const auto transport = example_industrial_spi_transport(&bridge);
    std::thread consumer([&] {
        const uint8_t tx[2] = {0x80U, 0U};
        uint8_t rx[2] = {};
        result = transport.exchange(transport.context, tx, rx, sizeof(tx),
                                    nx_deadline_after(now(nullptr), 500000U));
    });
    const bool admitted = queued();
    example_industrial_spi_stop(&bridge);
    const bool stop_selected = observed(cancel_observed);
    progress.store(true, std::memory_order_release);
    consumer.join();
    ASSERT_TRUE(admitted);
    EXPECT_TRUE(stop_selected);
    EXPECT_EQ(result, NX_ERROR_CANCELLED);
    EXPECT_EQ(bridge.stop_events, 1U);
    EXPECT_FALSE(bridge.pending);
    const uint8_t tx[2] = {0x80U, 0U};
    EXPECT_EQ(transport.exchange(transport.context, tx, nullptr, sizeof(tx),
                                 nx_deadline_after(now(nullptr), 10000U)),
              NX_ERROR_STATE);
}

TEST_F(IndustrialCompletion, InvalidCapacityOrExpiredDeadlineHasNoAdmission) {
    ASSERT_EQ(open(), NX_SUCCESS);
    uint8_t tx[EXAMPLE_INDUSTRIAL_SPI_BYTES + 1U] = {};
    const auto transport = example_industrial_spi_transport(&bridge);
    EXPECT_EQ(transport.exchange(transport.context, tx, nullptr, sizeof(tx),
                                 nx_deadline_after(now(nullptr), 10000U)),
              NX_ERROR_INVALID);
    EXPECT_EQ(
        transport.exchange(transport.context, tx, nullptr, 2U, now(nullptr)),
        NX_ERROR_TIMEOUT);
    EXPECT_FALSE(bridge.pending);
    EXPECT_TRUE(nx_bus_owner_idle(&owner));
}

class IndustrialPersistence : public testing::Test {
  protected:
    std::array<uint8_t, 1024> bytes{};
    std::array<uint8_t, NX_STORAGE_HEADER_SIZE> workspace{};
    nx_storage_t storage{};
    size_t mutations = 0U;
    bool fail_program = false;
    std::chrono::milliseconds program_delay{0};
    example_industrial_config_t config{100000U, 2U, 30000U, 500000U, 1000000U};

    static nx_storage_status_t read(void* context, size_t offset, void* output,
                                    size_t count) {
        auto& self = *static_cast<IndustrialPersistence*>(context);
        if (offset > self.bytes.size() || count > self.bytes.size() - offset) {
            return NX_STORAGE_INVALID;
        }
        std::memcpy(output, self.bytes.data() + offset, count);
        return NX_STORAGE_OK;
    }
    static nx_storage_status_t program(void* context, size_t offset,
                                       const void* input, size_t count) {
        auto& self = *static_cast<IndustrialPersistence*>(context);
        ++self.mutations;
        if (self.program_delay.count() != 0) {
            const auto delay = self.program_delay;
            self.program_delay = std::chrono::milliseconds(0);
            std::this_thread::sleep_for(delay);
        }
        if (self.fail_program) {
            return NX_STORAGE_IO;
        }
        if (offset % 4U != 0U || count % 4U != 0U ||
            offset > self.bytes.size() || count > self.bytes.size() - offset) {
            return NX_STORAGE_INVALID;
        }
        const auto* data = static_cast<const uint8_t*>(input);
        for (size_t index = 0U; index < count; ++index) {
            if ((self.bytes[offset + index] & data[index]) != data[index]) {
                return NX_STORAGE_IO;
            }
        }
        for (size_t index = 0U; index < count; ++index) {
            self.bytes[offset + index] &= data[index];
        }
        return NX_STORAGE_OK;
    }
    static nx_storage_status_t erase(void* context, size_t offset,
                                     size_t count) {
        auto& self = *static_cast<IndustrialPersistence*>(context);
        ++self.mutations;
        if (offset % 256U != 0U || count % 256U != 0U ||
            offset > self.bytes.size() || count > self.bytes.size() - offset) {
            return NX_STORAGE_INVALID;
        }
        std::memset(self.bytes.data() + offset, 0xFF, count);
        return NX_STORAGE_OK;
    }
    static nx_storage_status_t sync(void*) {
        return NX_STORAGE_OK;
    }
    nx_storage_status_t reopen() {
        const nx_storage_port_t port{this, bytes.size(), 256U,  4U,
                                     read, program,      erase, sync};
        return nx_storage_open(&storage, &port, 0U, 512U, workspace.data(),
                               workspace.size());
    }
    void SetUp() override {
        bytes.fill(0xFFU);
        inspect_owner_progress.store(false);
        force_owner_busy.store(false);
        fail_owner_transfer.store(false);
        owner_waits.store(0U);
        waits_with_polling_result.store(0U);
        maximum_service_steps.store(0U);
        ASSERT_EQ(reopen(), NX_STORAGE_OK);
    }
    void seed(uint32_t schema, uint32_t period) {
        nx_settings_t settings{};
        uint8_t image[64] = {};
        ASSERT_EQ(nx_settings_open(&settings, &storage, schema, image,
                                   sizeof(image), 1U),
                  NX_STORAGE_OK);
        const uint8_t value[4] = {static_cast<uint8_t>(period),
                                  static_cast<uint8_t>(period >> 8U),
                                  static_cast<uint8_t>(period >> 16U),
                                  static_cast<uint8_t>(period >> 24U)};
        ASSERT_EQ(nx_settings_set(&settings, EXAMPLE_INDUSTRIAL_PERIOD_KEY,
                                  value, sizeof(value)),
                  NX_STORAGE_OK);
        ASSERT_EQ(nx_settings_commit(&settings), NX_STORAGE_OK);
        nx_settings_close(&settings);
    }
};

TEST_F(IndustrialPersistence,
       RealSamplingModbusAndMaintenanceCommitJoinAllUsers) {
    example_industrial_report_t report{};
    ASSERT_EQ(example_industrial_run(&storage, &config, &report), NX_SUCCESS);
    EXPECT_EQ(report.sample.centi_celsius, 2508);
    EXPECT_EQ(report.sample.pressure_pa, 100653U);
    EXPECT_EQ(report.samples, 2U);
    EXPECT_GE(report.spi_completions, 6U);
    EXPECT_GE(report.modbus_replies, 3U);
    EXPECT_EQ(report.max_modbus_replies_per_turn, 1U);
    EXPECT_EQ(report.rejected_writes, 1U);
    EXPECT_GE(report.timer_events, 1U);
    EXPECT_EQ(report.stop_events, 1U);
    EXPECT_GE(report.watchdog_feeds, 2U);
    EXPECT_EQ(report.initial_period_us, config.period_us);
    EXPECT_EQ(report.durable_period_us, config.period_us + 10000U);
    EXPECT_TRUE(report.committed_in_maintenance);
    EXPECT_TRUE(report.joined);
    EXPECT_TRUE(report.platform_stopped);
    EXPECT_TRUE(report.watchdog_retained);
    EXPECT_FALSE(report.storage_retained);
    EXPECT_GT(mutations, 0U);
}

TEST_F(IndustrialPersistence, CompletedPollingIoIsPublishedBeforeWorkerWait) {
    config.sample_limit = 1U;
    inspect_owner_progress.store(true);
    example_industrial_report_t report{};
    const auto result = example_industrial_run(&storage, &config, &report);
    inspect_owner_progress.store(false);
    ASSERT_EQ(result, NX_SUCCESS);
    EXPECT_GE(report.spi_completions, 8U);
    EXPECT_EQ(waits_with_polling_result.load(), 0U);
    EXPECT_LE(maximum_service_steps.load(), 2U);
    EXPECT_TRUE(report.joined);
    EXPECT_TRUE(report.platform_stopped);
}

TEST_F(IndustrialPersistence, BusyPollingIoStillWaitsAndHonorsItsDeadline) {
    config.sample_limit = 1U;
    force_owner_busy.store(true);
    inspect_owner_progress.store(true);
    example_industrial_report_t report{};
    const auto result = example_industrial_run(&storage, &config, &report);
    inspect_owner_progress.store(false);
    force_owner_busy.store(false);
    EXPECT_EQ(result, NX_ERROR_TIMEOUT);
    EXPECT_GT(owner_waits.load(), 0U);
    EXPECT_LE(maximum_service_steps.load(), 2U);
    EXPECT_EQ(report.samples, 0U);
    EXPECT_EQ(report.spi_completions, 1U);
    EXPECT_EQ(mutations, 0U);
    EXPECT_TRUE(report.joined);
    EXPECT_TRUE(report.platform_stopped);
    EXPECT_FALSE(report.storage_retained);
}

TEST_F(IndustrialPersistence, FailedPollingIoIsPublishedBeforeWorkerWait) {
    fail_owner_transfer.store(true);
    inspect_owner_progress.store(true);
    example_industrial_report_t report{};
    const auto result = example_industrial_run(&storage, &config, &report);
    inspect_owner_progress.store(false);
    fail_owner_transfer.store(false);
    EXPECT_EQ(result, NX_ERROR_IO);
    EXPECT_EQ(waits_with_polling_result.load(), 0U);
    EXPECT_EQ(report.spi_completions, 1U);
    EXPECT_EQ(mutations, 0U);
    EXPECT_TRUE(report.joined);
    EXPECT_TRUE(report.platform_stopped);
    EXPECT_FALSE(report.storage_retained);
}

TEST_F(IndustrialPersistence,
       ReopenUsesCommittedConfigurationInsteadOfDefaults) {
    config.sample_limit = 1U;
    example_industrial_report_t first{};
    ASSERT_EQ(example_industrial_run(&storage, &config, &first), NX_SUCCESS);
    ASSERT_EQ(reopen(), NX_STORAGE_OK);
    example_industrial_report_t second{};
    ASSERT_EQ(example_industrial_run(&storage, &config, &second), NX_SUCCESS)
        << "samples=" << second.samples
        << " completions=" << second.spi_completions
        << " replies=" << second.modbus_replies << " joined=" << second.joined
        << " stopped=" << second.platform_stopped
        << " storage=" << second.storage_status
        << " watchdog_expired=" << second.watchdog_expired;
    EXPECT_TRUE(second.restored_settings);
    EXPECT_EQ(second.initial_period_us, first.durable_period_us);
    EXPECT_EQ(second.durable_period_us, first.durable_period_us + 10000U);
    EXPECT_TRUE(second.joined);
    EXPECT_TRUE(second.platform_stopped);
}

TEST_F(IndustrialPersistence, UnknownSchemaNeverBecomesAnEmptyWritableStore) {
    ASSERT_NO_FATAL_FAILURE(
        seed(EXAMPLE_INDUSTRIAL_SCHEMA + 1U, config.period_us));
    const auto before = bytes;
    const auto before_mutations = mutations;
    example_industrial_report_t report{};
    EXPECT_EQ(example_industrial_run(&storage, &config, &report),
              NX_ERROR_UNSUPPORTED);
    EXPECT_EQ(report.storage_status, NX_STORAGE_UNSUPPORTED);
    EXPECT_EQ(bytes, before);
    EXPECT_EQ(mutations, before_mutations);
    EXPECT_FALSE(report.committed_in_maintenance);
    EXPECT_FALSE(report.storage_retained);
}

TEST_F(IndustrialPersistence,
       FailedCommitJoinsIoAndRequiresExplicitStorageReopen) {
    ASSERT_NO_FATAL_FAILURE(seed(EXAMPLE_INDUSTRIAL_SCHEMA, config.period_us));
    fail_program = true;
    config.sample_limit = 1U;
    example_industrial_report_t report{};
    EXPECT_EQ(example_industrial_run(&storage, &config, &report), NX_ERROR_IO);
    EXPECT_EQ(report.storage_status, NX_STORAGE_IO);
    EXPECT_FALSE(storage.opened);
    EXPECT_FALSE(report.committed_in_maintenance);
    EXPECT_TRUE(report.joined);
    EXPECT_TRUE(report.platform_stopped);
    EXPECT_FALSE(report.storage_retained);
    fail_program = false;
    ASSERT_EQ(reopen(), NX_STORAGE_OK);
    nx_settings_t settings{};
    uint8_t image[64] = {};
    ASSERT_EQ(nx_settings_open(&settings, &storage, EXAMPLE_INDUSTRIAL_SCHEMA,
                               image, sizeof(image), 1U),
              NX_STORAGE_OK);
    uint8_t value[4] = {};
    size_t count = sizeof(value);
    ASSERT_EQ(nx_settings_get(&settings, EXAMPLE_INDUSTRIAL_PERIOD_KEY, value,
                              &count),
              NX_STORAGE_OK);
    EXPECT_EQ(count, sizeof(value));
    const auto recovered = static_cast<uint32_t>(value[0]) |
                           (static_cast<uint32_t>(value[1]) << 8U) |
                           (static_cast<uint32_t>(value[2]) << 16U) |
                           (static_cast<uint32_t>(value[3]) << 24U);
    EXPECT_EQ(recovered, config.period_us);
    nx_settings_close(&settings);
}

TEST_F(IndustrialPersistence, InvalidBudgetDoesNotStartOrMutateTheStore) {
    config.period_us = 1U;
    example_industrial_report_t report{};
    EXPECT_EQ(example_industrial_run(&storage, &config, &report),
              NX_ERROR_INVALID);
    EXPECT_EQ(mutations, 0U);
    EXPECT_FALSE(report.joined);
    EXPECT_FALSE(report.platform_stopped);
}

TEST_F(IndustrialPersistence, RestoredPeriodMustFitTheSelectedWatchdogWindow) {
    ASSERT_NO_FATAL_FAILURE(seed(EXAMPLE_INDUSTRIAL_SCHEMA, 500000U));
    const auto before = bytes;
    const auto before_mutations = mutations;
    example_industrial_report_t report{};
    EXPECT_EQ(example_industrial_run(&storage, &config, &report),
              NX_ERROR_INVALID);
    EXPECT_EQ(bytes, before);
    EXPECT_EQ(mutations, before_mutations);
    EXPECT_FALSE(report.joined);
    EXPECT_FALSE(report.platform_stopped);
}

TEST_F(IndustrialPersistence,
       ColdCleanupEffectsAreRetainedWithoutBorrowingStore) {
    ASSERT_EXIT(
        {
            settings_closes.store(0U);
            inject_cold_failure.store(true);
            example_industrial_report_t report{};
            const auto result =
                example_industrial_run(&storage, &config, &report);
            const bool held = result == NX_ERROR_IO &&
                              report.storage_retained &&
                              report.startup_primary == NX_ERROR_IO &&
                              report.startup_cleanup == NX_ERROR_IO &&
                              report.startup_remaining_effects == 1U &&
                              settings_closes.load() == 1U;
            example_industrial_report_t retry{};
            const auto repeated =
                example_industrial_run(&storage, &config, &retry);
            std::_Exit(held && repeated == NX_ERROR_STATE ? 0 : 1);
        },
        testing::ExitedWithCode(0), "");
}

TEST_F(IndustrialPersistence,
       UnconfirmedJoinRetainsIoButReleasesSettingsMetadata) {
    ASSERT_EXIT(
        {
            settings_closes.store(0U);
            inject_join_failure.store(true);
            example_industrial_report_t report{};
            const auto result =
                example_industrial_run(&storage, &config, &report);
            std::_Exit(result == NX_ERROR_IO && report.storage_retained &&
                               !report.platform_stopped &&
                               settings_closes.load() == 1U
                           ? 0
                           : 1);
        },
        testing::ExitedWithCode(0), "");
}

TEST_F(IndustrialPersistence,
       SlowMaintenanceKeepsCommitFactButFailsExpiredHealth) {
    config.sample_limit = 1U;
    program_delay = std::chrono::milliseconds(1100);
    example_industrial_report_t report{};
    EXPECT_EQ(example_industrial_run(&storage, &config, &report), NX_ERROR_IO);
    EXPECT_TRUE(report.committed_in_maintenance);
    EXPECT_TRUE(report.watchdog_expired);
    EXPECT_TRUE(report.watchdog_retained);
    EXPECT_TRUE(report.joined);
    EXPECT_TRUE(report.platform_stopped);
    EXPECT_EQ(report.storage_status, NX_STORAGE_OK);
}
TEST_F(IndustrialPersistence, ValidWindowEdgeUsesAnAuthorizedPeriodAdjustment) {
    config.period_us = 350000U;
    config.sample_limit = 1U;
    example_industrial_report_t report{};
    EXPECT_EQ(example_industrial_run(&storage, &config, &report), NX_SUCCESS);
    EXPECT_EQ(report.durable_period_us, 340000U);
    EXPECT_TRUE(report.committed_in_maintenance);
    EXPECT_TRUE(report.joined);
    EXPECT_TRUE(report.platform_stopped);
}

TEST_F(IndustrialPersistence,
       TightValidWindowKeepsCurrentPeriodWhenNoStepFits) {
    config.period_us = 80000U;
    config.watchdog_timeout_us = 325000U;
    config.sample_limit = 1U;
    example_industrial_report_t report{};
    EXPECT_EQ(example_industrial_run(&storage, &config, &report), NX_SUCCESS);
    EXPECT_EQ(report.durable_period_us, 80000U);
    EXPECT_TRUE(report.committed_in_maintenance);
    EXPECT_TRUE(report.joined);
    EXPECT_TRUE(report.platform_stopped);
}

struct DeadlineModel {
    testing::MockFunction<nx_time_us_t()> clock;
    testing::MockFunction<nx_result_t(uint32_t, nx_time_us_t)> waiting;
    uint32_t reads = 0U;
    static nx_time_us_t read(void* context) {
        return static_cast<DeadlineModel*>(context)->clock.Call();
    }
    static uint32_t arm(void*) {
        return 0U;
    }
    static nx_result_t wait(void* context, uint32_t sequence,
                            nx_time_us_t deadline) {
        return static_cast<DeadlineModel*>(context)->waiting.Call(sequence,
                                                                  deadline);
    }
    static nx_result_t wake(void*) {
        return NX_SUCCESS;
    }
    static nx_result_t start(void*, void*, nx_time_us_t) {
        return NX_ERROR_UNSUPPORTED;
    }
    static nx_result_t service(void*, nx_result_t*, size_t*) {
        return NX_ERROR_UNSUPPORTED;
    }
    static nx_result_t cancel(void*) {
        return NX_SUCCESS;
    }
};

TEST(IndustrialDeadline, PermanentlyReadyTimerCannotExtendOverallDeadline) {
    DeadlineModel model;
    EXPECT_CALL(model.clock, Call())
        .Times(testing::AtLeast(1))
        .WillRepeatedly([&] {
            uint32_t step = model.reads++;
            return 1000U + (step < 100U ? step : 100U);
        });
    EXPECT_CALL(model.waiting, Call(testing::_, testing::_))
        .Times(testing::AnyNumber())
        .WillRepeatedly(testing::Return(NX_ERROR_TIMEOUT));
    nx_bus_owner_t owner{};
    nx_owner_slot_t slot{};
    nx_spi_endpoint_t endpoint{};
    const nx_clock_t clock{DeadlineModel::read, &model};
    const nx_wait_port_t changed{&model, DeadlineModel::arm,
                                 DeadlineModel::wait, DeadlineModel::wake};
    const nx_owner_executor_port_t executor{nullptr, DeadlineModel::start,
                                            DeadlineModel::service,
                                            DeadlineModel::cancel, false};
    ASSERT_EQ(nx_bus_owner_init(&owner, &slot, 1U,
                                nx_owner_guard_port_t{nullptr, enter, leave},
                                executor, clock, nullptr),
              NX_SUCCESS);
    example_industrial_spi_t bridge{};
    ASSERT_EQ(example_industrial_spi_open(&bridge, &owner, &endpoint, changed,
                                          clock, 1U),
              NX_SUCCESS);
    const auto transport = example_industrial_spi_transport(&bridge);
    const uint8_t tx[2] = {0x80U, 0U};
    EXPECT_EQ(
        transport.exchange(transport.context, tx, nullptr, sizeof(tx), 1009U),
        NX_ERROR_TIMEOUT);
    EXPECT_LE(model.reads, 32U);
    EXPECT_TRUE(bridge.pending);
    nx_bus_owner_stop(&owner);
    for (size_t step = 0U; step < 4U && !nx_bus_owner_idle(&owner); ++step) {
        (void)nx_bus_owner_service(&owner);
    }
    if (!nx_bus_owner_idle(&owner)) {
        ADD_FAILURE() << "Model request retained; refusing storage reclamation";
        std::abort();
    }
    EXPECT_EQ(example_industrial_spi_join(&bridge, 1200U), NX_SUCCESS);
}
} /* namespace */
