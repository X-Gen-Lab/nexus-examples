/**
 * \file            drive_control_test.cpp
 * \brief           Actual paired AxisControl and DC current-loop consumer
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-24
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "drive.h"
#include "sil.h"
#include "sil_model.h"
#include <array>
#include <cmath>
#include <cstring>
#include <gtest/gtest.h>

namespace {
example_drive_config_t control_config() {
    example_drive_config_t result = {};
    result.radius_m = 0.1F;
    result.track_m = 0.4F;
    result.wheel_limit_rad_s = 10;
    result.command_lifetime_us = 30000;
    result.axis = {1, 1, 2000, 2000, NX_AXIS_INHIBIT_AFTER_DWELL};
    result.parameters = {1,   0.8F,  8,    20,    8,     2,     8, 10,
                         100, -1000, 1000, 0.01F, 0.05F, 10000, 0};
    result.current_pi = {2, 2000, 0, 0, -24, 24, -24, 24};
    return result;
}

nx_axis_feedback_t feedback(uint32_t sequence, nx_time_us_t now,
                            float position = 0, float velocity = 0) {
    return {1, 1, sequence, now - 1000, now, position, velocity, true};
}

class DriveControl : public testing::Test {
  protected:
    example_drive_t drive = {};
    std::array<nx_axis_feedback_t, 2> samples = {feedback(1, 1000),
                                                 feedback(1, 1000)};
    void SetUp() override {
        const auto config = control_config();
        ASSERT_EQ(example_drive_initialize(&drive, &config), NX_SUCCESS);
        ASSERT_EQ(example_drive_enable(&drive, samples.data(), 1000),
                  NX_SUCCESS);
    }
    void cycle(nx_time_us_t now, example_drive_observation_t* observation) {
        samples = {feedback(static_cast<uint32_t>(now / 1000), now),
                   feedback(static_cast<uint32_t>(now / 1000), now)};
        ASSERT_EQ(example_drive_step(&drive, samples.data(), now, 0.001F,
                                     observation),
                  NX_SUCCESS);
    }
};

TEST_F(DriveControl, ActualAxesAdoptCompleteTargetsWithCommonScaling) {
    const example_drive_command_t command = {1, 1, 1, 1000, 1, 2};
    ASSERT_EQ(example_drive_accept(&drive, &command, 1000), NX_SUCCESS);
    example_drive_observation_t observation = {};
    cycle(2000, &observation);
    EXPECT_NEAR(observation.raw_wheel_rad_s[0], 6, 1e-6F);
    EXPECT_NEAR(observation.raw_wheel_rad_s[1], 14, 1e-6F);
    EXPECT_NEAR(observation.scale, 10.0F / 14, 1e-6F);
    EXPECT_NEAR(observation.limited_wheel_rad_s[0], 60.0F / 14, 1e-6F);
    EXPECT_NEAR(observation.limited_wheel_rad_s[1], 10, 1e-6F);
    EXPECT_EQ(observation.axis[0].command_sequence, 1U);
    EXPECT_EQ(observation.axis[1].command_sequence, 1U);
    EXPECT_EQ(observation.axis[0].adopted_at, 2000U);
    EXPECT_EQ(observation.axis[1].adopted_at, 2000U);
}

TEST_F(DriveControl, DuplicateCannotRenewAuthorityAndExpiryIsInclusive) {
    example_drive_command_t command = {1, 1, 1, 1000, 0.6F, 0};
    ASSERT_EQ(example_drive_accept(&drive, &command, 1000), NX_SUCCESS);
    example_drive_observation_t observation = {};
    for (nx_time_us_t now = 2000; now < 31000; now += 1000) {
        cycle(now, &observation);
    }
    command.received_at = 30000;
    EXPECT_NE(example_drive_accept(&drive, &command, 30000), NX_SUCCESS);
    cycle(31000, &observation);
    EXPECT_TRUE(observation.axis[0].command_expired);
    EXPECT_TRUE(observation.axis[1].command_expired);
    EXPECT_NE(observation.axis[0].state, NX_AXIS_RUNNING);
    EXPECT_NE(observation.axis[1].state, NX_AXIS_RUNNING);
}

TEST_F(DriveControl, DelayedParsingDoesNotReplaceTheOriginalReceipt) {
    const example_drive_command_t command = {1, 1, 1, 1000, 0.6F, 0};
    const auto before = drive;
    EXPECT_NE(example_drive_accept(&drive, &command, 31000), NX_SUCCESS);
    EXPECT_EQ(std::memcmp(&drive, &before, sizeof(drive)), 0);
}

TEST_F(DriveControl, InvalidSecondAxisCannotPartiallyReplaceTheGroup) {
    const example_drive_command_t command = {1, 1, 1, 1000, 0.6F, 0};
    drive.axis[1].state = NX_AXIS_FAULT;
    const auto before = drive;
    EXPECT_NE(example_drive_accept(&drive, &command, 1000), NX_SUCCESS);
    EXPECT_EQ(std::memcmp(&drive, &before, sizeof(drive)), 0);
}

TEST_F(DriveControl, InvalidCurrentNeverPublishesAPartialVoltagePair) {
    const float currents[2] = {0, 0};
    float voltage[2] = {7, 9};
    EXPECT_NE(
        example_drive_current_step(&drive, currents, false, 0.00005F, voltage),
        NX_SUCCESS);
    EXPECT_FLOAT_EQ(voltage[0], 7);
    EXPECT_FLOAT_EQ(voltage[1], 9);
    EXPECT_NE(drive.fault, NX_SUCCESS);
}

TEST_F(DriveControl, ApplicationPreservesTheFullClockEpoch) {
    auto config = control_config();
    config.axis.epoch = UINT64_C(1099511627779);
    ASSERT_EQ(example_drive_initialize(&drive, &config), NX_SUCCESS);
    for (auto& item : samples) {
        item.epoch = config.axis.epoch;
    }
    ASSERT_EQ(example_drive_enable(&drive, samples.data(), 1000), NX_SUCCESS);
    const example_drive_command_t command = {
        1, config.axis.epoch, 1, 1000, 0.6F, 0};
    ASSERT_EQ(example_drive_accept(&drive, &command, 1000), NX_SUCCESS);
    samples[0].sequence = samples[1].sequence = 2;
    samples[0].interval_start = samples[1].interval_start = 1000;
    samples[0].sampled_at = samples[1].sampled_at = 2000;
    example_drive_observation_t out = {};
    ASSERT_EQ(example_drive_step(&drive, samples.data(), 2000, 0.001F, &out),
              NX_SUCCESS);
    EXPECT_EQ(drive.axis[0].command.epoch, config.axis.epoch);
    EXPECT_EQ(drive.axis[1].command.epoch, config.axis.epoch);
}

example_drive_experiment_t experiment() {
    example_drive_experiment_t result = {};
    result.control = control_config();
    result.plant = {2, 0.002, 0.1, 0.1, 0.005, 0.01, 24, 1048576, 0.001, 32768};
    result.linear_m_s = 0.6F;
    result.angular_rad_s = 1;
    result.duration_us = 3000000;
    result.evaluation_start_us = 1000000;
    result.evaluation_samples = 1000;
    result.actual_left_radius_m = 0.1;
    result.actual_right_radius_m = 0.1;
    return result;
}

TEST(DriveSil, RealQuantizedClosedLoopMeetsEachWheelTeachingWindow) {
    const auto config = experiment();
    example_drive_report_t report = {};
    ASSERT_EQ(example_drive_run(&config, &report), NX_SUCCESS);
    EXPECT_EQ(report.samples, 1000U);
    EXPECT_TRUE(report.data_complete);
    EXPECT_TRUE(report.wheel_tracking_passed);
    for (size_t i = 0; i < 2; ++i) {
        EXPECT_LE(report.peak_error_rad_s[i], 0.2F);
        EXPECT_LE(report.rms_error_rad_s[i], 0.1);
    }
}

TEST(DriveSil, MissingRecordCannotBecomeZeroErrorOrMoveTheWindow) {
    auto config = experiment();
    config.drop_record_at_us = 1500000;
    example_drive_report_t report = {};
    ASSERT_EQ(example_drive_run(&config, &report), NX_SUCCESS);
    EXPECT_EQ(report.samples, 999U);
    EXPECT_FALSE(report.data_complete);
    EXPECT_FALSE(report.wheel_tracking_passed);
}

TEST(DriveSil, WindowUsesTheReferenceAdoptedDuringTheMeasuredInterval) {
    auto config = experiment();
    config.duration_us = 3000;
    config.evaluation_start_us = 2000;
    config.evaluation_samples = 1;
    example_drive_report_t report = {};
    ASSERT_EQ(example_drive_run(&config, &report), NX_SUCCESS);
    EXPECT_FLOAT_EQ(report.peak_error_rad_s[0], 0);
    EXPECT_FLOAT_EQ(report.peak_error_rad_s[1], 0);
}

TEST(DriveSil, CommandLossAdoptsBothStopReferencesWithinTheTeachingBudget) {
    auto config = experiment();
    config.command_stop_us = 2100000;
    example_drive_report_t report = {};
    ASSERT_EQ(example_drive_run(&config, &report), NX_SUCCESS);
    ASSERT_NE(report.stop_reference_at[0], 0U);
    ASSERT_NE(report.stop_reference_at[1], 0U);
    for (const auto stopped : report.stop_reference_at) {
        EXPECT_GE(stopped - report.last_valid_received_at, 30000U);
        EXPECT_LE(stopped - report.last_valid_received_at, 40000U);
    }
    EXPECT_LT(std::fabs(report.final_velocity_rad_s[0]), 0.05F);
    EXPECT_LT(std::fabs(report.final_velocity_rad_s[1]), 0.05F);
}

TEST(DriveSil, TwoMetreTaskUsesGroundTruthAndExposesCommonRadiusError) {
    auto config = experiment();
    config.distance_m = 2;
    config.linear_m_s = 0.5F;
    config.angular_rad_s = 0;
    config.duration_us = 6001000;
    example_drive_report_t nominal = {};
    ASSERT_EQ(example_drive_run(&config, &nominal), NX_SUCCESS);
    EXPECT_EQ(nominal.stationary_samples, 500U);
    EXPECT_TRUE(nominal.stopped_window_passed)
        << nominal.final_velocity_rad_s[0] << ' '
        << nominal.final_velocity_rad_s[1];
    EXPECT_TRUE(nominal.ground_endpoint_passed);
    config.actual_left_radius_m = 0.0965;
    config.actual_right_radius_m = 0.0965;
    example_drive_report_t wrong_radius = {};
    ASSERT_EQ(example_drive_run(&config, &wrong_radius), NX_SUCCESS);
    EXPECT_NEAR(wrong_radius.odometry.x_m, 2, 0.02);
    EXPECT_LT(wrong_radius.truth.x_m, 1.95);
    EXPECT_FALSE(wrong_radius.ground_endpoint_passed);
}
} /* namespace */
