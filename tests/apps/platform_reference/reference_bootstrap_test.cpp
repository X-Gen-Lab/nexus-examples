/**
 * \file            reference_bootstrap_test.cpp
 * \brief           Actual C11 reference enters the OS before creating its task
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-21
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "nexus/os/storage.h"
#include "nexus_bindings.h"
#include "reference_bootstrap.h"
#include <csignal>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <unistd.h>

namespace {
using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::StrictMock;

/** \brief External bootstrap and platform ports, not a replacement main. */
class Ports {
  public:
    MOCK_METHOD(nx_platform_start_result_t, platform_start, ());
    MOCK_METHOD(nx_result_t, run, ());
    MOCK_METHOD(nx_result_t, task_start,
                (nx_task_t*, const char*, void*, size_t, uint32_t,
                 void (*)(void*), void*));
};

Ports* s_ports;
volatile sig_atomic_t s_inside_bootstrap;
volatile sig_atomic_t s_bootstrap_entered;
volatile sig_atomic_t s_create_failed;
volatile sig_atomic_t s_kernel_returned;
volatile sig_atomic_t s_cleanup_attempted;
volatile sig_atomic_t s_outside_bootstrap;

class ReferenceBootstrap : public ::testing::Test {
  protected:
    void SetUp() override {
        s_ports = &ports;
        s_inside_bootstrap = 0;
        s_bootstrap_entered = 0;
        s_create_failed = 0;
        s_kernel_returned = 0;
        s_cleanup_attempted = 0;
        s_outside_bootstrap = 0;
    }

    void TearDown() override {
        s_ports = nullptr;
    }

    StrictMock<Ports> ports;
};

TEST_F(ReferenceBootstrap, CreatesAlignedByteSizedTaskInsideKernelEntry) {
    EXPECT_CALL(ports, platform_start())
        .WillOnce(
            Return(nx_platform_start_result_t{NX_SUCCESS, NX_SUCCESS, 0}));
    EXPECT_CALL(ports, run()).WillOnce(Return(NX_SUCCESS));
    EXPECT_CALL(ports, task_start(_, _, _, _, _, _, _))
        .WillOnce(Invoke([](nx_task_t* task, const char* name, void* stack,
                            size_t bytes, uint32_t priority,
                            void (*entry)(void*), void* context) {
            EXPECT_NE(task, nullptr);
            EXPECT_STREQ(name, "reference");
            EXPECT_NE(stack, nullptr);
            EXPECT_EQ(
                reinterpret_cast<uintptr_t>(stack) % NX_OS_STACK_ALIGNMENT, 0U);
            EXPECT_EQ(bytes, 1024U);
            EXPECT_EQ(priority, 2U);
            EXPECT_NE(entry, nullptr);
            EXPECT_EQ(context, nullptr);
            return NX_SUCCESS;
        }));
    /* The mock returns an OS failure after invoking bootstrap. Production
     * successful nx_os_run does not return; main preserves its error exit.
     */
    EXPECT_EQ(example_reference_main(), 3);
    EXPECT_EQ(s_bootstrap_entered, 1);
    EXPECT_EQ(s_outside_bootstrap, 0);
    EXPECT_EQ(s_kernel_returned, 1);
    EXPECT_EQ(s_cleanup_attempted, 0);
}

TEST_F(ReferenceBootstrap, RefusedPlatformDoesNotEnterTheKernel) {
    EXPECT_CALL(ports, platform_start())
        .WillOnce(
            Return(nx_platform_start_result_t{NX_ERROR_IO, NX_SUCCESS, 0}));
    EXPECT_EQ(example_reference_main(), 1);
    EXPECT_EQ(s_bootstrap_entered, 0);
    EXPECT_EQ(s_cleanup_attempted, 0);
}

/** \brief Inspect only signal-safe observations of the terminal policy. */
void fail_stop_observed(int) {
    const bool retained = s_bootstrap_entered == 1 && s_create_failed == 1 &&
                          s_kernel_returned == 0 && s_cleanup_attempted == 0 &&
                          s_outside_bootstrap == 0;
    _exit(retained ? 0 : 90);
}

TEST_F(ReferenceBootstrap, BootstrapCreationFailureRetainsStaticOwnership) {
    EXPECT_EXIT(
        {
            EXPECT_CALL(ports, platform_start())
                .WillOnce(Return(
                    nx_platform_start_result_t{NX_SUCCESS, NX_SUCCESS, 0}));
            EXPECT_CALL(ports, run()).WillOnce(Return(NX_SUCCESS));
            EXPECT_CALL(ports, task_start(_, _, _, _, _, _, _))
                .WillOnce(Return(NX_ERROR_IO));
            if (std::signal(SIGALRM, fail_stop_observed) == SIG_ERR) {
                _exit(92);
            }
            (void)alarm(1);
            (void)example_reference_main();
            _exit(91);
        },
        ::testing::ExitedWithCode(0), "");
}
} /* namespace */

extern "C" nx_platform_start_result_t
__wrap_nx_platform_start_diagnosed(nx_platform_diagnostic_t*) {
    return s_ports->platform_start();
}

extern "C" nx_result_t
__wrap_nx_platform_stop_diagnosed(nx_platform_diagnostic_t*) {
    s_cleanup_attempted = 1;
    return NX_ERROR_STATE;
}

extern "C" nx_result_t __wrap_nx_os_run(void (*startup)(void*), void* context) {
    const nx_result_t result = s_ports->run();
    if (result != NX_SUCCESS) {
        return result;
    }
    s_inside_bootstrap = 1;
    s_bootstrap_entered = 1;
    startup(context);
    s_inside_bootstrap = 0;
    s_kernel_returned = 1;
    return NX_ERROR_IO;
}

extern "C" nx_result_t __wrap_nx_task_start(nx_task_t* task, const char* name,
                                            void* stack, size_t stack_bytes,
                                            uint32_t priority,
                                            void (*entry)(void*),
                                            void* context) {
    const nx_result_t selected = s_ports->task_start(
        task, name, stack, stack_bytes, priority, entry, context);
    if (s_inside_bootstrap == 0) {
        s_outside_bootstrap = 1;
        return NX_ERROR_CONTEXT;
    }
    if (selected != NX_SUCCESS) {
        s_create_failed = 1;
    }
    return selected;
}

extern "C" nx_time_us_t __wrap_nx_os_now_us(void) {
    ADD_FAILURE() << "The bootstrap contract must not run the worker";
    return 0U;
}

extern "C" nx_result_t __wrap_nx_sleep_until(nx_time_us_t) {
    ADD_FAILURE() << "The bootstrap contract must not run the worker";
    return NX_ERROR_STATE;
}
