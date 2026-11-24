/**
 * \file            rotor_test.cpp
 * \brief           Real AS5047P protocol, timestamp and bounded handoff checks
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-24
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "as5047p_wire.h"
#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>

namespace {
using Wire = example_test::As5047pWire;

class MotorRotor : public testing::Test {
  protected:
    Wire wire;
    nx_as5047p_config_t config = {
        &wire.endpoint, {Wire::time, &wire}, 1U, 1U, false};
    nx_as5047p_t device = {};
    example_rotor_queue_t queue = {};
    example_rotor_reader_t reader = {};
    void SetUp() override {
        ASSERT_EQ(nx_as5047p_init(&device, &config, 10000U), NX_SUCCESS);
        ASSERT_EQ(example_rotor_queue_initialize(&queue, 1U, 1U, 200U),
                  NX_SUCCESS);
        ASSERT_EQ(example_rotor_reader_initialize(&reader, &device, &queue,
                                                  false, 0.0F, 1000U, 200U),
                  NX_SUCCESS);
    }
    void anchor() {
        wire.now = 100U;
        ASSERT_EQ(example_rotor_poll(&reader, 10000U), NX_ERROR_BUSY);
    }
    void publish(uint16_t angle, nx_time_us_t at) {
        wire.now = at;
        wire.angle = angle;
        ASSERT_EQ(example_rotor_poll(&reader, 10000U), NX_SUCCESS);
    }
};

TEST_F(MotorRotor, ReadsTheRealSensorAndPreservesObservationTime) {
    anchor();
    publish(1100U, 200U);
    const unsigned transfers = wire.transfers;
    example_rotor_sample_t sample = {};
    ASSERT_EQ(example_rotor_take(&queue, 210U, 1U, 1U, &sample), NX_SUCCESS);
    EXPECT_EQ(wire.transfers, transfers);
    EXPECT_EQ(sample.angle_at, 200U);
    EXPECT_EQ(sample.sequence, 1U);
    EXPECT_EQ(sample.position_counts, 100);
    EXPECT_GT(sample.mechanical_velocity_rad_s, 0.0F);
    EXPECT_NEAR(sample.mechanical_angle_rad,
                1100.0F * 6.283185307179586F / 16384.0F, 0.00001F);
}

TEST_F(MotorRotor, CachedAngleExpiresWithoutInventingFreshTimestamps) {
    anchor();
    publish(1100U, 200U);
    example_rotor_sample_t sample = {};
    ASSERT_EQ(example_rotor_take(&queue, 210U, 1U, 1U, &sample), NX_SUCCESS);
    ASSERT_EQ(example_rotor_take(&queue, 399U, 1U, 1U, &sample), NX_SUCCESS);
    EXPECT_EQ(sample.angle_at, 200U);
    EXPECT_EQ(example_rotor_take(&queue, 400U, 1U, 1U, &sample),
              NX_ERROR_TIMEOUT);
    EXPECT_EQ(sample.angle_at, 200U);
}

TEST_F(MotorRotor, BadParityClosesTheCachedObservationPath) {
    anchor();
    publish(1100U, 200U);
    example_rotor_sample_t sample = {};
    ASSERT_EQ(example_rotor_take(&queue, 210U, 1U, 1U, &sample), NX_SUCCESS);
    wire.corrupt = true;
    wire.now = 250U;
    EXPECT_EQ(example_rotor_poll(&reader, 10000U), NX_ERROR_IO);
    EXPECT_EQ(example_rotor_take(&queue, 251U, 1U, 1U, &sample), NX_ERROR_IO);
    EXPECT_EQ(sample.angle_at, 200U);
}

TEST_F(MotorRotor, FullHandoffNeverOverwritesOrPollsFromTheFastSide) {
    anchor();
    publish(1100U, 200U);
    publish(1200U, 300U);
    wire.angle = 1300U;
    wire.now = 350U;
    EXPECT_EQ(example_rotor_poll(&reader, 10000U), NX_ERROR_BUSY);
    const unsigned transfers = wire.transfers;
    example_rotor_sample_t sample = {};
    ASSERT_EQ(example_rotor_take(&queue, 350U, 1U, 1U, &sample), NX_SUCCESS);
    EXPECT_EQ(sample.angle_at, 200U);
    ASSERT_EQ(example_rotor_take(&queue, 350U, 1U, 1U, &sample), NX_SUCCESS);
    EXPECT_EQ(sample.angle_at, 300U);
    EXPECT_EQ(wire.transfers, transfers);
}

TEST_F(MotorRotor, WrongEpochCannotUseAValidCachedSample) {
    anchor();
    publish(1100U, 200U);
    example_rotor_sample_t sample = {};
    EXPECT_EQ(example_rotor_take(&queue, 210U, 1U, 2U, &sample),
              NX_ERROR_STATE);
}

TEST_F(MotorRotor, MissingAndAmbiguousMotionDoesNotCreateVelocity) {
    example_rotor_sample_t sample = {};
    EXPECT_EQ(example_rotor_take(&queue, 50U, 1U, 1U, &sample), NX_ERROR_EMPTY);
    anchor();
    wire.now = 200U;
    wire.angle = 9000U;
    EXPECT_NE(example_rotor_poll(&reader, 10000U), NX_SUCCESS);
    EXPECT_NE(example_rotor_take(&queue, 201U, 1U, 1U, &sample), NX_SUCCESS);
}

TEST_F(MotorRotor, ReversalAndWrapHaveOneMechanicalDirection) {
    ASSERT_EQ(example_rotor_reader_initialize(&reader, &device, &queue, true,
                                              0.1F, 1000U, 200U),
              NX_SUCCESS);
    wire.angle = 16300U;
    anchor();
    publish(100U, 200U);
    example_rotor_sample_t sample = {};
    ASSERT_EQ(example_rotor_take(&queue, 210U, 1U, 1U, &sample), NX_SUCCESS);
    EXPECT_EQ(sample.position_counts, -184);
    EXPECT_LT(sample.mechanical_velocity_rad_s, 0.0F);
}

TEST_F(MotorRotor, ExcessiveReadGapClosesTheSource) {
    anchor();
    wire.now = 301U;
    wire.angle = 1100U;
    EXPECT_EQ(example_rotor_poll(&reader, 10000U), NX_ERROR_TIMEOUT);
    example_rotor_sample_t sample = {};
    EXPECT_EQ(example_rotor_take(&queue, 302U, 1U, 1U, &sample),
              NX_ERROR_TIMEOUT);
}

TEST_F(MotorRotor, AConsumerFaultClosesFurtherSensorTransfers) {
    anchor();
    publish(1100U, 200U);
    example_rotor_sample_t sample = {};
    ASSERT_EQ(example_rotor_take(&queue, 210U, 1U, 1U, &sample), NX_SUCCESS);
    ASSERT_EQ(example_rotor_take(&queue, 400U, 1U, 1U, &sample),
              NX_ERROR_TIMEOUT);
    const unsigned transfers = wire.transfers;
    wire.now = 250U;
    EXPECT_EQ(example_rotor_poll(&reader, 10000U), NX_ERROR_TIMEOUT);
    EXPECT_EQ(wire.transfers, transfers);
}

TEST_F(MotorRotor, ANewMappingRequiresAJoinedFreshChannel) {
    anchor();
    publish(1100U, 200U);
    example_rotor_sample_t sample = {};
    ASSERT_EQ(example_rotor_take(&queue, 210U, 1U, 1U, &sample), NX_SUCCESS);
    EXPECT_EQ(example_rotor_reader_initialize(&reader, &device, &queue, true,
                                              0.5F, 1000U, 200U),
              NX_ERROR_STATE);
    ASSERT_EQ(example_rotor_take(&queue, 211U, 1U, 1U, &sample), NX_SUCCESS);
    EXPECT_GT(sample.mechanical_velocity_rad_s, 0.0F);
}

TEST_F(MotorRotor, TwoExecutorsTransferWholeRecordsWithBoundedStorage) {
    /* Exercise repeated two-slot reuse, not host scheduling throughput. */
    constexpr uint32_t records = 512U;
    ASSERT_EQ(example_rotor_queue_initialize(&queue, 1U, 1U, records + 1U),
              NX_SUCCESS);
    std::atomic<bool> failed{false};
    std::thread producer([&] {
        for (uint32_t i = 1U; i <= records && !failed.load(); ++i) {
            const example_rotor_sample_t input = {i,
                                                  1U,
                                                  i,
                                                  static_cast<float>(i % 100U) *
                                                      0.01F,
                                                  static_cast<float>(i),
                                                  -static_cast<float>(i),
                                                  1U,
                                                  i};
            unsigned attempts = 0U;
            for (;;) {
                const nx_result_t result =
                    example_rotor_publish(&queue, &input);
                if (result == NX_SUCCESS) {
                    break;
                }
                if (result != NX_ERROR_BUSY || failed.load() ||
                    ++attempts == 1000000U) {
                    failed.store(true);
                    break;
                }
                std::this_thread::yield();
            }
        }
    });
    uint32_t expected = 1U;
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (expected <= records && !failed.load()) {
        if (std::chrono::steady_clock::now() >= end) {
            failed.store(true);
            break;
        }
        example_rotor_sample_t sample = {};
        const nx_result_t result =
            example_rotor_take(&queue, records, 1U, 1U, &sample);
        if (result == NX_ERROR_EMPTY ||
            (result == NX_SUCCESS && sample.sequence < expected)) {
            std::this_thread::yield();
            continue;
        }
        if (result != NX_SUCCESS || sample.sequence != expected ||
            sample.angle_at != expected ||
            sample.position_counts != static_cast<int64_t>(expected) ||
            sample.mechanical_position_rad != static_cast<float>(expected) ||
            sample.mechanical_velocity_rad_s != -static_cast<float>(expected)) {
            failed.store(true);
            break;
        }
        ++expected;
    }
    producer.join();
    EXPECT_FALSE(failed.load());
    EXPECT_EQ(expected, records + 1U);
}
} /* namespace */
