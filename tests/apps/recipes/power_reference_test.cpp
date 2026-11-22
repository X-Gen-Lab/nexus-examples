/**
 * \file            power_reference_test.cpp
 * \brief           Baremetal STOP composition orders real public boundaries
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-22
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "baremetal_stop_reference.h"
#include "nexus_bindings.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace {
/** \brief External Platform/System boundaries, not the application algorithm.
 */
class Boundaries {
  public:
    MOCK_METHOD(nx_result_t, Suspend, ());
    MOCK_METHOD(nx_result_t, Enter,
                (nx_clock_barrier_t, const nx_stm32_power_options_t*,
                 nx_stm32_stop_receipt_t*));
    MOCK_METHOD(nx_result_t, Reopen, (nx_clock_barrier_t));
    MOCK_METHOD(nx_platform_start_result_t, Resume, ());
};
Boundaries* boundaries;
class BaremetalStop : public testing::Test {
  protected:
    testing::StrictMock<Boundaries> calls;
    max_align_t domain_storage{};
    nx_clock_barrier_t barrier{
        reinterpret_cast<nx_clock_domain_t*>(&domain_storage), 9U};
    nx_stm32_power_options_t options{};
    example_stop_report_t report{};
    nx_platform_start_result_t restored{NX_SUCCESS, NX_SUCCESS, 0U};
    void SetUp() override {
        boundaries = &calls;
        options.poll_limit = 16U;
    }
    void TearDown() override {
        boundaries = nullptr;
    }
};
TEST_F(BaremetalStop, DrainsBeforeStopAndReopensBeforeControllerResume) {
    testing::InSequence order;
    EXPECT_CALL(calls, Suspend()).WillOnce(testing::Return(NX_SUCCESS));
    EXPECT_CALL(calls, Enter(testing::_, &options, testing::_))
        .WillOnce(testing::Invoke([](nx_clock_barrier_t,
                                     const nx_stm32_power_options_t*,
                                     nx_stm32_stop_receipt_t* receipt) {
            *receipt = {41U, 42U, 1000U, 5U, true};
            return NX_SUCCESS;
        }));
    EXPECT_CALL(calls, Reopen(testing::_))
        .WillOnce(testing::Return(NX_SUCCESS));
    EXPECT_CALL(calls, Resume()).WillOnce(testing::Return(restored));
    EXPECT_EQ(example_baremetal_stop(barrier, &options, &report), NX_SUCCESS);
    EXPECT_TRUE(report.power_attempted);
    EXPECT_TRUE(report.reopened);
    EXPECT_TRUE(report.resumed);
    EXPECT_EQ(report.receipt.old_epoch, 41U);
    EXPECT_EQ(report.receipt.new_epoch, 42U);
}
TEST_F(BaremetalStop, IncompleteSuspendKeepsPowerEntryClosed) {
    EXPECT_CALL(calls, Suspend()).WillOnce(testing::Return(NX_ERROR_BUSY));
    EXPECT_EQ(example_baremetal_stop(barrier, &options, &report),
              NX_ERROR_BUSY);
    EXPECT_FALSE(report.power_attempted);
    EXPECT_FALSE(report.reopened);
    EXPECT_FALSE(report.resumed);
}
TEST_F(BaremetalStop, RejectedStopPreservesPrimaryErrorWhileRestoringOwners) {
    testing::InSequence order;
    EXPECT_CALL(calls, Suspend()).WillOnce(testing::Return(NX_SUCCESS));
    EXPECT_CALL(calls, Enter(testing::_, &options, testing::_))
        .WillOnce(testing::Return(NX_ERROR_UNSUPPORTED));
    EXPECT_CALL(calls, Reopen(testing::_))
        .WillOnce(testing::Return(NX_SUCCESS));
    EXPECT_CALL(calls, Resume()).WillOnce(testing::Return(restored));
    EXPECT_EQ(example_baremetal_stop(barrier, &options, &report),
              NX_ERROR_UNSUPPORTED);
    EXPECT_EQ(report.power, NX_ERROR_UNSUPPORTED);
    EXPECT_TRUE(report.resumed);
}
TEST_F(BaremetalStop,
       ReopenFailureRetainsSuspensionAndCannotResumeControllers) {
    testing::InSequence order;
    EXPECT_CALL(calls, Suspend()).WillOnce(testing::Return(NX_SUCCESS));
    EXPECT_CALL(calls, Enter(testing::_, &options, testing::_))
        .WillOnce(testing::Return(NX_SUCCESS));
    EXPECT_CALL(calls, Reopen(testing::_))
        .WillOnce(testing::Return(NX_ERROR_BUSY));
    EXPECT_EQ(example_baremetal_stop(barrier, &options, &report),
              NX_ERROR_BUSY);
    EXPECT_FALSE(report.reopened);
    EXPECT_FALSE(report.resumed);
    EXPECT_EQ(report.reopen, NX_ERROR_BUSY);
}
TEST_F(BaremetalStop, ResumeFailurePreservesItsCleanupAndRemainingEffects) {
    testing::InSequence order;
    EXPECT_CALL(calls, Suspend()).WillOnce(testing::Return(NX_SUCCESS));
    EXPECT_CALL(calls, Enter(testing::_, &options, testing::_))
        .WillOnce(testing::Return(NX_SUCCESS));
    EXPECT_CALL(calls, Reopen(testing::_))
        .WillOnce(testing::Return(NX_SUCCESS));
    restored = {NX_ERROR_IO, NX_ERROR_BUSY, 7U};
    EXPECT_CALL(calls, Resume()).WillOnce(testing::Return(restored));
    EXPECT_EQ(example_baremetal_stop(barrier, &options, &report), NX_ERROR_IO);
    EXPECT_FALSE(report.resumed);
    EXPECT_EQ(report.resume.cleanup, NX_ERROR_BUSY);
    EXPECT_EQ(report.resume.remaining_effects, 7U);
}
TEST_F(BaremetalStop, StandbyIsRejectedWithoutTouchingThePlatform) {
    options.standby = true;
    EXPECT_EQ(example_baremetal_stop(barrier, &options, &report),
              NX_ERROR_INVALID);
    EXPECT_FALSE(report.power_attempted);
}
} /* namespace */

extern "C" nx_result_t nx_platform_suspend(void) {
    return boundaries->Suspend();
}
extern "C" nx_platform_start_result_t nx_platform_resume(void) {
    return boundaries->Resume();
}
extern "C" nx_result_t
nx_stm32_power_enter(nx_clock_barrier_t barrier,
                     const nx_stm32_power_options_t* options,
                     nx_stm32_stop_receipt_t* receipt) {
    return boundaries->Enter(barrier, options, receipt);
}
extern "C" nx_result_t nx_clock_domain_reopen(nx_clock_barrier_t barrier) {
    return boundaries->Reopen(barrier);
}
