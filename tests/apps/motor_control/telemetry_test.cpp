/**
 * \file            telemetry_test.cpp
 * \brief           Real versioned drive framing and bounded Task stream writes
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-25
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "telemetry.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace {
struct Wire {
    std::vector<uint8_t> bytes;
    size_t budget = EXAMPLE_MOTOR_TELEMETRY_BYTES;
    unsigned calls = 0;
    nx_result_t status = NX_SUCCESS;
    bool impossible = false;
    static nx_result_t write(void* context, const uint8_t* source, size_t count,
                             size_t* written) {
        auto& self = *static_cast<Wire*>(context);
        ++self.calls;
        *written = self.impossible ? count + 1 : std::min(count, self.budget);
        if (!self.impossible) {
            self.bytes.insert(self.bytes.end(), source, source + *written);
        }
        return self.status;
    }
};
example_motor_observation_t observation() {
    example_motor_observation_t record = {};
    record.sample.sequence = UINT64_C(0x0807060504030201);
    record.sample.clock_epoch = 9;
    record.sample.sample_at = 100;
    record.sample.observed_at = 103;
    record.sample.planned_apply_at = 200;
    record.sample.current_raw[0] = 0x1234;
    record.sample.current_count = 2;
    record.sample.current_phase_mask = 3;
    record.sample.bus_valid = true;
    record.sample.bus_raw = 2400;
    record.plan.sequence = record.sample.sequence + 1;
    record.plan.duty_ticks[0] = 25;
    record.plan.trigger_ticks = 800;
    record.result = NX_SUCCESS;
    return record;
}
TEST(MotorTelemetry, LittleEndianFrameCarriesVersionUnitsSourceAndLoss) {
    example_motor_telemetry_t telemetry = {};
    Wire wire;
    ASSERT_EQ(example_motor_telemetry_initialize(&telemetry, Wire::write, &wire,
                                                 1000000),
              NX_SUCCESS);
    telemetry.dropped = 7;
    const auto record = observation();
    ASSERT_EQ(example_motor_telemetry_sink(&telemetry, &record), NX_SUCCESS);
    ASSERT_EQ(wire.bytes.size(), EXAMPLE_MOTOR_TELEMETRY_BYTES);
    EXPECT_EQ(std::string(wire.bytes.begin(), wire.bytes.begin() + 4), "NXMT");
    EXPECT_EQ(wire.bytes[4], 1U);
    EXPECT_EQ(wire.bytes[5], 3U);
    EXPECT_EQ(wire.bytes[6], 116U);
    EXPECT_EQ(wire.bytes[7], 0U);
    EXPECT_EQ(wire.bytes[12], 7U);
    for (size_t i = 0; i < 8; ++i) {
        EXPECT_EQ(wire.bytes[16 + i], i + 1U);
    }
    EXPECT_EQ(wire.bytes[60], 0x34U);
    EXPECT_EQ(wire.bytes[61], 0x12U);
    EXPECT_EQ(wire.bytes[68], 25U);
    EXPECT_TRUE(std::all_of(wire.bytes.begin() + 96, wire.bytes.begin() + 110,
                            [](uint8_t value) { return value == 0; }));
    EXPECT_EQ(wire.bytes[110], 2U);
    EXPECT_EQ(wire.bytes[111], 3U);
    EXPECT_EQ(wire.bytes[112], 0x40U);
    EXPECT_EQ(wire.bytes[113], 0x42U);
    EXPECT_EQ(wire.bytes[114], 0x0FU);
    EXPECT_FALSE(telemetry.pending);
    EXPECT_EQ(wire.calls, 1U);
}
TEST(MotorTelemetry, PartialErrorAndBusyRetainOneChannelHeadWithoutReplay) {
    example_motor_telemetry_t telemetry = {};
    Wire wire;
    wire.budget = 13;
    wire.status = NX_ERROR_BUSY;
    ASSERT_EQ(example_motor_telemetry_initialize(&telemetry, Wire::write, &wire,
                                                 1000000),
              NX_SUCCESS);
    auto record = observation();
    EXPECT_EQ(example_motor_telemetry_sink(&telemetry, &record), NX_ERROR_BUSY);
    EXPECT_EQ(telemetry.offset, 13U);
    EXPECT_EQ(wire.calls, 1U);
    telemetry.dropped = 99;
    wire.status = NX_ERROR_IO;
    EXPECT_EQ(example_motor_telemetry_sink(&telemetry, &record), NX_ERROR_IO);
    EXPECT_EQ(telemetry.offset, 26U);
    auto other = record;
    ++other.sample.sequence;
    EXPECT_EQ(example_motor_telemetry_sink(&telemetry, &other), NX_ERROR_STATE);
    EXPECT_EQ(wire.calls, 2U);
    wire.status = NX_SUCCESS;
    wire.budget = EXAMPLE_MOTOR_TELEMETRY_BYTES;
    EXPECT_EQ(example_motor_telemetry_sink(&telemetry, &record), NX_SUCCESS);
    ASSERT_EQ(wire.bytes.size(), EXAMPLE_MOTOR_TELEMETRY_BYTES);
    EXPECT_EQ(wire.bytes[12], 0U);
    EXPECT_EQ(wire.calls, 3U);
    EXPECT_FALSE(telemetry.pending);
}
TEST(MotorTelemetry, ImpossibleProgressFailsClosedAndInitializationDoesNoIO) {
    example_motor_telemetry_t telemetry = {};
    Wire wire;
    EXPECT_EQ(example_motor_telemetry_initialize(&telemetry, nullptr, &wire, 1),
              NX_ERROR_INVALID);
    EXPECT_EQ(
        example_motor_telemetry_initialize(&telemetry, Wire::write, &wire, 0),
        NX_ERROR_INVALID);
    ASSERT_EQ(
        example_motor_telemetry_initialize(&telemetry, Wire::write, &wire, 1),
        NX_SUCCESS);
    EXPECT_EQ(wire.calls, 0U);
    wire.impossible = true;
    const auto record = observation();
    EXPECT_EQ(example_motor_telemetry_sink(&telemetry, &record), NX_ERROR_IO);
    EXPECT_TRUE(telemetry.broken);
    wire.impossible = false;
    EXPECT_EQ(example_motor_telemetry_sink(&telemetry, &record),
              NX_ERROR_STATE);
    EXPECT_EQ(wire.calls, 1U);
}

TEST(MotorTelemetry, DiagnosticBackpressureRetainsHeadAndFailureHasNoPlan) {
    example_motor_observations_t channel = {};
    nx_power_stage_sample_t sample = {};
    sample.sequence = 5;
    sample.clock_epoch = 1;
    example_motor_observation_publish(&channel, &sample, nullptr, NX_ERROR_IO);
    example_motor_telemetry_t telemetry = {};
    Wire wire;
    wire.budget = 0;
    ASSERT_EQ(
        example_motor_telemetry_initialize(&telemetry, Wire::write, &wire, 1),
        NX_SUCCESS);
    uint32_t delivered = 0, dropped = 0;
    EXPECT_EQ(
        example_motor_observation_drain(&channel, example_motor_telemetry_sink,
                                        &telemetry, 1, &delivered, &dropped),
        NX_ERROR_BUSY);
    EXPECT_EQ(delivered, 0U);
    EXPECT_EQ(channel.consumed, 0U);
    wire.budget = EXAMPLE_MOTOR_TELEMETRY_BYTES;
    EXPECT_EQ(
        example_motor_observation_drain(&channel, example_motor_telemetry_sink,
                                        &telemetry, 1, &delivered, &dropped),
        NX_SUCCESS);
    EXPECT_EQ(delivered, 1U);
    EXPECT_EQ(channel.consumed, 1U);
    ASSERT_EQ(wire.bytes.size(), EXAMPLE_MOTOR_TELEMETRY_BYTES);
    EXPECT_EQ(wire.bytes[5], 0U);
    EXPECT_EQ(wire.bytes[8], static_cast<uint8_t>(NX_ERROR_IO));
    EXPECT_TRUE(std::all_of(wire.bytes.begin() + 68, wire.bytes.begin() + 92,
                            [](uint8_t value) { return value == 0; }));
}
} /* namespace */
