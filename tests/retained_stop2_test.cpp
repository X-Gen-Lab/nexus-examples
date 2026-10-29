/**
 * \file            retained_stop2_test.cpp
 * \brief           Product sequencing retains masks and exact RUN leases
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
class PlatformBoundary {
  public:
    MOCK_METHOD(nx_result_t, Close, (nx_clock_domain_t*, nx_clock_barrier_t*));
    MOCK_METHOD(nx_result_t, Stop, ());
    MOCK_METHOD(nx_result_t, Release, (nx_clock_domain_t*, nx_clock_lease_t*));
    MOCK_METHOD(nx_result_t, Quiescent, (nx_clock_barrier_t));
    MOCK_METHOD(nx_arch_irq_state_t, SaveMask, ());
    MOCK_METHOD(nx_stm32u575_stop2_result_t, Enter, (nx_clock_barrier_t));
    MOCK_METHOD(nx_stm32u575_stop2_result_t, Recover, (nx_clock_barrier_t));
    MOCK_METHOD(void, RestoreMask, (nx_arch_irq_state_t));
    MOCK_METHOD(nx_result_t, Reopen, (nx_clock_barrier_t));
    MOCK_METHOD(nx_result_t, Acquire, (nx_clock_domain_t*, nx_clock_lease_t*));
    MOCK_METHOD(nx_platform_start_result_t, Start, ());
};
PlatformBoundary* boundary;
class RetainedStop2 : public testing::Test {
  protected:
    testing::StrictMock<PlatformBoundary> hardware;
    example_stop2_application_t app{};
    unsigned domain_storage{};
    nx_stm32u575_stop2_result_t result{};
    void SetUp() override {
        boundary = &hardware;
        app.domain = reinterpret_cast<nx_clock_domain_t*>(&domain_storage);
        app.platform_lease.owner = app.domain;
        result.primary = NX_SUCCESS;
        result.cleanup = NX_SUCCESS;
    }
    void TearDown() override {
        boundary = nullptr;
    }
    void close_run() {
        EXPECT_CALL(hardware, Close(app.domain, &app.barrier))
            .WillOnce([this](nx_clock_domain_t*, nx_clock_barrier_t* barrier) {
                *barrier = {app.domain, 7U};
                return NX_SUCCESS;
            });
        EXPECT_EQ(example_stop2_progress(&app), NX_ERROR_BUSY);
        EXPECT_EQ(app.phase, EXAMPLE_STOP2_STOP);
    }
    void drain_run() {
        close_run();
        EXPECT_CALL(hardware, Stop()).WillOnce(testing::Return(NX_SUCCESS));
        EXPECT_EQ(example_stop2_progress(&app), NX_ERROR_BUSY);
        EXPECT_CALL(hardware, Release(app.domain, &app.platform_lease))
            .WillOnce(testing::Return(NX_SUCCESS));
        EXPECT_EQ(example_stop2_progress(&app), NX_ERROR_BUSY);
        EXPECT_CALL(hardware, Quiescent(testing::_))
            .WillOnce(testing::Return(NX_SUCCESS));
        EXPECT_EQ(example_stop2_progress(&app), NX_ERROR_BUSY);
    }
    void enter() {
        EXPECT_CALL(hardware, SaveMask())
            .WillOnce(testing::Return(nx_arch_irq_state_t{0U}));
        EXPECT_CALL(hardware, Enter(testing::_))
            .WillOnce(testing::Return(result));
        EXPECT_EQ(example_stop2_progress(&app), NX_ERROR_BUSY);
        EXPECT_TRUE(app.mask_retained);
    }
};
TEST_F(RetainedStop2, InvalidStateNeverTouchesTheHardware) {
    EXPECT_EQ(example_stop2_progress(nullptr), NX_ERROR_INVALID);
    auto* domain = app.domain;
    app.domain = nullptr;
    EXPECT_EQ(example_stop2_progress(&app), NX_ERROR_INVALID);
    app.domain = domain;
    app.phase = static_cast<example_stop2_phase_t>(15U);
    EXPECT_EQ(example_stop2_progress(&app), NX_ERROR_INVALID);
}
TEST_F(RetainedStop2, BusyOrFailedPlatformStopKeepsItsExactLeaseAndNoSleep) {
    close_run();
    EXPECT_CALL(hardware, Stop()).WillOnce(testing::Return(NX_ERROR_BUSY));
    EXPECT_EQ(example_stop2_progress(&app), NX_ERROR_BUSY);
    EXPECT_EQ(app.phase, EXAMPLE_STOP2_STOP);
    EXPECT_CALL(hardware, Stop()).WillOnce(testing::Return(NX_ERROR_IO));
    EXPECT_EQ(example_stop2_progress(&app), NX_ERROR_IO);
    EXPECT_EQ(app.platform_lease.owner, app.domain);
    EXPECT_EQ(app.first_error, NX_ERROR_IO);
    EXPECT_EQ(app.phase, EXAMPLE_STOP2_STOP);
}
TEST_F(RetainedStop2, FailedReleaseCannotQuiesceOrEnter) {
    close_run();
    EXPECT_CALL(hardware, Stop()).WillOnce(testing::Return(NX_SUCCESS));
    EXPECT_EQ(example_stop2_progress(&app), NX_ERROR_BUSY);
    EXPECT_CALL(hardware, Release(app.domain, &app.platform_lease))
        .WillOnce(testing::Return(NX_ERROR_STATE));
    EXPECT_EQ(example_stop2_progress(&app), NX_ERROR_STATE);
    EXPECT_EQ(app.phase, EXAMPLE_STOP2_RELEASE);
    EXPECT_EQ(app.platform_lease.owner, app.domain);
}
TEST_F(RetainedStop2,
       RetainedEffectsRetryRecoveryWithoutSecondWfiOrMaskRestore) {
    drain_run();
    result.primary = NX_ERROR_TIMEOUT;
    result.cleanup = NX_ERROR_IO;
    result.remaining_effects = 2U;
    enter();
    EXPECT_CALL(hardware, Recover(testing::_))
        .WillOnce(testing::Return(result));
    EXPECT_EQ(example_stop2_progress(&app), NX_ERROR_BUSY);
    EXPECT_TRUE(app.mask_retained);
    EXPECT_EQ(app.phase, EXAMPLE_STOP2_RECOVER);
    result.primary = NX_SUCCESS;
    result.cleanup = NX_SUCCESS;
    result.remaining_effects = 0U;
    EXPECT_CALL(hardware, Recover(testing::_))
        .WillOnce(testing::Return(result));
    EXPECT_EQ(example_stop2_progress(&app), NX_ERROR_BUSY);
    EXPECT_CALL(hardware,
                RestoreMask(testing::Field(&nx_arch_irq_state_t::value, 0U)));
    EXPECT_EQ(example_stop2_progress(&app), NX_ERROR_TIMEOUT);
    EXPECT_EQ(app.phase, EXAMPLE_STOP2_DONE);
    EXPECT_FALSE(app.mask_retained);
    EXPECT_FALSE(app.restarted);
}
TEST_F(RetainedStop2, RestartAcquiresANewLeaseAndStopsBeforeFinalRelease) {
    drain_run();
    enter();
    EXPECT_CALL(hardware,
                RestoreMask(testing::Field(&nx_arch_irq_state_t::value, 0U)));
    EXPECT_CALL(hardware, Reopen(testing::_))
        .WillOnce(testing::Return(NX_SUCCESS));
    EXPECT_EQ(example_stop2_progress(&app), NX_ERROR_BUSY);
    EXPECT_CALL(hardware, Acquire(app.domain, &app.platform_lease))
        .WillOnce(testing::Return(NX_SUCCESS));
    EXPECT_EQ(example_stop2_progress(&app), NX_ERROR_BUSY);
    EXPECT_CALL(hardware, Start())
        .WillOnce(testing::Return(
            nx_platform_start_result_t{NX_SUCCESS, NX_SUCCESS, 0U}));
    EXPECT_EQ(example_stop2_progress(&app), NX_ERROR_BUSY);
    EXPECT_TRUE(app.restarted);
    EXPECT_CALL(hardware, Stop()).WillOnce(testing::Return(NX_ERROR_BUSY));
    EXPECT_EQ(example_stop2_progress(&app), NX_ERROR_BUSY);
    EXPECT_EQ(app.phase, EXAMPLE_STOP2_FINAL_STOP);
    EXPECT_CALL(hardware, Stop()).WillOnce(testing::Return(NX_SUCCESS));
    EXPECT_EQ(example_stop2_progress(&app), NX_ERROR_BUSY);
    EXPECT_CALL(hardware, Release(app.domain, &app.platform_lease))
        .WillOnce(testing::Return(NX_SUCCESS));
    EXPECT_EQ(example_stop2_progress(&app), NX_SUCCESS);
    EXPECT_EQ(app.phase, EXAMPLE_STOP2_DONE);
    EXPECT_EQ(example_stop2_progress(&app), NX_SUCCESS);
}
TEST_F(RetainedStop2, InitialPartialStartDrainsButNeverEntersOrRestarts) {
    app.first_error = NX_ERROR_IO;
    drain_run();
    EXPECT_EQ(example_stop2_progress(&app), NX_ERROR_IO);
    EXPECT_EQ(app.phase, EXAMPLE_STOP2_DONE);
}
TEST_F(RetainedStop2, CloseFailureKeepsTheLeaseWithoutStartingStop) {
    EXPECT_CALL(hardware, Close(app.domain, &app.barrier))
        .WillOnce(testing::Return(NX_ERROR_CONTEXT));
    EXPECT_EQ(example_stop2_progress(&app), NX_ERROR_CONTEXT);
    EXPECT_EQ(app.phase, EXAMPLE_STOP2_CLOSE);
    EXPECT_EQ(app.platform_lease.owner, app.domain);
}
TEST_F(RetainedStop2, OtherEnrolledConsumersPreventSleepAndMasking) {
    app.phase = EXAMPLE_STOP2_QUIESCENT;
    app.barrier = {app.domain, 7U};
    EXPECT_CALL(hardware, Quiescent(testing::_))
        .WillOnce(testing::Return(NX_ERROR_BUSY));
    EXPECT_EQ(example_stop2_progress(&app), NX_ERROR_BUSY);
    EXPECT_FALSE(app.mask_retained);
    EXPECT_EQ(app.phase, EXAMPLE_STOP2_QUIESCENT);
}
TEST_F(RetainedStop2, AnAlreadyMaskedCallerIsNeverUnmaskedOnFailure) {
    drain_run();
    result.primary = NX_ERROR_IO;
    EXPECT_CALL(hardware, SaveMask())
        .WillOnce(testing::Return(nx_arch_irq_state_t{1U}));
    EXPECT_CALL(hardware, Enter(testing::_)).WillOnce(testing::Return(result));
    EXPECT_EQ(example_stop2_progress(&app), NX_ERROR_BUSY);
    EXPECT_CALL(hardware,
                RestoreMask(testing::Field(&nx_arch_irq_state_t::value, 1U)));
    EXPECT_EQ(example_stop2_progress(&app), NX_ERROR_IO);
    EXPECT_EQ(app.phase, EXAMPLE_STOP2_DONE);
    EXPECT_FALSE(app.restarted);
}
TEST_F(RetainedStop2, PartialRestartStillDrainsBeforeItsLeaseCanRelease) {
    app.phase = EXAMPLE_STOP2_RESTART;
    EXPECT_CALL(hardware, Start())
        .WillOnce(testing::Return(
            nx_platform_start_result_t{NX_ERROR_IO, NX_ERROR_BUSY, 1U}));
    EXPECT_EQ(example_stop2_progress(&app), NX_ERROR_BUSY);
    EXPECT_FALSE(app.restarted);
    EXPECT_CALL(hardware, Stop()).WillOnce(testing::Return(NX_ERROR_BUSY));
    EXPECT_EQ(example_stop2_progress(&app), NX_ERROR_BUSY);
    EXPECT_EQ(app.phase, EXAMPLE_STOP2_FINAL_STOP);
    EXPECT_CALL(hardware, Stop()).WillOnce(testing::Return(NX_SUCCESS));
    EXPECT_EQ(example_stop2_progress(&app), NX_ERROR_BUSY);
    EXPECT_CALL(hardware, Release(app.domain, &app.platform_lease))
        .WillOnce(testing::Return(NX_SUCCESS));
    EXPECT_EQ(example_stop2_progress(&app), NX_ERROR_IO);
    EXPECT_EQ(app.phase, EXAMPLE_STOP2_DONE);
}
TEST_F(RetainedStop2, FinalReleaseFailureRetainsTheExactRestartLease) {
    app.phase = EXAMPLE_STOP2_FINAL_RELEASE;
    EXPECT_CALL(hardware, Release(app.domain, &app.platform_lease))
        .WillOnce(testing::Return(NX_ERROR_STATE));
    EXPECT_EQ(example_stop2_progress(&app), NX_ERROR_STATE);
    EXPECT_EQ(app.phase, EXAMPLE_STOP2_FINAL_RELEASE);
    EXPECT_EQ(app.platform_lease.owner, app.domain);
    EXPECT_EQ(app.first_error, NX_ERROR_STATE);
}
TEST_F(RetainedStop2, BusyRestartIsARejectedStartRatherThanSuccessfulProgress) {
    app.phase = EXAMPLE_STOP2_RESTART;
    EXPECT_CALL(hardware, Start())
        .WillOnce(testing::Return(
            nx_platform_start_result_t{NX_ERROR_BUSY, NX_SUCCESS, 0U}));
    EXPECT_EQ(example_stop2_progress(&app), NX_ERROR_BUSY);
    EXPECT_FALSE(app.restarted);
    EXPECT_EQ(app.first_error, NX_ERROR_BUSY);
    EXPECT_CALL(hardware, Stop()).WillOnce(testing::Return(NX_SUCCESS));
    EXPECT_EQ(example_stop2_progress(&app), NX_ERROR_BUSY);
    EXPECT_CALL(hardware, Release(app.domain, &app.platform_lease))
        .WillOnce(testing::Return(NX_SUCCESS));
    EXPECT_EQ(example_stop2_progress(&app), NX_ERROR_BUSY);
    EXPECT_EQ(app.phase, EXAMPLE_STOP2_DONE);
}
} /* namespace */

extern "C" nx_result_t nx_clock_domain_close(nx_clock_domain_t* domain,
                                             nx_clock_barrier_t* barrier) {
    return boundary->Close(domain, barrier);
}
extern "C" nx_result_t nx_platform_stop(void) {
    return boundary->Stop();
}
extern "C" nx_result_t nx_clock_domain_release(nx_clock_domain_t* domain,
                                               nx_clock_lease_t* lease) {
    return boundary->Release(domain, lease);
}
extern "C" nx_result_t nx_clock_domain_quiescent(nx_clock_barrier_t barrier) {
    return boundary->Quiescent(barrier);
}
extern "C" nx_arch_irq_state_t nx_arch_irq_save(void) {
    return boundary->SaveMask();
}
extern "C" nx_stm32u575_stop2_result_t
nx_stm32u575_stop2_enter(nx_clock_barrier_t barrier, uint32_t ticks,
                         uint32_t polls) {
    EXPECT_EQ(ticks, 2048U);
    EXPECT_EQ(polls, 10000U);
    return boundary->Enter(barrier);
}
extern "C" nx_stm32u575_stop2_result_t
nx_stm32u575_stop2_recover(nx_clock_barrier_t barrier, uint32_t polls) {
    EXPECT_EQ(polls, 10000U);
    return boundary->Recover(barrier);
}
extern "C" void nx_arch_irq_restore(nx_arch_irq_state_t saved) {
    boundary->RestoreMask(saved);
}
extern "C" nx_result_t nx_clock_domain_reopen(nx_clock_barrier_t barrier) {
    return boundary->Reopen(barrier);
}
extern "C" nx_result_t nx_clock_domain_acquire(nx_clock_domain_t* domain,
                                               nx_clock_lease_t* lease) {
    return boundary->Acquire(domain, lease);
}
extern "C" nx_platform_start_result_t nx_platform_start(void) {
    return boundary->Start();
}
