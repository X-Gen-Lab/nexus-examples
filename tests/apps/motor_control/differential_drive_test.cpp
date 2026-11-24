/**
 * \file            differential_drive_test.cpp
 * \brief           Real DC plant, quantized feedback and independent truth
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-24
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "sil_model.h"
#include <cmath>
#include <cstring>
#include <gtest/gtest.h>
#include <limits>

namespace {
example_drive_plant_config_t configuration() {
    return {2, 0.002, 0.1, 0.1, 0.005, 0.01, 24, 1048576, 0.01, 2048};
}

TEST(DriveModel, OutputIsStagedAndAdoptedAtTheNextModelBoundary) {
    example_drive_plant_t plant = {};
    const auto config = configuration();
    ASSERT_EQ(example_drive_plant_initialize(&plant, &config), NX_SUCCESS);
    ASSERT_EQ(example_drive_plant_stage(&plant, 100), NX_SUCCESS);
    EXPECT_DOUBLE_EQ(plant.active_voltage_v, 0);
    ASSERT_EQ(example_drive_plant_advance(&plant, 0.00005, 0), NX_SUCCESS);
    EXPECT_DOUBLE_EQ(plant.current_a, 0);
    EXPECT_DOUBLE_EQ(plant.active_voltage_v, 24);
    ASSERT_EQ(example_drive_plant_advance(&plant, 0.00005, 0), NX_SUCCESS);
    EXPECT_NEAR(plant.current_a, 0.6, 1e-12);
}

TEST(DriveModel, InvalidParametersAndIntervalsDoNotMutateTruth) {
    example_drive_plant_t plant = {};
    auto config = configuration();
    ASSERT_EQ(example_drive_plant_initialize(&plant, &config), NX_SUCCESS);
    const auto before = plant;
    config.inductance_h = 0;
    EXPECT_EQ(example_drive_plant_initialize(&plant, &config),
              NX_ERROR_INVALID);
    EXPECT_EQ(std::memcmp(&plant, &before, sizeof(plant)), 0);
    EXPECT_EQ(example_drive_plant_advance(&plant, 1, 0), NX_ERROR_INVALID);
    EXPECT_EQ(example_drive_plant_stage(
                  &plant, std::numeric_limits<double>::quiet_NaN()),
              NX_ERROR_INVALID);
    EXPECT_EQ(std::memcmp(&plant, &before, sizeof(plant)), 0);
}

TEST(DriveModel, SensorsQuantizeWrapAndReportSaturationWithoutChangingTruth) {
    example_drive_plant_t plant = {};
    const auto config = configuration();
    ASSERT_EQ(example_drive_plant_initialize(&plant, &config), NX_SUCCESS);
    plant.position_rad = -2 * std::acos(-1.0) / config.counts_per_turn;
    plant.current_a = -0.024;
    plant.cycle = 9;
    const auto before = plant;
    example_drive_sensor_t sample = {};
    ASSERT_EQ(example_drive_plant_sense(&plant, true, &sample), NX_SUCCESS);
    EXPECT_EQ(sample.encoder_count, UINT32_MAX);
    EXPECT_EQ(sample.current_code, 2046);
    EXPECT_EQ(sample.sequence, 9U);
    EXPECT_TRUE(sample.valid);
    EXPECT_FALSE(sample.saturated);
    EXPECT_EQ(std::memcmp(&plant, &before, sizeof(plant)), 0);
    plant.current_a = 5000;
    ASSERT_EQ(example_drive_plant_sense(&plant, false, &sample), NX_SUCCESS);
    EXPECT_EQ(sample.current_code, UINT16_MAX);
    EXPECT_TRUE(sample.saturated);
    EXPECT_FALSE(sample.valid);
}

TEST(DriveModel, MechanicalLoadChangesAccelerationRatherThanTarget) {
    example_drive_plant_t plant = {};
    const auto config = configuration();
    ASSERT_EQ(example_drive_plant_initialize(&plant, &config), NX_SUCCESS);
    plant.current_a = 2;
    plant.velocity_rad_s = 1;
    ASSERT_EQ(example_drive_plant_advance(&plant, 0.00005, 0.3), NX_SUCCESS);
    EXPECT_LT(plant.velocity_rad_s, 1);
    EXPECT_GT(plant.current_a, 0);
}

TEST(DriveModel, EncoderOdometryCannotSubstituteForIndependentGroundTruth) {
    example_drive_pose_t truth = {};
    example_drive_pose_t odometry = {};
    for (int n = 0; n < 2000; ++n) {
        ASSERT_EQ(example_drive_pose_advance(&truth, 10, 10, 0.0965, 0.0965,
                                             0.4, 0.001),
                  NX_SUCCESS);
        ASSERT_EQ(
            example_drive_pose_advance(&odometry, 10, 10, 0.1, 0.1, 0.4, 0.001),
            NX_SUCCESS);
    }
    EXPECT_NEAR(odometry.x_m, 2, 1e-10);
    EXPECT_NEAR(truth.x_m, 1.93, 1e-10);
    EXPECT_GT(std::fabs(truth.x_m - 2), 0.05);
}
} /* namespace */
