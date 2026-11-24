/**
 * \file            lift_axis_test.cpp
 * \brief           Explicit mechanical handover and finite homing contracts
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-24
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "lift_axis.h"
#include "native/model.h"
#include <gtest/gtest.h>

namespace {
class Lift : public testing::Test {
  protected:
    nx_axis_t axis = {};
    example_lift_t lift = {};
    example_lift_feedback_t feedback = {};
    example_lift_output_t output = {};
    nx_time_us_t now = 1000;
    void SetUp() override {
        nx_axis_config_t axis_config = {1, 1, 2000, 2000,
                                        NX_AXIS_HOLD_ZERO_VELOCITY};
        nx_axis_parameters_t parameters = {};
        parameters.revision = 1;
        parameters.velocity_kp = 2;
        parameters.velocity_ki = 1;
        parameters.tracking_rate_per_s = 1;
        parameters.integral_limit_a = 5;
        parameters.position_kp = 2;
        parameters.current_limit_a = 5;
        parameters.velocity_limit_rad_s = 1;
        parameters.acceleration_rad_s2 = 10;
        parameters.minimum_position_rad = -100;
        parameters.maximum_position_rad = 100;
        parameters.position_tolerance_rad = 0.01F;
        parameters.velocity_tolerance_rad_s = 0.01F;
        parameters.settling_us = 2000;
        parameters.stop_feedforward_a = 1;
        ASSERT_EQ(nx_axis_initialize(&axis, &axis_config, &parameters),
                  NX_SUCCESS);
        example_lift_config_t config = {};
        config.maximum_feedback_age_us = 2000;
        config.handover_timeout_us = 50000;
        config.homing_timeout_us = 500000;
        config.homing_phase_timeout_us = 100000;
        config.homing_maximum_phase_distance_rad = 0.5F;
        config.search_velocity_rad_s = 0.1F;
        config.approach_velocity_rad_s = 0.02F;
        config.home_coordinate_rad = 0;
        config.support_feedforward_a = 1;
        config.on_feedback_loss = EXAMPLE_LIFT_BRAKE_KEEP;
        config.on_power_loss = EXAMPLE_LIFT_BRAKE_KEEP;
        ASSERT_EQ(example_lift_initialize(&lift, &axis, &config), NX_SUCCESS);
        feedback.motion = {1, 1, 1, 0, now, 0.2F, 0, true};
        feedback.brake = EXAMPLE_LIFT_BRAKE_ENGAGED;
        feedback.brake_valid = true;
        feedback.brake_sampled_at = now;
        feedback.support_sampled_at = now;
        feedback.switches_sampled_at = now;
        feedback.switches_valid = true;
        feedback.power_available = true;
        feedback.drive_ready = true;
    }
    nx_result_t step() {
        const auto before = now;
        now += 1000;
        ++feedback.motion.sequence;
        feedback.motion.interval_start = before;
        feedback.motion.sampled_at = now;
        feedback.brake_sampled_at = now;
        feedback.support_sampled_at = now;
        feedback.switches_sampled_at = now;
        return example_lift_step(&lift, &feedback, now, 0.001F, &output);
    }
    void released() {
        ASSERT_EQ(example_lift_start(&lift, &feedback, now), NX_SUCCESS);
        feedback.support_confirmed = true;
        ASSERT_EQ(step(), NX_SUCCESS);
        EXPECT_EQ(output.brake_request, EXAMPLE_LIFT_BRAKE_RELEASE);
        feedback.brake = EXAMPLE_LIFT_BRAKE_RELEASED;
        ASSERT_EQ(step(), NX_SUCCESS);
        EXPECT_EQ(output.state, EXAMPLE_LIFT_ACTIVE);
    }
};
TEST_F(Lift, StartupKeepsBrakeUntilIndependentMotorSupportIsConfirmed) {
    ASSERT_EQ(example_lift_start(&lift, &feedback, now), NX_SUCCESS);
    ASSERT_EQ(step(), NX_SUCCESS);
    EXPECT_EQ(output.brake_request, EXAMPLE_LIFT_BRAKE_CLOSE);
    EXPECT_TRUE(output.motor_permit);
    EXPECT_GT(output.axis.current_a, 0.9F);
    EXPECT_EQ(output.state, EXAMPLE_LIFT_BUILD_SUPPORT);
    feedback.support_confirmed = true;
    ASSERT_EQ(step(), NX_SUCCESS);
    EXPECT_EQ(output.brake_request, EXAMPLE_LIFT_BRAKE_RELEASE);
    EXPECT_EQ(output.state, EXAMPLE_LIFT_RELEASING);
}
TEST_F(Lift, ElapsedTimeNeverSubstitutesForReleasedBrakeFeedback) {
    ASSERT_EQ(example_lift_start(&lift, &feedback, now), NX_SUCCESS);
    feedback.support_confirmed = true;
    ASSERT_EQ(step(), NX_SUCCESS);
    for (unsigned i = 0; i < 10; ++i)
        ASSERT_EQ(step(), NX_SUCCESS);
    EXPECT_EQ(output.state, EXAMPLE_LIFT_RELEASING);
    EXPECT_EQ(example_lift_move(&lift, 0.1F, now, now + 10000, now),
              NX_ERROR_STATE);
}
TEST_F(Lift, ControlledStopKeepsSupportUntilClosedBrakeIsConfirmed) {
    released();
    ASSERT_EQ(example_lift_move(&lift, 0.1F, now, now + 20000, now),
              NX_SUCCESS);
    ASSERT_EQ(step(), NX_SUCCESS);
    ASSERT_EQ(example_lift_request_stop(&lift, now), NX_SUCCESS);
    for (unsigned i = 0; i < 20 && output.state != EXAMPLE_LIFT_ENGAGING; ++i)
        ASSERT_EQ(step(), NX_SUCCESS);
    EXPECT_EQ(output.state, EXAMPLE_LIFT_ENGAGING);
    EXPECT_TRUE(output.motor_permit);
    EXPECT_GT(output.axis.current_a, 0.9F);
    EXPECT_EQ(output.brake_request, EXAMPLE_LIFT_BRAKE_CLOSE);
    ASSERT_EQ(step(), NX_SUCCESS);
    EXPECT_TRUE(output.motor_permit);
    feedback.brake = EXAMPLE_LIFT_BRAKE_ENGAGED;
    ASSERT_EQ(step(), NX_SUCCESS);
    EXPECT_EQ(output.state, EXAMPLE_LIFT_SECURED);
    EXPECT_FALSE(output.motor_permit);
    EXPECT_EQ(axis.state, NX_AXIS_DISABLED);
}
TEST_F(Lift, FeedbackFailureReportsUnconfirmedSupportWithoutDynamicBraking) {
    released();
    feedback.motion.valid = false;
    EXPECT_EQ(step(), NX_ERROR_STATE);
    EXPECT_EQ(output.state, EXAMPLE_LIFT_FAULT);
    EXPECT_EQ(output.brake_request, EXAMPLE_LIFT_BRAKE_KEEP);
    EXPECT_FALSE(output.motor_permit);
    EXPECT_TRUE(output.support_unconfirmed);
    EXPECT_EQ(example_lift_reset(&lift, &feedback, now), NX_ERROR_STATE);
}
TEST_F(Lift, PowerFailureDoesNotClaimTorqueOrMechanicalHolding) {
    released();
    feedback.power_available = false;
    feedback.support_confirmed = false;
    EXPECT_EQ(step(), NX_ERROR_STATE);
    EXPECT_FALSE(output.motor_permit);
    EXPECT_TRUE(output.support_unconfirmed);
    EXPECT_FALSE(output.reference_valid);
}
TEST_F(Lift, HomingSearchHasFiniteDistanceAndRetainsFault) {
    released();
    ASSERT_EQ(example_lift_home(&lift, &feedback, now), NX_SUCCESS);
    feedback.motion.position_rad -= 0.6F;
    EXPECT_EQ(step(), NX_ERROR_STATE);
    EXPECT_EQ(output.state, EXAMPLE_LIFT_FAULT);
    EXPECT_FALSE(output.reference_valid);
}
TEST_F(Lift, HomingUsesSearchBackoffApproachAndOnlyThenBuildsReference) {
    released();
    ASSERT_EQ(example_lift_home(&lift, &feedback, now), NX_SUCCESS);
    ASSERT_EQ(step(), NX_SUCCESS);
    EXPECT_EQ(output.homing, EXAMPLE_LIFT_HOME_SEARCH);
    feedback.home_switch = true;
    feedback.motion.position_rad = 0.02F;
    ASSERT_EQ(step(), NX_SUCCESS);
    EXPECT_EQ(output.homing, EXAMPLE_LIFT_HOME_BACKOFF);
    feedback.home_switch = false;
    feedback.motion.position_rad = 0.03F;
    ASSERT_EQ(step(), NX_SUCCESS);
    EXPECT_EQ(output.homing, EXAMPLE_LIFT_HOME_APPROACH);
    feedback.home_switch = true;
    feedback.motion.position_rad = 0.021F;
    ASSERT_EQ(step(), NX_SUCCESS);
    EXPECT_FALSE(output.reference_valid);
    for (unsigned i = 0; i < 20 && output.state != EXAMPLE_LIFT_ENGAGING; ++i)
        ASSERT_EQ(step(), NX_SUCCESS);
    EXPECT_FALSE(output.reference_valid);
    feedback.brake = EXAMPLE_LIFT_BRAKE_ENGAGED;
    ASSERT_EQ(step(), NX_SUCCESS);
    EXPECT_EQ(output.state, EXAMPLE_LIFT_SECURED);
    EXPECT_TRUE(output.reference_valid);
    EXPECT_NEAR(output.reference_offset_rad, -0.021F, 1e-6F);
}
TEST_F(Lift, RecoveryRequiresClosedBrakeAndActualDriveWithdrawal) {
    released();
    feedback.power_available = false;
    feedback.support_confirmed = false;
    EXPECT_EQ(step(), NX_ERROR_STATE);
    feedback.power_available = true;
    feedback.brake = EXAMPLE_LIFT_BRAKE_ENGAGED;
    EXPECT_EQ(example_lift_reset(&lift, &feedback, now), NX_ERROR_STATE);
    feedback.drive_ready = false;
    ASSERT_EQ(example_lift_reset(&lift, &feedback, now), NX_SUCCESS);
    EXPECT_EQ(lift.state, EXAMPLE_LIFT_SECURED);
    EXPECT_EQ(axis.state, NX_AXIS_DISABLED);
    EXPECT_EQ(example_lift_move(&lift, 0.1F, now, now + 10000, now),
              NX_ERROR_STATE);
}
TEST_F(Lift, IndependentMechanicalModelClosesTheSupportAndBrakeChain) {
    example_lift_model_t model = {};
    const example_lift_model_config_t config = {
        1,    0.01F, 1, 0.02F,  0.002F, 1e-6F, 0.08F,
        5000, 0.2F,  0, 0.002F, 1,      -0.1F};
    ASSERT_EQ(example_lift_model_initialize(&model, &config, 1, 1, now),
              NX_SUCCESS);
    now += 1000;
    ASSERT_EQ(example_lift_model_advance(&model, &lift.output, now, 0.001F,
                                         &feedback),
              NX_SUCCESS);
    ASSERT_EQ(example_lift_start(&lift, &feedback, now), NX_SUCCESS);
    output = lift.output;
    for (unsigned i = 0; i < 30 && lift.state != EXAMPLE_LIFT_ACTIVE; ++i) {
        now += 1000;
        ASSERT_EQ(
            example_lift_model_advance(&model, &output, now, 0.001F, &feedback),
            NX_SUCCESS);
        ASSERT_EQ(example_lift_step(&lift, &feedback, now, 0.001F, &output),
                  NX_SUCCESS);
    }
    ASSERT_EQ(lift.state, EXAMPLE_LIFT_ACTIVE);
    ASSERT_EQ(example_lift_move(&lift, 0.05F, now, now + 30000, now),
              NX_SUCCESS);
    for (unsigned i = 0; i < 10; ++i) {
        now += 1000;
        ASSERT_EQ(
            example_lift_model_advance(&model, &output, now, 0.001F, &feedback),
            NX_SUCCESS);
        ASSERT_EQ(example_lift_step(&lift, &feedback, now, 0.001F, &output),
                  NX_SUCCESS);
    }
    ASSERT_EQ(example_lift_request_stop(&lift, now), NX_SUCCESS);
    for (unsigned i = 0; i < 49 && lift.state != EXAMPLE_LIFT_SECURED; ++i) {
        now += 1000;
        ASSERT_EQ(
            example_lift_model_advance(&model, &output, now, 0.001F, &feedback),
            NX_SUCCESS);
        ASSERT_EQ(example_lift_step(&lift, &feedback, now, 0.001F, &output),
                  NX_SUCCESS);
        if (feedback.brake != EXAMPLE_LIFT_BRAKE_ENGAGED) {
            EXPECT_TRUE(output.motor_permit);
        }
    }
    EXPECT_EQ(lift.state, EXAMPLE_LIFT_SECURED);
    EXPECT_FALSE(output.motor_permit);
    EXPECT_EQ(feedback.brake, EXAMPLE_LIFT_BRAKE_ENGAGED);
}
TEST_F(Lift, UnknownBrakeDoesNotBecomeConfirmedFromMotorSupport) {
    ASSERT_EQ(example_lift_start(&lift, &feedback, now), NX_SUCCESS);
    feedback.support_confirmed = true;
    feedback.brake = EXAMPLE_LIFT_BRAKE_UNKNOWN;
    EXPECT_EQ(step(), NX_ERROR_STATE);
    EXPECT_TRUE(output.support_unconfirmed);
    EXPECT_FALSE(output.motor_permit);
}
TEST_F(Lift, RejectsOutputAliasingTheLiveOwnerBeforeMutation) {
    const auto before = lift;
    auto* alias = reinterpret_cast<example_lift_output_t*>(&lift);
    EXPECT_EQ(example_lift_step(&lift, &feedback, now + 1000, 0.001F, alias),
              NX_ERROR_INVALID);
    EXPECT_EQ(lift.axis, before.axis);
    EXPECT_EQ(lift.state, before.state);
}
TEST_F(Lift, RepeatedExecutorTimeFaultsEvenWhileSecured) {
    ASSERT_EQ(step(), NX_SUCCESS);
    EXPECT_EQ(example_lift_step(&lift, &feedback, now, 0.001F, &output),
              NX_ERROR_STATE);
    EXPECT_EQ(output.state, EXAMPLE_LIFT_FAULT);
}
TEST_F(Lift, StopCannotCreateLocalBudgetFromTimeBeforeLastStep) {
    released();
    EXPECT_EQ(example_lift_request_stop(&lift, now - 1), NX_ERROR_INVALID);
    EXPECT_EQ(lift.state, EXAMPLE_LIFT_ACTIVE);
}
TEST_F(Lift, HomingPhaseExpiryNeverRenewsSearchAuthority) {
    released();
    ASSERT_EQ(example_lift_home(&lift, &feedback, now), NX_SUCCESS);
    for (unsigned i = 0; i < 99; ++i) {
        ASSERT_EQ(step(), NX_SUCCESS);
    }
    EXPECT_EQ(step(), NX_ERROR_STATE);
    EXPECT_EQ(lift.fault, NX_ERROR_TIMEOUT);
    EXPECT_FALSE(output.reference_valid);
    EXPECT_FALSE(output.motor_permit);
}
TEST_F(Lift, MoveCannotRenewAuthorityUsingExecutorTimeBeforeLastStep) {
    released();
    EXPECT_EQ(example_lift_move(&lift, 0.1F, now - 1, now + 10000, now - 1),
              NX_ERROR_STATE);
    EXPECT_FALSE(axis.command_pending && axis.pending_command.sequence == 2);
}
TEST_F(Lift, RebindingDisabledAxisPreservesItsCommandSequenceIdentity) {
    ASSERT_EQ(nx_axis_enable(&axis, &feedback.motion, now), NX_SUCCESS);
    const nx_axis_command_t command = {NX_AXIS_VELOCITY, 1, 1, 7, now,
                                       now + 10000,      0, 0, 1};
    ASSERT_EQ(nx_axis_accept(&axis, &command, now), NX_SUCCESS);
    ASSERT_EQ(nx_axis_disable(&axis), NX_SUCCESS);
    const auto config = lift.config;
    ASSERT_EQ(example_lift_initialize(&lift, &axis, &config), NX_SUCCESS);
    EXPECT_EQ(example_lift_start(&lift, &feedback, now), NX_SUCCESS);
    EXPECT_EQ(axis.accepted_sequence, 8U);
}
TEST_F(Lift,
       UnexpectedBrakeChangeDuringControlledStopFaultsInsteadOfDrivingIt) {
    released();
    ASSERT_EQ(example_lift_request_stop(&lift, now), NX_SUCCESS);
    feedback.brake = EXAMPLE_LIFT_BRAKE_ENGAGED;
    feedback.motion.velocity_rad_s = 0.1F;
    EXPECT_EQ(step(), NX_ERROR_STATE);
    EXPECT_EQ(output.state, EXAMPLE_LIFT_FAULT);
    EXPECT_FALSE(output.motor_permit);
    EXPECT_EQ(output.brake_request, EXAMPLE_LIFT_BRAKE_KEEP);
}
} /* namespace */
