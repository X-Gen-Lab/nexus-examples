/**
 * \file            threadx_application_test.cpp
 * \brief           Real ThreadX UART owner drains and deletes actual workers
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-25
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "entry.h"
#include "nexus/io/uart.h"
#include "nexus/os/storage.h"
#include "tx_api.h"
#include <atomic>
#include <cstdlib>
#include <gtest/gtest.h>
#include <sched.h>

extern "C" nx_result_t __real_nx_os_run(void (*)(void*), void*);
extern "C" nx_result_t __real_nx_task_start(nx_task_t*, const char*, void*,
                                            size_t, uint32_t, void (*)(void*),
                                            void*);
extern "C" nx_result_t __real_nx_task_join_until(nx_task_t*, nx_time_us_t);
extern "C" nx_result_t __real_nx_notify_destroy(nx_notify_t*);
extern "C" nx_result_t __real_nx_platform_stop(void);
extern "C" nx_result_t __real_nx_uart_port_attach_wake(const nx_uart_port_t*,
                                                       const nx_irq_wake_t*,
                                                       uint8_t);
extern "C" nx_result_t __real_nx_uart_port_stop(const nx_uart_port_t*);
extern "C" void __real_nx_uart_port_service(const nx_uart_port_t*);

namespace {
TX_THREAD observer{};
alignas(8) uint8_t observer_stack[8192];
nx_task_t* tasks[4]{};
size_t task_count;
std::atomic<size_t> joined;
std::atomic<size_t> notifications_destroyed;
std::atomic<size_t> platform_stops;
std::atomic<bool> early_reclamation;
std::atomic<size_t> publisher_detachments;
std::atomic<size_t> uart_stop_attempts;
std::atomic<bool> serviced_after_busy_stop;
std::atomic<bool> uart_stopped;
bool completed;
void (*application_startup)(void*);
void* application_context;

TEST(UartOwnerThreadX, BusyUartStopRetriesBeforeNotificationReclamation) {
    EXPECT_TRUE(completed);
    ASSERT_EQ(task_count, 4U);
    EXPECT_EQ(joined, 3U);
    EXPECT_EQ(notifications_destroyed, 4U);
    EXPECT_EQ(platform_stops, 0U);
    EXPECT_FALSE(early_reclamation);
    EXPECT_EQ(publisher_detachments, 1U);
    EXPECT_EQ(uart_stop_attempts, 2U);
    EXPECT_TRUE(serviced_after_busy_stop);
    EXPECT_TRUE(uart_stopped);
    for (size_t i = 0U; i < 3U; ++i) {
        EXPECT_FALSE(tasks[i]->live);
        EXPECT_EQ(tasks[i]->control.tx_thread_id, 0U);
        EXPECT_EQ(tasks[i]->finished.tx_semaphore_id, 0U);
    }
    /* The coordinator has returned but its caller-owned control remains live.
     * The scheduler and Tick still exist, so Platform clocks remain acquired.
     */
    EXPECT_TRUE(tasks[3]->live);
    const ULONG before = tx_time_get();
    EXPECT_EQ(tx_thread_sleep(2U), TX_SUCCESS);
    EXPECT_GT(tx_time_get(), before);
}

void observe(ULONG) {
    const auto deadline = nx_deadline_after(nx_time_now_us(), 2000000U);
    while (notifications_destroyed != 4U && nx_time_now_us() < deadline) {
        (void)tx_thread_sleep(1U);
    }
    completed = notifications_destroyed == 4U;
    std::exit(RUN_ALL_TESTS());
}

void startup(void*) {
    application_startup(application_context);
    if (tx_thread_create(&observer, const_cast<char*>("observer"), observe, 0U,
                         observer_stack, sizeof observer_stack, 16U, 16U,
                         TX_NO_TIME_SLICE, TX_AUTO_START) != TX_SUCCESS) {
        std::abort();
    }
}
} /* namespace */

extern "C" nx_result_t __wrap_nx_os_run(void (*entry)(void*), void* context) {
    application_startup = entry;
    application_context = context;
    return __real_nx_os_run(startup, nullptr);
}

extern "C" nx_result_t __wrap_nx_task_start(nx_task_t* task, const char* name,
                                            void* stack, size_t bytes,
                                            uint32_t priority,
                                            void (*entry)(void*),
                                            void* context) {
    const auto result = __real_nx_task_start(task, name, stack, bytes, priority,
                                             entry, context);
    if (result == NX_SUCCESS && task_count < 4U) {
        tasks[task_count++] = task;
    }
    return result;
}

extern "C" nx_result_t __wrap_nx_task_join_until(nx_task_t* task,
                                                 nx_time_us_t deadline) {
    const auto result = __real_nx_task_join_until(task, deadline);
    if (result == NX_SUCCESS) {
        ++joined;
    }
    return result;
}

extern "C" nx_result_t __wrap_nx_notify_destroy(nx_notify_t* notification) {
    if (joined != 3U || publisher_detachments != 1U || !uart_stopped) {
        early_reclamation = true;
    }
    const auto result = __real_nx_notify_destroy(notification);
    if (result == NX_SUCCESS) {
        ++notifications_destroyed;
    }
    return result;
}

extern "C" nx_result_t
__wrap_nx_uart_port_attach_wake(const nx_uart_port_t* port,
                                const nx_irq_wake_t* wake, uint8_t priority) {
    const auto result = __real_nx_uart_port_attach_wake(port, wake, priority);
    if (result == NX_SUCCESS && wake == nullptr) {
        ++publisher_detachments;
    }
    return result;
}

extern "C" nx_result_t __wrap_nx_uart_port_stop(const nx_uart_port_t* port) {
    if (joined != 3U || publisher_detachments != 1U ||
        notifications_destroyed != 0U) {
        early_reclamation = true;
    }
    if (++uart_stop_attempts == 1U) {
        return NX_ERROR_BUSY;
    }
    const auto result = __real_nx_uart_port_stop(port);
    if (result == NX_SUCCESS) {
        uart_stopped = true;
    }
    return result;
}

extern "C" void __wrap_nx_uart_port_service(const nx_uart_port_t* port) {
    if (uart_stop_attempts == 1U && notifications_destroyed == 0U) {
        serviced_after_busy_stop = true;
    }
    __real_nx_uart_port_service(port);
}

extern "C" nx_result_t __wrap_nx_platform_stop(void) {
    ++platform_stops;
    return __real_nx_platform_stop();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    cpu_set_t allowed;
    if (sched_getaffinity(0, sizeof allowed, &allowed) != 0) {
        return 70;
    }
    for (unsigned i = 0U; i < CPU_SETSIZE; ++i) {
        if (CPU_ISSET(i, &allowed)) {
            CPU_ZERO(&allowed);
            CPU_SET(i, &allowed);
            if (sched_setaffinity(0, sizeof allowed, &allowed) != 0) {
                return 71;
            }
            return example_uart_threadx_main();
        }
    }
    return 73;
}
