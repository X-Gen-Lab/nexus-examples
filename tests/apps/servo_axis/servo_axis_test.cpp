/**
 * \file            servo_axis_test.cpp
 * \brief           Actual cascaded servo control and independent loaded DC
 * plant \author          Nexus Team \version         1.0.0 \date 2026-11-24
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "servo_sil.h"
#include <cmath>
#include <cstring>
#include <gtest/gtest.h>

namespace {
TEST(ServoAxis, LoadedPositionUsesRealCascadeAndMeasuredSettling) {
    const auto experiment = example_servo_reference();
    example_servo_report_t report{};
    ASSERT_EQ(example_servo_run(&experiment, &report), NX_SUCCESS);
    EXPECT_TRUE(report.data_complete);
    EXPECT_TRUE(report.tracking_passed);
    EXPECT_TRUE(report.target_reached);
    EXPECT_EQ(report.samples, 1000U);
    EXPECT_EQ(report.parameter_revision, 2U);
    EXPECT_GT(report.trajectory_complete_at, report.adopted_at);
    EXPECT_GE(report.measured_reached_at, report.trajectory_complete_at);
    EXPECT_NEAR(report.measured_position_rad, 1.0F, 0.01F);
    EXPECT_NEAR(report.truth_position_rad, 1.0, 0.01);
    EXPECT_NEAR(report.truth_velocity_rad_s, 0.0, 0.02);
    EXPECT_NEAR(report.truth_current_a, 0.3, 0.03);
}

TEST(ServoAxis, VelocityModeClosesTheInnerCurrentLoopUnderLoad) {
    auto experiment = example_servo_reference();
    experiment.mode = NX_AXIS_VELOCITY;
    experiment.velocity_rad_s = 1.2F;
    example_servo_report_t report{};
    ASSERT_EQ(example_servo_run(&experiment, &report), NX_SUCCESS);
    EXPECT_TRUE(report.data_complete);
    EXPECT_TRUE(report.tracking_passed);
    EXPECT_NEAR(report.truth_velocity_rad_s, 1.2, 0.03);
    EXPECT_GT(report.truth_current_a, 0.3);
}

TEST(ServoAxis, CurrentModeUsesQuantizedAdcRatherThanAssigningPlantCurrent) {
    auto experiment = example_servo_reference();
    experiment.mode = NX_AXIS_CURRENT;
    experiment.current_a = 1.0F;
    example_servo_report_t report{};
    ASSERT_EQ(example_servo_run(&experiment, &report), NX_SUCCESS);
    EXPECT_TRUE(report.data_complete);
    EXPECT_LT(report.peak_current_error_a, 0.02F);
    EXPECT_NEAR(report.truth_current_a, 1.0, 0.01);
    EXPECT_GT(report.truth_velocity_rad_s, 0.1);
}

TEST(ServoAxis, InsufficientCurrentCannotInventTheRequiredHoldingTorque) {
    auto experiment = example_servo_reference();
    experiment.derating_at_us = 1500000U;
    experiment.derated_current_limit_a = 0.1F;
    example_servo_report_t report{};
    ASSERT_EQ(example_servo_run(&experiment, &report), NX_SUCCESS);
    EXPECT_TRUE(report.data_complete);
    EXPECT_FALSE(report.target_reached);
    EXPECT_FALSE(report.tracking_passed);
    EXPECT_GT(report.limited_samples, 0U);
    EXPECT_GT(std::fabs(report.truth_position_rad - 1.0), 0.05);
}

TEST(ServoAxis, MissingRecordIsNotFilledWithZeroError) {
    auto experiment = example_servo_reference();
    experiment.drop_record_at_us = 4500000U;
    example_servo_report_t report{};
    ASSERT_EQ(example_servo_run(&experiment, &report), NX_SUCCESS);
    EXPECT_EQ(report.samples, 999U);
    EXPECT_FALSE(report.data_complete);
    EXPECT_FALSE(report.tracking_passed);
}

TEST(ServoAxis, BoundaryStatisticsUseTheReferenceObservedByTheSensors) {
    auto experiment = example_servo_reference();
    experiment.mode = NX_AXIS_VELOCITY;
    experiment.velocity_rad_s = 1.2F;
    experiment.evaluation_start_us = 2000U;
    experiment.evaluation_samples = 1U;
    example_servo_report_t report{};
    ASSERT_EQ(example_servo_run(&experiment, &report), NX_SUCCESS);
    EXPECT_EQ(report.samples, 1U);
    EXPECT_FLOAT_EQ(report.peak_velocity_error_rad_s, 0.0F);
    EXPECT_FLOAT_EQ(report.peak_current_error_a, 0.0F);
}

TEST(ServoAxis, ControlledStopIsAdoptedBeforeObservedPhysicalRest) {
    auto experiment = example_servo_reference();
    experiment.stop_at_us = 500000U;
    example_servo_report_t report{};
    ASSERT_EQ(example_servo_run(&experiment, &report), NX_SUCCESS);
    EXPECT_EQ(report.stop_adopted_at, 500000U);
    EXPECT_GT(report.model_stopped_at, report.stop_adopted_at);
    EXPECT_FALSE(report.target_reached);
    EXPECT_FALSE(report.tracking_passed);
}

TEST(ServoAxis, CommandExpiryDoesNotBecomeANewMotionAuthorization) {
    auto experiment = example_servo_reference();
    experiment.valid_until_us = 500000U;
    experiment.control.axis.stop_policy = NX_AXIS_INHIBIT_AFTER_DWELL;
    example_servo_report_t report{};
    ASSERT_EQ(example_servo_run(&experiment, &report), NX_SUCCESS);
    EXPECT_TRUE(report.command_expired);
    EXPECT_EQ(report.stop_adopted_at, 500000U);
    EXPECT_EQ(report.axis_fault, NX_ERROR_TIMEOUT);
    EXPECT_EQ(report.final_state, NX_AXIS_FAULT);
    EXPECT_FALSE(report.tracking_passed);
}

TEST(ServoAxis, InvalidSamplingAndInputPreserveTheUnpublishedReport) {
    auto experiment = example_servo_reference();
    example_servo_report_t report{};
    report.samples = 77U;
    const auto before = report;
    experiment.duration_us = 7U;
    EXPECT_EQ(example_servo_run(&experiment, &report), NX_ERROR_INVALID);
    EXPECT_EQ(std::memcmp(&report, &before, sizeof(report)), 0);
    experiment = example_servo_reference();
    experiment.sensor_fault_at_us = 300000U;
    EXPECT_EQ(example_servo_run(&experiment, &report), NX_ERROR_STATE);
    EXPECT_EQ(std::memcmp(&report, &before, sizeof(report)), 0);
}

TEST(ServoAxis, ACompletedReferenceDoesNotGuaranteeLoadedActuatorArrival) {
    auto experiment = example_servo_reference();
    experiment.control.parameters.position_kp = 0.0F;
    experiment.control.parameters.velocity_kp = 0.0F;
    experiment.control.parameters.velocity_ki = 0.0F;
    experiment.candidate_parameters.position_kp = 0.0F;
    experiment.candidate_parameters.velocity_kp = 0.0F;
    experiment.candidate_parameters.velocity_ki = 0.0F;
    example_servo_report_t report{};
    ASSERT_EQ(example_servo_run(&experiment, &report), NX_SUCCESS);
    EXPECT_GT(report.trajectory_complete_at, 0U);
    EXPECT_FALSE(report.target_reached);
    EXPECT_FALSE(report.tracking_passed);
}
} /* namespace */
