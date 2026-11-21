/**
 * \file            spi_bootstrap_test.cpp
 * \brief           Real MCU SPI composition creates tasks inside OS bootstrap
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-21
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "nexus/core/fault.h"
#include "nexus/os/storage.h"
#include "nexus_bindings.h"
#include "shared_spi_application.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <unistd.h>

namespace {
using ::testing::_;
using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::StrictMock;

struct Entered {};
struct Parked {};

/** \brief Mock external kernel/bootstrap effects, not the C application. */
class Ports {
  public:
    MOCK_METHOD(nx_platform_start_result_t, platform_start, ());
    MOCK_METHOD(nx_result_t, platform_stop, ());
    MOCK_METHOD(void, run, ());
    MOCK_METHOD(nx_result_t, create, (void*, size_t, uint32_t));
    MOCK_METHOD(void, fault, ());
};
Ports* s_ports;
bool s_inside_bootstrap;

class SpiBootstrap : public ::testing::Test {
  protected:
    void SetUp() override {
        s_ports = &ports;
        s_inside_bootstrap = false;
    }
    void TearDown() override {
        s_ports = nullptr;
    }
    StrictMock<Ports> ports;
};

TEST_F(SpiBootstrap, CoordinatorCreationIsInsideTheKernelEntry) {
    EXPECT_EXIT(
        {
            InSequence order;
            EXPECT_CALL(ports, platform_start())
                .WillOnce(Return(
                    nx_platform_start_result_t{NX_SUCCESS, NX_SUCCESS, 0}));
            EXPECT_CALL(ports, run());
            EXPECT_CALL(ports, create(_, _, 2U))
                .WillOnce(Invoke([](void* stack, size_t bytes, uint32_t) {
                    EXPECT_EQ(reinterpret_cast<uintptr_t>(stack) %
                                  NX_OS_STACK_ALIGNMENT,
                              0U);
                    EXPECT_EQ(bytes,
                              configMINIMAL_STACK_SIZE * sizeof(StackType_t));
                    return NX_SUCCESS;
                }));
            example_spi_report_t report{};
            EXPECT_THROW((void)example_shared_spi_run(&report), Entered);
            _exit(::testing::Mock::VerifyAndClearExpectations(&ports) &&
                          !::testing::Test::HasFailure()
                      ? 0
                      : 90);
        },
        ::testing::ExitedWithCode(0), "");
}

TEST_F(SpiBootstrap, FailedCreationStopsInfrastructureBeforeTerminalPolicy) {
    EXPECT_EXIT(
        {
            InSequence order;
            EXPECT_CALL(ports, platform_start())
                .WillOnce(Return(
                    nx_platform_start_result_t{NX_SUCCESS, NX_SUCCESS, 0}));
            EXPECT_CALL(ports, run());
            EXPECT_CALL(ports, create(_, _, 2U)).WillOnce(Return(NX_ERROR_IO));
            EXPECT_CALL(ports, platform_stop()).WillOnce(Return(NX_SUCCESS));
            EXPECT_CALL(ports, fault()).WillOnce(Invoke([] {
                throw Parked{};
            }));
            example_spi_report_t report{};
            EXPECT_THROW((void)example_shared_spi_run(&report), Parked);
            _exit(::testing::Mock::VerifyAndClearExpectations(&ports) &&
                          !::testing::Test::HasFailure()
                      ? 0
                      : 90);
        },
        ::testing::ExitedWithCode(0), "");
}

TEST_F(SpiBootstrap, RefusedPlatformNeverTouchesTheKernel) {
    EXPECT_CALL(ports, platform_start())
        .WillOnce(
            Return(nx_platform_start_result_t{NX_ERROR_IO, NX_SUCCESS, 0}));
    example_spi_report_t report{};
    EXPECT_EQ(example_shared_spi_run(&report), 2);
}
} /* namespace */

extern "C" nx_platform_start_result_t __wrap_nx_platform_start(void) {
    return s_ports->platform_start();
}
extern "C" nx_result_t __wrap_nx_platform_stop(void) {
    return s_ports->platform_stop();
}
extern "C" nx_result_t __wrap_nx_os_run(void (*startup)(void*), void* context) {
    s_ports->run();
    s_inside_bootstrap = true;
    startup(context);
    throw Entered{};
}
extern "C" nx_result_t __wrap_nx_task_start(nx_task_t*, const char*,
                                            void* stack, size_t bytes,
                                            uint32_t priority, void (*)(void*),
                                            void*) {
    const nx_result_t result = s_ports->create(stack, bytes, priority);
    return s_inside_bootstrap ? result : NX_ERROR_CONTEXT;
}
extern "C" nx_result_t __wrap_nx_task_join_until(nx_task_t*, nx_time_us_t) {
    ADD_FAILURE() << "Bootstrap cannot run or join a worker";
    return NX_ERROR_CONTEXT;
}
extern "C" nx_time_us_t __wrap_nx_os_now_us(void) {
    return 0U;
}
extern "C" nx_result_t __wrap_nx_sleep_until(nx_time_us_t) {
    ADD_FAILURE() << "Bootstrap cannot run a worker";
    return NX_ERROR_CONTEXT;
}
extern "C" void __wrap_nx_fault_observe(nx_fault_domain_t, nx_result_t,
                                        const char*, unsigned) {
    s_ports->fault();
}
