/**
 * \file            imu_reference_test.cpp
 * \brief           Actual LSM6DSOX calibration and six-axis fusion consumer
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-22
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "imu_reference.h"
#include <array>
#include <cmath>
#include <cstring>
#include <gtest/gtest.h>
#include <limits>

namespace {
struct Registers {
    std::array<uint8_t, 256> data = {};
    nx_time_us_t now = 100;
    size_t reads = 0;
    Registers() {
        data[0x0f] = 0x6c;
    }
    static nx_time_us_t clock(void* context) {
        return static_cast<Registers*>(context)->now;
    }
    static nx_result_t read(void* context, uint8_t reg, uint8_t* output,
                            size_t bytes, nx_time_us_t) {
        auto& self = *static_cast<Registers*>(context);
        ++self.reads;
        std::memcpy(output, self.data.data() + reg, bytes);
        return NX_SUCCESS;
    }
    static nx_result_t write(void* context, uint8_t reg, const uint8_t* input,
                             size_t bytes, nx_time_us_t) {
        auto& self = *static_cast<Registers*>(context);
        std::memcpy(self.data.data() + reg, input, bytes);
        if (reg == 0x12 && (input[0] & 1U) != 0) {
            self.data.fill(0);
            self.data[0x0f] = 0x6c;
        }
        return NX_SUCCESS;
    }
};

class ImuReference : public testing::Test {
  protected:
    Registers registers;
    nx_lsm6dsox_t sensor = {};
    example_imu_reference_t reference = {};
    example_imu_config_t config = {
        {{0, 0, 0}, {1, 1, 1}}, {{0, 0, 0}, {1, 1, 1}}, {1, 0, 0, 0},
        {1, 0, 0, 0},           {2, 7, 12, 0.25F},      11};

    void restart() {
        ASSERT_EQ(nx_lsm6dsox_reset_begin(&sensor, 10000), NX_SUCCESS);
        ASSERT_EQ(nx_lsm6dsox_reset_poll(&sensor), NX_SUCCESS);
        ASSERT_EQ(nx_lsm6dsox_start(&sensor, 10000), NX_SUCCESS);
        registers.data[0x1e] = 3;
        registers.data[0x26] = 0xe8;
        registers.data[0x27] = 0x03;
    }
    void SetUp() override {
        const nx_lsm6dsox_transport_port_t port = {
            &registers,
            Registers::read,
            Registers::write,
            {Registers::clock, &registers}};
        const nx_lsm6dsox_config_t selected = {1, 2, NX_LSM6DSOX_DIRECT};
        ASSERT_EQ(nx_lsm6dsox_init(&sensor, port, &selected, 10000),
                  NX_SUCCESS);
        restart();
        ASSERT_EQ(example_imu_init(&reference, &sensor, &config), NX_SUCCESS);
    }
};

TEST_F(ImuReference, RealSensorDataFlowsThroughCalibrationAndFusion) {
    example_imu_observation_t result = {};
    ASSERT_EQ(example_imu_step(&reference, 0.01F, 10000, &result), NX_SUCCESS);
    EXPECT_EQ(result.sample.sequence, 1U);
    EXPECT_EQ(result.calibration_version, 11U);
    EXPECT_NEAR(result.angular_rate_body.z, 0.30543262F, 1e-6F);
    EXPECT_NEAR(result.attitude.orientation.z, std::sin(0.0030543262F / 2),
                1e-6F);
    EXPECT_FALSE(result.attitude.acceleration_used);
    EXPECT_EQ(result.sample.observed.source,
              NX_IO_TIMESTAMP_SERVICE_OBSERVATION);
}

TEST_F(ImuReference,
       GenerationChangeLatchesResynchronizationWithoutNewEstimate) {
    example_imu_observation_t result = {};
    ASSERT_EQ(example_imu_step(&reference, 0.01F, 10000, &result), NX_SUCCESS);
    const auto before = result;
    ASSERT_EQ(nx_lsm6dsox_stop(&sensor, 10000), NX_SUCCESS);
    restart();
    EXPECT_EQ(example_imu_step(&reference, 0.01F, 10000, &result),
              NX_ERROR_STATE);
    EXPECT_TRUE(reference.faulted);
    EXPECT_EQ(std::memcmp(&result, &before, sizeof(result)), 0);
    EXPECT_EQ(example_imu_step(&reference, 0.01F, 10000, &result),
              NX_ERROR_STATE);
}

TEST_F(ImuReference, InvalidIntervalRejectsBeforeSamplingAndBadConfigIsAtomic) {
    example_imu_observation_t result = {};
    const auto before = reference;
    const auto reads = registers.reads;
    EXPECT_EQ(example_imu_step(&reference, 0, 10000, &result),
              NX_ERROR_INVALID);
    EXPECT_EQ(registers.reads, reads);
    auto invalid = config;
    invalid.gyroscope.scale.z = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(example_imu_init(&reference, &sensor, &invalid),
              NX_ERROR_INVALID);
    EXPECT_EQ(std::memcmp(&reference, &before, sizeof(reference)), 0);
}
} /* namespace */
