/**
 * \file            provider_test.cpp
 * \brief           Actual F407 provider and public motor application
 * integration \author          Nexus Team \version         1.0.0 \date
 * 2026-11-25 \copyright       Copyright (c) 2026 Nexus Team
 */
#include "control.h"
#include "provider_model.h"
#include <gtest/gtest.h>

namespace {
class MotorProvider : public testing::Test {
  protected:
    example_test::ProviderModel hardware;
    example_motor_drive_t drive = {};
    void SetUp() override {
        ASSERT_EQ(
            nx_stm32_power_stage_prepare(&hardware.owner, &hardware.config),
            NX_SUCCESS);
        auto config = example_motor_drive_reference();
        config.commissioned = true;
        config.iq_a = 0.5F;
        ASSERT_EQ(
            example_motor_drive_initialize(&drive, &hardware.port, &config),
            NX_SUCCESS);
        ASSERT_EQ(example_motor_drive_start(&drive, hardware.now), NX_SUCCESS);
        hardware.now = 4;
        EXPECT_EQ(example_motor_drive_step(&drive, hardware.now),
                  NX_ERROR_BUSY);
        ASSERT_TRUE(hardware.owner.running);
    }
    void TearDown() override {
        hardware.refused = nullptr;
        if (drive.borrowed) {
            (void)example_motor_drive_stop(&drive);
        }
        EXPECT_EQ(nx_stm32_power_stage_close(&hardware.owner), NX_SUCCESS);
    }
};

TEST_F(MotorProvider,
       RealCaptureAlgorithmsAndCompleteSubmissionReachSettlement) {
    for (unsigned i = 0; i < 6; ++i) {
        hardware.capture();
        ASSERT_EQ(hardware.owner.completed_cycles, 1U + i)
            << "provider fault=" << hardware.owner.fault
            << " motor fault=" << drive.context.fast.motor.fault;
        EXPECT_EQ(example_motor_drive_step(&drive, hardware.now),
                  NX_ERROR_BUSY);
    }
    EXPECT_TRUE(drive.have_feedback);
    EXPECT_GT(drive.reference_generation, 2U);
    EXPECT_GT(drive.context.last_plan.planned_voltage.q, 0);
    EXPECT_FALSE(drive.context.fast.motor.track_pending);
    EXPECT_EQ(hardware.timer.CCR1, hardware.owner.pwm.duties[0]);
    EXPECT_EQ(hardware.timer.CCR2, hardware.owner.pwm.duties[1]);
    EXPECT_EQ(hardware.timer.CCR3, hardware.owner.pwm.duties[2]);
    EXPECT_EQ(hardware.timer.CCR4, hardware.owner.staged_trigger_ticks);
    EXPECT_EQ(example_motor_drive_stop(&drive), NX_ERROR_CANCELLED);
    EXPECT_FALSE(drive.borrowed);
    EXPECT_EQ(nx_request_state(&drive.session.base), NX_REQUEST_SETTLED);
    EXPECT_EQ(drive.session.base.result, NX_ERROR_CANCELLED);
    EXPECT_EQ(hardware.timer.BDTR & TIM_BDTR_MOE, 0U);
    EXPECT_EQ(hardware.enabled[ADC_IRQn], 0U);
}

TEST_F(MotorProvider,
       ActualAdcFaultInhibitsThenTaskWithdrawsTheOriginalSession) {
    hardware.capture();
    ASSERT_EQ(hardware.owner.completed_cycles, 1U);
    EXPECT_EQ(example_motor_drive_step(&drive, hardware.now), NX_ERROR_BUSY);
    hardware.phase_code[0] = 4095;
    hardware.capture();
    EXPECT_EQ(hardware.timer.BDTR & TIM_BDTR_MOE, 0U);
    EXPECT_TRUE(drive.borrowed);
    EXPECT_NE(nx_request_state(&drive.session.base), NX_REQUEST_SETTLED);
    EXPECT_NE(example_motor_drive_step(&drive, hardware.now), NX_ERROR_BUSY);
    EXPECT_FALSE(drive.borrowed);
    EXPECT_EQ(nx_request_state(&drive.session.base), NX_REQUEST_SETTLED);
    EXPECT_NE(drive.primary, NX_SUCCESS);
}

TEST_F(MotorProvider,
       RefusedOutputWithdrawalRetainsTheActualContextUntilRetry) {
    hardware.capture();
    ASSERT_EQ(hardware.owner.completed_cycles, 1U);
    hardware.refused = &hardware.timer.BDTR;
    EXPECT_EQ(example_motor_drive_stop(&drive), NX_ERROR_BUSY);
    EXPECT_TRUE(drive.borrowed);
    EXPECT_EQ(nx_request_state(&drive.session.base), NX_REQUEST_QUARANTINED);
    EXPECT_EQ(hardware.owner.active, &drive.session);
    EXPECT_EQ(drive.session.context, &drive.context);
    hardware.refused = nullptr;
    EXPECT_EQ(example_motor_drive_step(&drive, hardware.now),
              NX_ERROR_CANCELLED);
    EXPECT_FALSE(drive.borrowed);
    EXPECT_EQ(hardware.owner.active, nullptr);
    EXPECT_EQ(nx_request_state(&drive.session.base), NX_REQUEST_SETTLED);
    EXPECT_EQ(hardware.timer.BDTR & TIM_BDTR_MOE, 0U);
}
} /* namespace */
