/**
 * \file            compound_stop2_test.cpp
 * \brief           Compile-time compound Stop2 joins precede mask restoration
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-10-30
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "nexus_bindings.h"
#include "stop2_application.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace {
class CompoundBoundary {
  public:
    MOCK_METHOD(nx_arch_irq_state_t, Save, ());
    MOCK_METHOD(void, Restore, (nx_arch_irq_state_t));
    MOCK_METHOD(nx_stm32u575_stop2_result_t, Enter, (nx_clock_barrier_t));
    MOCK_METHOD(nx_stm32u575_stop2_result_t, Recover, (nx_clock_barrier_t));
    MOCK_METHOD(example_stop2_join_result_t, Join, ());
    MOCK_METHOD(nx_result_t, Reopen, (nx_clock_barrier_t));
};
CompoundBoundary* boundary;
class CompoundStop2 : public testing::Test {
  protected:
    testing::StrictMock<CompoundBoundary> hardware;
    example_stop2_application_t app{};
    nx_stm32u575_stop2_result_t transition{};
    unsigned domain_storage{};
    void SetUp() override {
        boundary = &hardware;
        app.domain = reinterpret_cast<nx_clock_domain_t*>(&domain_storage);
        app.barrier = {app.domain, 7U};
        app.phase = EXAMPLE_STOP2_ENTER;
    }
    void TearDown() override {
        boundary = nullptr;
    }
    void enter(uint32_t mask = 0U) {
        EXPECT_CALL(hardware, Save())
            .WillOnce(testing::Return(nx_arch_irq_state_t{mask}));
        EXPECT_CALL(hardware, Enter(testing::_))
            .WillOnce([this](nx_clock_barrier_t owners) {
                EXPECT_EQ(owners.domain, app.domain);
                EXPECT_EQ(owners.epoch, app.barrier.epoch);
                return transition;
            });
        EXPECT_EQ(example_stop2_progress(&app), NX_ERROR_BUSY);
        EXPECT_TRUE(app.mask_retained);
    }
    void joined(nx_result_t operation = NX_SUCCESS) {
        EXPECT_CALL(hardware, Join())
            .WillOnce(testing::Return(
                example_stop2_join_result_t{NX_SUCCESS, operation}));
        EXPECT_EQ(example_stop2_progress(&app), NX_ERROR_BUSY);
        EXPECT_EQ(app.phase, EXAMPLE_STOP2_REOPEN);
        EXPECT_TRUE(app.mask_retained);
    }
    void failed_finish(nx_result_t error, uint32_t mask = 0U) {
        EXPECT_CALL(hardware,
                    Restore(testing::Field(&nx_arch_irq_state_t::value, mask)));
        EXPECT_EQ(example_stop2_progress(&app), error);
        EXPECT_EQ(app.phase, EXAMPLE_STOP2_DONE);
        EXPECT_FALSE(app.mask_retained);
        EXPECT_FALSE(app.restarted);
    }
};
TEST_F(CompoundStop2, SuccessfulTransitionStillRequiresAnExplicitMaskedJoin) {
    enter();
    EXPECT_EQ(app.phase, EXAMPLE_STOP2_JOIN);
    EXPECT_CALL(hardware, Join())
        .WillOnce(testing::Return(
            example_stop2_join_result_t{NX_ERROR_BUSY, NX_SUCCESS}));
    EXPECT_EQ(example_stop2_progress(&app), NX_ERROR_BUSY);
    EXPECT_EQ(app.phase, EXAMPLE_STOP2_JOIN);
    EXPECT_TRUE(app.mask_retained);
    joined();
    testing::InSequence order;
    EXPECT_CALL(hardware,
                Restore(testing::Field(&nx_arch_irq_state_t::value, 0U)));
    EXPECT_CALL(hardware, Reopen(testing::_))
        .WillOnce(testing::Return(NX_SUCCESS));
    EXPECT_EQ(example_stop2_progress(&app), NX_ERROR_BUSY);
    EXPECT_EQ(app.phase, EXAMPLE_STOP2_ACQUIRE);
}
TEST_F(CompoundStop2, JoinFailureRetainsMaskAndFirstFailureThroughRetry) {
    enter();
    EXPECT_CALL(hardware, Join())
        .WillOnce(testing::Return(
            example_stop2_join_result_t{NX_ERROR_IO, NX_SUCCESS}));
    EXPECT_EQ(example_stop2_progress(&app), NX_ERROR_IO);
    EXPECT_EQ(app.first_error, NX_ERROR_IO);
    EXPECT_EQ(app.phase, EXAMPLE_STOP2_JOIN);
    EXPECT_TRUE(app.mask_retained);
    joined();
    failed_finish(NX_ERROR_IO);
}
TEST_F(CompoundStop2, RejectedTransitionStillJoinsComponentAccess) {
    transition.primary = NX_ERROR_INVALID;
    enter();
    EXPECT_EQ(app.phase, EXAMPLE_STOP2_JOIN);
    joined(NX_ERROR_STATE);
    failed_finish(NX_ERROR_INVALID);
}
TEST_F(CompoundStop2, FinalRequestErrorCannotReopenAdmissionOrRestart) {
    enter();
    joined(NX_ERROR_IO);
    failed_finish(NX_ERROR_IO);
}
TEST_F(CompoundStop2, BusyFinalOperationIsNotMistakenForSuccessfulProgress) {
    enter();
    joined(NX_ERROR_BUSY);
    failed_finish(NX_ERROR_BUSY);
}
TEST_F(CompoundStop2, FailedInitialStartupSkipsWfiButJoinsThePartialComponent) {
    app.first_error = NX_ERROR_IO;
    EXPECT_CALL(hardware, Save())
        .WillOnce(testing::Return(nx_arch_irq_state_t{0U}));
    EXPECT_EQ(example_stop2_progress(&app), NX_ERROR_BUSY);
    EXPECT_EQ(app.phase, EXAMPLE_STOP2_JOIN);
    EXPECT_TRUE(app.mask_retained);
    joined();
    failed_finish(NX_ERROR_IO);
}
TEST_F(CompoundStop2, RecoveryDoesNotJoinUntilTheCompoundEffectsAreDrained) {
    transition.primary = NX_ERROR_TIMEOUT;
    transition.cleanup = NX_ERROR_BUSY;
    transition.remaining_effects = 4U;
    enter(1U);
    EXPECT_EQ(app.phase, EXAMPLE_STOP2_RECOVER);
    EXPECT_CALL(hardware, Recover(testing::_))
        .WillOnce(testing::Return(transition));
    EXPECT_EQ(example_stop2_progress(&app), NX_ERROR_BUSY);
    EXPECT_EQ(app.phase, EXAMPLE_STOP2_RECOVER);
    EXPECT_TRUE(app.mask_retained);
    transition.primary = NX_SUCCESS;
    transition.cleanup = NX_SUCCESS;
    transition.remaining_effects = 0U;
    EXPECT_CALL(hardware, Recover(testing::_))
        .WillOnce(testing::Return(transition));
    EXPECT_EQ(example_stop2_progress(&app), NX_ERROR_BUSY);
    EXPECT_EQ(app.phase, EXAMPLE_STOP2_JOIN);
    joined(NX_ERROR_IO);
    failed_finish(NX_ERROR_TIMEOUT, 1U);
}
} /* namespace */

extern "C" nx_arch_irq_state_t nx_arch_irq_save(void) {
    return boundary->Save();
}
extern "C" void nx_arch_irq_restore(nx_arch_irq_state_t saved) {
    boundary->Restore(saved);
}
extern "C" nx_stm32u575_stop2_result_t
example_stop2_application_enter(nx_clock_barrier_t owners) {
    return boundary->Enter(owners);
}
extern "C" example_stop2_join_result_t example_stop2_application_join(void) {
    return boundary->Join();
}
extern "C" nx_stm32u575_stop2_result_t
nx_stm32u575_stop2_recover(nx_clock_barrier_t owners, uint32_t polls) {
    EXPECT_EQ(polls, 10000U);
    return boundary->Recover(owners);
}
extern "C" nx_result_t nx_clock_domain_reopen(nx_clock_barrier_t owners) {
    return boundary->Reopen(owners);
}
extern "C" nx_stm32u575_stop2_result_t
nx_stm32u575_stop2_enter(nx_clock_barrier_t, uint32_t, uint32_t) {
    ADD_FAILURE() << "Compound selection must not call the WUT-only entry";
    nx_stm32u575_stop2_result_t refused{};
    refused.primary = NX_ERROR_UNSUPPORTED;
    return refused;
}
extern "C" nx_platform_start_result_t nx_platform_start(void) {
    ADD_FAILURE() << "No restart is admitted before the selected join";
    return {NX_ERROR_UNSUPPORTED, NX_SUCCESS, 0U};
}
extern "C" nx_result_t nx_platform_stop(void) {
    ADD_FAILURE() << "Fixture starts after RUN has already drained";
    return NX_ERROR_UNSUPPORTED;
}
extern "C" nx_result_t nx_clock_domain_acquire(nx_clock_domain_t*,
                                               nx_clock_lease_t*) {
    ADD_FAILURE() << "Fixture does not advance beyond reopened admission";
    return NX_ERROR_UNSUPPORTED;
}
extern "C" nx_result_t nx_clock_domain_close(nx_clock_domain_t*,
                                             nx_clock_barrier_t*) {
    ADD_FAILURE() << "Fixture starts at the closed boundary";
    return NX_ERROR_UNSUPPORTED;
}
extern "C" nx_result_t nx_clock_domain_release(nx_clock_domain_t*,
                                               nx_clock_lease_t*) {
    ADD_FAILURE() << "Fixture starts after the exact RUN lease release";
    return NX_ERROR_UNSUPPORTED;
}
extern "C" nx_result_t nx_clock_domain_quiescent(nx_clock_barrier_t) {
    ADD_FAILURE() << "Fixture starts after the exact closed barrier drained";
    return NX_ERROR_UNSUPPORTED;
}
