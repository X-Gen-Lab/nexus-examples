/**
 * \file            uart_bootstrap_test.cpp
 * \brief           Actual MCU UART startup preserves bootstrap and rollback
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-21
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "nexus/core/fault.h"
#include "nexus/io/uart.h"
#include "nexus/os/storage.h"
#include "nexus_bindings.h"
#include "nexus_config.h"
#include "uart_bootstrap.h"
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
/** \brief External OS/IRQ ports leave the real rollback implementation intact.
 */
class Ports {
  public:
    MOCK_METHOD(nx_platform_start_result_t, platform_start, ());
    MOCK_METHOD(nx_result_t, platform_stop, ());
    MOCK_METHOD(void, run, ());
    MOCK_METHOD(nx_result_t, notify, (nx_notify_t*));
    MOCK_METHOD(nx_result_t, destroy, (nx_notify_t*));
    MOCK_METHOD(nx_result_t, create, (nx_task_t*, void*, size_t, uint32_t));
    MOCK_METHOD(nx_result_t, discard, (nx_task_t*));
    MOCK_METHOD(nx_result_t, attach, (const nx_irq_wake_t*));
    MOCK_METHOD(nx_result_t, uart_stop, ());
    MOCK_METHOD(void, fault, ());
};
Ports* s_ports;
bool s_inside_bootstrap;

uint32_t arm(void*) {
    return 0U;
}
nx_result_t wait(void*, uint32_t, uint64_t) {
    return NX_ERROR_CONTEXT;
}
nx_result_t wake(void*) {
    return NX_SUCCESS;
}

class UartBootstrap : public ::testing::Test {
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

TEST_F(UartBootstrap, AllKernelObjectsAreCreatedInsideBootstrap) {
    EXPECT_EXIT(
        {
            InSequence order;
            EXPECT_CALL(ports, platform_start())
                .WillOnce(Return(
                    nx_platform_start_result_t{NX_SUCCESS, NX_SUCCESS, 0}));
            EXPECT_CALL(ports, run());
            EXPECT_CALL(ports, notify(_))
                .Times(4)
                .WillRepeatedly(Return(NX_SUCCESS));
            EXPECT_CALL(ports, attach(::testing::NotNull()))
                .WillOnce(Return(NX_SUCCESS));
            EXPECT_CALL(ports, create(_, _, _, 2U))
                .WillOnce(Return(NX_SUCCESS));
            EXPECT_CALL(ports, create(_, _, _, 3U))
                .Times(2)
                .WillRepeatedly(Return(NX_SUCCESS));
            EXPECT_CALL(ports, create(_, _, _, 1U))
                .WillOnce(Return(NX_SUCCESS));
            EXPECT_THROW((void)example_uart_mcu_main(), Entered);
            _exit(::testing::Mock::VerifyAndClearExpectations(&ports) &&
                          !::testing::Test::HasFailure()
                      ? 0
                      : 90);
        },
        ::testing::ExitedWithCode(0), "");
}

TEST_F(UartBootstrap, PartialNotificationFailureRollsBackOnlyCreatedObjects) {
    EXPECT_EXIT(
        {
            nx_notify_t* created = nullptr;
            InSequence order;
            EXPECT_CALL(ports, platform_start())
                .WillOnce(Return(
                    nx_platform_start_result_t{NX_SUCCESS, NX_SUCCESS, 0}));
            EXPECT_CALL(ports, run());
            EXPECT_CALL(ports, notify(_))
                .WillOnce(Invoke([&](nx_notify_t* value) {
                    created = value;
                    return NX_SUCCESS;
                }));
            EXPECT_CALL(ports, notify(_)).WillOnce(Return(NX_ERROR_IO));
            EXPECT_CALL(ports, attach(nullptr)).WillOnce(Return(NX_SUCCESS));
            EXPECT_CALL(ports, uart_stop()).WillOnce(Return(NX_SUCCESS));
            EXPECT_CALL(ports, destroy(_))
                .WillOnce(Invoke([&](nx_notify_t* value) {
                    EXPECT_EQ(value, created);
                    return NX_SUCCESS;
                }));
            EXPECT_CALL(ports, platform_stop()).WillOnce(Return(NX_SUCCESS));
            EXPECT_CALL(ports, fault()).WillOnce(Invoke([] {
                throw Parked{};
            }));
            EXPECT_THROW((void)example_uart_mcu_main(), Parked);
            _exit(::testing::Mock::VerifyAndClearExpectations(&ports) &&
                          !::testing::Test::HasFailure()
                      ? 0
                      : 90);
        },
        ::testing::ExitedWithCode(0), "");
}

TEST_F(UartBootstrap,
       PartialWorkerFailureDiscardsBeforeDestroyingNotifications) {
    EXPECT_EXIT(
        {
            nx_task_t* created = nullptr;
            nx_task_t* attempted = nullptr;
            nx_notify_t* notifications[4]{};
            size_t index = 0U;
            InSequence order;
            EXPECT_CALL(ports, platform_start())
                .WillOnce(Return(
                    nx_platform_start_result_t{NX_SUCCESS, NX_SUCCESS, 0}));
            EXPECT_CALL(ports, run());
            EXPECT_CALL(ports, notify(_))
                .Times(4)
                .WillRepeatedly(Invoke([&](nx_notify_t* value) {
                    notifications[index++] = value;
                    return NX_SUCCESS;
                }));
            EXPECT_CALL(ports, attach(::testing::NotNull()))
                .WillOnce(Return(NX_SUCCESS));
            EXPECT_CALL(ports, create(_, _, _, 2U))
                .WillOnce(
                    Invoke([&](nx_task_t* value, void*, size_t, uint32_t) {
                        created = value;
                        return NX_SUCCESS;
                    }));
            EXPECT_CALL(ports, create(_, _, _, 3U))
                .WillOnce(
                    Invoke([&](nx_task_t* value, void*, size_t, uint32_t) {
                        attempted = value;
                        return NX_ERROR_IO;
                    }));
            EXPECT_CALL(ports, discard(_))
                .WillOnce(Invoke([&](nx_task_t* value) {
                    EXPECT_EQ(value, attempted);
                    return NX_SUCCESS;
                }));
            EXPECT_CALL(ports, discard(_))
                .WillOnce(Invoke([&](nx_task_t* value) {
                    EXPECT_EQ(value, created);
                    return NX_SUCCESS;
                }));
            EXPECT_CALL(ports, attach(nullptr)).WillOnce(Return(NX_SUCCESS));
            EXPECT_CALL(ports, uart_stop()).WillOnce(Return(NX_SUCCESS));
            EXPECT_CALL(ports, destroy(_))
                .Times(4)
                .WillRepeatedly(Invoke([&](nx_notify_t* value) {
                    EXPECT_EQ(value, notifications[--index]);
                    return NX_SUCCESS;
                }));
            EXPECT_CALL(ports, platform_stop()).WillOnce(Return(NX_SUCCESS));
            EXPECT_CALL(ports, fault()).WillOnce(Invoke([] {
                throw Parked{};
            }));
            EXPECT_THROW((void)example_uart_mcu_main(), Parked);
            _exit(::testing::Mock::VerifyAndClearExpectations(&ports) &&
                          !::testing::Test::HasFailure()
                      ? 0
                      : 90);
        },
        ::testing::ExitedWithCode(0), "");
}

#if defined(NEXUS_BACKEND_THREADX)
struct RetainedFailure {
    size_t failed_index;
    nx_result_t discard_result;
};

/** \brief A failed resume can retain the actual candidate control and loan. */
class RetainedWorkerFailure
    : public UartBootstrap,
      public ::testing::WithParamInterface<RetainedFailure> {};

TEST_P(RetainedWorkerFailure, CandidateMustWithdrawBeforeEarlierEffects) {
    const auto selected = GetParam();
    EXPECT_EXIT(
        {
            nx_task_t* candidates[4]{};
            nx_notify_t* notifications[4]{};
            size_t created = 0U;
            size_t notified = 0U;
            size_t withdrawing = selected.failed_index + 1U;
            InSequence order;
            EXPECT_CALL(ports, platform_start())
                .WillOnce(Return(
                    nx_platform_start_result_t{NX_SUCCESS, NX_SUCCESS, 0}));
            EXPECT_CALL(ports, run());
            EXPECT_CALL(ports, notify(_))
                .Times(4)
                .WillRepeatedly(Invoke([&](nx_notify_t* value) {
                    notifications[notified++] = value;
                    return NX_SUCCESS;
                }));
            EXPECT_CALL(ports, attach(::testing::NotNull()))
                .WillOnce(Return(NX_SUCCESS));
            EXPECT_CALL(ports, create(_, _, _, _))
                .Times(static_cast<int>(selected.failed_index + 1U))
                .WillRepeatedly(
                    Invoke([&](nx_task_t* value, void*, size_t, uint32_t) {
                        candidates[created] = value;
                        value->live = true;
                        return created++ == selected.failed_index ? NX_ERROR_IO
                                                                  : NX_SUCCESS;
                    }));
            EXPECT_CALL(ports, discard(_))
                .Times(static_cast<int>(selected.discard_result == NX_SUCCESS
                                            ? selected.failed_index + 1U
                                            : 1U))
                .WillRepeatedly(Invoke([&](nx_task_t* value) {
                    EXPECT_EQ(value, candidates[withdrawing - 1U]);
                    if (value != candidates[withdrawing - 1U]) {
                        /* Bound the pre-fix failure without entering its park
                         * loop after it skipped the failed candidate. */
                        throw Parked{};
                    }
                    EXPECT_TRUE(value->live);
                    if (selected.discard_result == NX_SUCCESS) {
                        value->live = false;
                        --withdrawing;
                    }
                    return selected.discard_result;
                }));
            if (selected.discard_result == NX_SUCCESS) {
                EXPECT_CALL(ports, attach(nullptr))
                    .WillOnce(Return(NX_SUCCESS));
                EXPECT_CALL(ports, uart_stop()).WillOnce(Return(NX_SUCCESS));
                EXPECT_CALL(ports, destroy(_))
                    .Times(4)
                    .WillRepeatedly(Invoke([&](nx_notify_t* value) {
                        EXPECT_EQ(value, notifications[--notified]);
                        return NX_SUCCESS;
                    }));
                EXPECT_CALL(ports, platform_stop())
                    .WillOnce(Return(NX_SUCCESS));
            }
            EXPECT_CALL(ports, fault()).WillOnce(Invoke([&] {
                for (size_t i = 0U; i < created; ++i) {
                    EXPECT_EQ(candidates[i]->live,
                              selected.discard_result != NX_SUCCESS);
                }
                throw Parked{};
            }));
            EXPECT_THROW((void)example_uart_mcu_main(), Parked);
            _exit(::testing::Mock::VerifyAndClearExpectations(&ports) &&
                          !::testing::Test::HasFailure()
                      ? 0
                      : 90);
        },
        ::testing::ExitedWithCode(0), "");
}

INSTANTIATE_TEST_SUITE_P(
    ThreadXResumeFailure, RetainedWorkerFailure,
    ::testing::Values(
        RetainedFailure{0U, NX_SUCCESS}, RetainedFailure{1U, NX_SUCCESS},
        RetainedFailure{3U, NX_SUCCESS}, RetainedFailure{0U, NX_ERROR_IO},
        RetainedFailure{1U, NX_ERROR_IO}, RetainedFailure{3U, NX_ERROR_IO},
        RetainedFailure{0U, NX_ERROR_INVALID},
        RetainedFailure{1U, NX_ERROR_INVALID},
        RetainedFailure{3U, NX_ERROR_INVALID}));
#endif
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
extern "C" nx_result_t __wrap_nx_notify_init(nx_notify_t* notification) {
    const nx_result_t result = s_ports->notify(notification);
    return s_inside_bootstrap ? result : NX_ERROR_CONTEXT;
}
extern "C" nx_wait_port_t __wrap_nx_notify_port(nx_notify_t* notification) {
    return {notification, arm, wait, wake};
}
extern "C" nx_result_t __wrap_nx_notify_destroy(nx_notify_t* notification) {
    return s_ports->destroy(notification);
}
extern "C" nx_result_t __wrap_nx_task_start(nx_task_t* task, const char*,
                                            void* stack, size_t bytes,
                                            uint32_t priority, void (*)(void*),
                                            void*) {
    EXPECT_EQ(reinterpret_cast<uintptr_t>(stack) % NX_OS_STACK_ALIGNMENT, 0U);
    EXPECT_EQ(bytes, 256U * sizeof(nx_os_stack_word_t));
    const nx_result_t result = s_ports->create(task, stack, bytes, priority);
    return s_inside_bootstrap ? result : NX_ERROR_CONTEXT;
}
extern "C" nx_result_t
__wrap_nx_task_discard_before_scheduler(nx_task_t* task) {
    return s_ports->discard(task);
}
extern "C" nx_result_t __wrap_nx_task_join_until(nx_task_t*, nx_time_us_t) {
    ADD_FAILURE() << "Bootstrap cannot run or join a worker";
    return NX_ERROR_CONTEXT;
}
extern "C" nx_result_t
__wrap_nx_uart_port_attach_wake(const nx_uart_port_t*,
                                const nx_irq_wake_t* wake_port, uint8_t) {
    return s_ports->attach(wake_port);
}
extern "C" nx_result_t __wrap_nx_uart_port_stop(const nx_uart_port_t*) {
    return s_ports->uart_stop();
}
extern "C" void __wrap_nx_fault_observe(nx_fault_domain_t, nx_result_t,
                                        const char*, unsigned) {
    s_ports->fault();
}
#if defined(NEXUS_BACKEND_THREADX)
extern "C" void __wrap_nx_threadx_fail_stop(nx_result_t) {
    throw Parked{};
}
#else
extern "C" void __wrap_nx_freertos_assert_failed(const char*, unsigned) {
    throw Parked{};
}
#endif
