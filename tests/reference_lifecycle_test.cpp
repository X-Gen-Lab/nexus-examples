/**
 * \file            reference_lifecycle_test.cpp
 * \brief           Public cold diagnostics preserve lifecycle responsibility
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-10-16
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "nexus/arch/arch.h"
#include "nexus/io/native/model.h"
#include "nexus_bindings.h"
#include "nexus_factory.h"
#include <gtest/gtest.h>

namespace {
nx_platform_diagnostic_t stale_diagnostic() {
    return {NX_PLATFORM_PHASE_CONTROLLER, 99U, NX_PLATFORM_PHASE_RESOURCES,
            98U};
}

void expect_clear(const nx_platform_diagnostic_t& diagnostic) {
    EXPECT_EQ(diagnostic.phase, NX_PLATFORM_PHASE_NONE);
    EXPECT_EQ(diagnostic.controller_index, SIZE_MAX);
    EXPECT_EQ(diagnostic.cleanup_phase, NX_PLATFORM_PHASE_NONE);
    EXPECT_EQ(diagnostic.cleanup_controller_index, SIZE_MAX);
}

TEST(ReferenceLifecycle, SuccessfulColdCallsOverwriteCallerOwnedMetadata) {
    nx_platform_diagnostic_t diagnostic = stale_diagnostic();
    nx_platform_start_result_t startup =
        nx_platform_start_diagnosed(&diagnostic);
    ASSERT_EQ(startup.primary, NX_SUCCESS);
    EXPECT_EQ(startup.cleanup, NX_SUCCESS);
    EXPECT_EQ(startup.remaining_effects, 0U);
    expect_clear(diagnostic);
    diagnostic = stale_diagnostic();
    EXPECT_EQ(nx_platform_stop_diagnosed(&diagnostic), NX_SUCCESS);
    expect_clear(diagnostic);
}

TEST(ReferenceLifecycle, ContextRefusalReportsNoControllerOrCleanupAttempt) {
    nx_platform_diagnostic_t diagnostic = stale_diagnostic();
    nx_arch_irq_state_t token = nx_arch_irq_save();
    nx_platform_start_result_t startup =
        nx_platform_start_diagnosed(&diagnostic);
    nx_arch_irq_restore(token);
    EXPECT_EQ(startup.primary, NX_ERROR_CONTEXT);
    EXPECT_EQ(startup.cleanup, NX_SUCCESS);
    EXPECT_EQ(startup.remaining_effects, 0U);
    EXPECT_EQ(diagnostic.phase, NX_PLATFORM_PHASE_CONTEXT);
    EXPECT_EQ(diagnostic.controller_index, SIZE_MAX);
    EXPECT_EQ(diagnostic.cleanup_phase, NX_PLATFORM_PHASE_NONE);
    EXPECT_EQ(diagnostic.cleanup_controller_index, SIZE_MAX);
}

class ActiveReference : public testing::Test {
  protected:
    const nx_uart_port_t* port = nx_factory_uart(NX_UART_ID_UART0);
    nx_uart_tx_request_t request{};
    const uint8_t bytes[4] = {'n', 'e', 'x', 'u'};

    void SetUp() override {
        ASSERT_NE(port, nullptr);
        nx_request_initialize(&request.base);
        nx_platform_start_result_t startup = nx_platform_start();
        ASSERT_EQ(startup.primary, NX_SUCCESS);
        ASSERT_EQ(startup.cleanup, NX_SUCCESS);
        ASSERT_EQ(startup.remaining_effects, 0U);
    }

    void TearDown() override {
        nx_native_uart_model_fault(port, false, false);
        nx_uart_port_service(port);
        EXPECT_EQ(nx_platform_stop(), NX_SUCCESS);
    }
};

TEST_F(ActiveReference, RepeatedStartDoesNotReleaseTheOriginalOwner) {
    nx_platform_diagnostic_t diagnostic = stale_diagnostic();
    nx_platform_start_result_t startup =
        nx_platform_start_diagnosed(&diagnostic);
    EXPECT_EQ(startup.primary, NX_ERROR_STATE);
    EXPECT_EQ(startup.cleanup, NX_SUCCESS);
    /* The retained platform receipt reports the existing owner's effects. */
    EXPECT_EQ(startup.remaining_effects, 1U);
    EXPECT_EQ(diagnostic.phase, NX_PLATFORM_PHASE_CONTEXT);
    EXPECT_EQ(diagnostic.controller_index, SIZE_MAX);
    EXPECT_EQ(diagnostic.cleanup_phase, NX_PLATFORM_PHASE_NONE);
    EXPECT_EQ(diagnostic.cleanup_controller_index, SIZE_MAX);
    EXPECT_EQ(nx_native_uart_model_receive(port, 'x', 0U), NX_SUCCESS);
}

TEST_F(ActiveReference, BusyStopRetainsBorrowUntilOwnerContinuesService) {
    ASSERT_EQ(
        nx_uart_tx_prepare(&request, bytes, sizeof(bytes), NX_DEADLINE_NEVER),
        NX_SUCCESS);
    ASSERT_EQ(nx_uart_port_submit(port, &request), NX_SUCCESS);
    nx_native_uart_model_fault(port, false, true);
    nx_platform_diagnostic_t diagnostic = stale_diagnostic();
    EXPECT_EQ(nx_platform_stop_diagnosed(&diagnostic), NX_ERROR_BUSY);
    EXPECT_EQ(diagnostic.phase, NX_PLATFORM_PHASE_CONTROLLER);
    EXPECT_NE(diagnostic.controller_index, SIZE_MAX);
    EXPECT_EQ(diagnostic.cleanup_phase, NX_PLATFORM_PHASE_NONE);
    EXPECT_EQ(diagnostic.cleanup_controller_index, SIZE_MAX);
    EXPECT_EQ(nx_request_state(&request.base), NX_REQUEST_QUARANTINED);
    nx_result_t result;
    size_t transferred;
    EXPECT_EQ(nx_request_result(&request.base, &result, &transferred),
              NX_ERROR_BUSY);
    const nx_platform_diagnostic_t retained = diagnostic;
    EXPECT_EQ(nx_platform_stop_diagnosed(&diagnostic), NX_ERROR_BUSY);
    EXPECT_EQ(diagnostic.controller_index, retained.controller_index);
    nx_native_uart_model_fault(port, false, false);
    nx_uart_port_service(port);
    ASSERT_EQ(nx_request_state(&request.base), NX_REQUEST_SETTLED);
    ASSERT_EQ(nx_request_result(&request.base, &result, &transferred),
              NX_SUCCESS);
    EXPECT_EQ(result, NX_ERROR_CANCELLED);
    EXPECT_LE(transferred, sizeof(bytes));
    EXPECT_EQ(nx_platform_stop_diagnosed(&diagnostic), NX_SUCCESS);
    expect_clear(diagnostic);
    EXPECT_EQ(retained.phase, NX_PLATFORM_PHASE_CONTROLLER);
    EXPECT_NE(retained.controller_index, SIZE_MAX);
}
} /* namespace */
