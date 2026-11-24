/**
 * \file            bus_test.cpp
 * \brief           Real INA226 source and qualified voltage handoff contracts
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-24
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "bus_ina226.h"
#include <array>
#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <limits>
#include <thread>

namespace {
example_bus_sample_t sample(uint32_t sequence = 1U) {
    return {10U * sequence,
            10U * sequence + 5U,
            1000U,
            UINT64_C(0x100000001),
            7U,
            42U,
            sequence,
            24.0F};
}

TEST(MotorBus, RetainsOriginalSourceTimeAndWhole64BitIdentity) {
    example_bus_queue_t queue = {};
    ASSERT_EQ(
        example_bus_queue_initialize(&queue, 7U, UINT64_C(0x100000001), 42U),
        NX_SUCCESS);
    const auto input = sample();
    ASSERT_EQ(example_bus_publish(&queue, &input), NX_SUCCESS);
    example_bus_sample_t output = {};
    ASSERT_EQ(
        example_bus_take(&queue, 100U, 7U, UINT64_C(0x100000001), 42U, &output),
        NX_SUCCESS);
    EXPECT_EQ(output.sampled_at, 10U);
    EXPECT_EQ(output.observed_at, 15U);
    EXPECT_EQ(output.valid_until, 1000U);
    EXPECT_FLOAT_EQ(output.voltage_v, 24);
    ASSERT_EQ(
        example_bus_take(&queue, 999U, 7U, UINT64_C(0x100000001), 42U, &output),
        NX_SUCCESS);
    EXPECT_EQ(output.sampled_at, 10U);
    EXPECT_EQ(example_bus_take(&queue, 1000U, 7U, UINT64_C(0x100000001), 42U,
                               &output),
              NX_ERROR_TIMEOUT);
    EXPECT_EQ(output.sampled_at, 10U);
}

TEST(MotorBus, EmptyAndFullNeverInventOrOverwriteObservations) {
    example_bus_queue_t queue = {};
    ASSERT_EQ(
        example_bus_queue_initialize(&queue, 7U, UINT64_C(0x100000001), 42U),
        NX_SUCCESS);
    example_bus_sample_t output = {};
    output.sequence = 99;
    EXPECT_EQ(
        example_bus_take(&queue, 100U, 7U, UINT64_C(0x100000001), 42U, &output),
        NX_ERROR_EMPTY);
    EXPECT_EQ(output.sequence, 99U);
    const auto first = sample(1), second = sample(2), third = sample(3);
    ASSERT_EQ(example_bus_publish(&queue, &first), NX_SUCCESS);
    ASSERT_EQ(example_bus_publish(&queue, &second), NX_SUCCESS);
    EXPECT_EQ(example_bus_publish(&queue, &third), NX_ERROR_BUSY);
    ASSERT_EQ(
        example_bus_take(&queue, 100U, 7U, UINT64_C(0x100000001), 42U, &output),
        NX_SUCCESS);
    EXPECT_EQ(output.sequence, 1U);
    EXPECT_EQ(example_bus_publish(&queue, &third), NX_SUCCESS);
}

TEST(MotorBus, SourceFaultBypassesCapacityAndInvalidatesFreshCache) {
    example_bus_queue_t queue = {};
    ASSERT_EQ(
        example_bus_queue_initialize(&queue, 7U, UINT64_C(0x100000001), 42U),
        NX_SUCCESS);
    const auto input = sample();
    ASSERT_EQ(example_bus_publish(&queue, &input), NX_SUCCESS);
    example_bus_sample_t output = {};
    ASSERT_EQ(
        example_bus_take(&queue, 100U, 7U, UINT64_C(0x100000001), 42U, &output),
        NX_SUCCESS);
    EXPECT_EQ(example_bus_fault(&queue, NX_ERROR_NACK), NX_ERROR_NACK);
    EXPECT_EQ(example_bus_fault(&queue, NX_ERROR_IO), NX_ERROR_NACK);
    EXPECT_EQ(
        example_bus_take(&queue, 101U, 7U, UINT64_C(0x100000001), 42U, &output),
        NX_ERROR_NACK);
    EXPECT_EQ(output.sequence, 1U);
}

TEST(MotorBus, FutureReadAndEpochTruncationCannotQualifyCachedData) {
    example_bus_queue_t queue = {};
    ASSERT_EQ(
        example_bus_queue_initialize(&queue, 7U, UINT64_C(0x100000001), 42U),
        NX_SUCCESS);
    const auto input = sample();
    ASSERT_EQ(example_bus_publish(&queue, &input), NX_SUCCESS);
    example_bus_sample_t output = {};
    EXPECT_EQ(
        example_bus_take(&queue, 14U, 7U, UINT64_C(0x100000001), 42U, &output),
        NX_ERROR_STATE);
    ASSERT_EQ(
        example_bus_queue_initialize(&queue, 7U, UINT64_C(0x100000001), 42U),
        NX_SUCCESS);
    ASSERT_EQ(example_bus_publish(&queue, &input), NX_SUCCESS);
    EXPECT_EQ(example_bus_take(&queue, 100U, 7U, 1U, 42U, &output),
              NX_ERROR_STATE);
    EXPECT_EQ(output.sequence, 0U);
}

TEST(MotorBus, WrongSourceAndInvalidVoltageNeverEnterTheChannel) {
    example_bus_queue_t queue = {};
    ASSERT_EQ(
        example_bus_queue_initialize(&queue, 7U, UINT64_C(0x100000001), 42U),
        NX_SUCCESS);
    auto input = sample();
    input.source_id = 43;
    EXPECT_EQ(example_bus_publish(&queue, &input), NX_ERROR_INVALID);
    input = sample();
    input.voltage_v = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(example_bus_publish(&queue, &input), NX_ERROR_INVALID);
    EXPECT_EQ(queue.produced, 0U);
}

TEST(MotorBus, TwoThreadsExchangeWholeSourceRecordsWithoutIoOrOverwrite) {
    /* Repeated two-slot reuse is a correctness contract, not throughput. */
    constexpr uint32_t records = 512;
    example_bus_queue_t queue = {};
    ASSERT_EQ(
        example_bus_queue_initialize(&queue, 7U, UINT64_C(0x100000001), 42U),
        NX_SUCCESS);
    std::atomic<bool> failed{false};
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    std::thread producer([&] {
        for (uint32_t i = 1U; i <= records && !failed.load(); ++i) {
            auto input = sample(i);
            input.valid_until = 10000000U;
            input.voltage_v = 24.0F + static_cast<float>(i) * 0.00001F;
            nx_result_t result;
            do {
                result = example_bus_publish(&queue, &input);
                if (result == NX_ERROR_BUSY)
                    std::this_thread::yield();
                if (std::chrono::steady_clock::now() >= end)
                    failed.store(true);
            } while (result == NX_ERROR_BUSY && !failed.load());
            if (result != NX_SUCCESS)
                failed.store(true);
        }
    });
    uint32_t last = 0U;
    while (last < records && !failed.load()) {
        example_bus_sample_t output = {};
        const nx_result_t result = example_bus_take(
            &queue, 1000000U, 7U, UINT64_C(0x100000001), 42U, &output);
        if (result == NX_ERROR_EMPTY) {
            std::this_thread::yield();
        } else if (result != NX_SUCCESS || output.sequence < last ||
                   output.sequence > last + 1U ||
                   output.sampled_at != 10U * output.sequence ||
                   output.observed_at != output.sampled_at + 5U ||
                   output.source_id != 42U ||
                   output.voltage_v !=
                       24.0F + static_cast<float>(output.sequence) * 0.00001F) {
            failed.store(true);
        } else {
            last = output.sequence;
        }
        if (std::chrono::steady_clock::now() >= end)
            failed.store(true);
    }
    producer.join();
    EXPECT_FALSE(failed.load());
    EXPECT_EQ(last, records);
}

struct Wire {
    std::array<uint16_t, 256> registers = {};
    nx_time_us_t now = 10;
    unsigned calls = 0;
    bool fail = false;
    Wire() {
        registers[0xfe] = 0x5449;
        registers[0xff] = 0x2260;
        registers[2] = 19200;
    }
    static nx_time_us_t clock(void* context) {
        return static_cast<Wire*>(context)->now;
    }
    static nx_result_t transaction(void* context, nx_i2c_message_t* messages,
                                   size_t count, nx_time_us_t,
                                   size_t* transferred) {
        auto& wire = *static_cast<Wire*>(context);
        ++wire.calls;
        if (wire.fail) {
            *transferred = 0;
            return NX_ERROR_NACK;
        }
        const uint8_t reg = messages[0].buffer.tx[0];
        if (count == 2U) {
            const uint16_t value = wire.registers[reg];
            messages[1].buffer.rx[0] = static_cast<uint8_t>(value >> 8U);
            messages[1].buffer.rx[1] = static_cast<uint8_t>(value);
            if (reg == 6U)
                wire.registers[6] &= static_cast<uint16_t>(~0x0008U);
            *transferred = 3;
        } else {
            wire.registers[reg] = static_cast<uint16_t>(
                (static_cast<uint16_t>(messages[0].buffer.tx[1]) << 8U) |
                messages[0].buffer.tx[2]);
            if (reg == 0U)
                wire.registers[6] &= static_cast<uint16_t>(~0x0008U);
            *transferred = 3;
        }
        return NX_SUCCESS;
    }
    static const nx_i2c_endpoint_ops_t ops;
    nx_i2c_endpoint_t endpoint = {&ops, this};
};
const nx_i2c_endpoint_ops_t Wire::ops = {transaction, nullptr, nullptr,
                                         nullptr};

class MotorBusIna226 : public testing::Test {
  protected:
    Wire wire;
    nx_ina226_config_t config = {&wire.endpoint,
                                 {Wire::clock, &wire},
                                 7U,
                                 9U,
                                 2000U,
                                 1000000U,
                                 0U,
                                 0U,
                                 0U};
    nx_ina226_t sensor = {};
    example_bus_queue_t queue = {};
    example_bus_ina226_t reader = {};
    void SetUp() override {
        ASSERT_EQ(nx_ina226_init(&sensor, &config, 10000U), NX_SUCCESS);
        ASSERT_EQ(example_bus_queue_initialize(&queue, 7U, 9U, 42U),
                  NX_SUCCESS);
        ASSERT_EQ(example_bus_ina226_initialize(&reader, &sensor, &queue, 100U),
                  NX_SUCCESS);
    }
    void convert(nx_time_us_t at) {
        ASSERT_EQ(example_bus_ina226_begin(&reader, 10000U), NX_SUCCESS);
        wire.now = at;
        wire.registers[6] |= 0x0008U;
        ASSERT_EQ(example_bus_ina226_poll(&reader), NX_SUCCESS);
    }
};

TEST_F(MotorBusIna226, FreshOneShotFeedsFastConsumerWithoutI2cInTheFastPath) {
    wire.registers[6] |= 0x0008U;
    ASSERT_EQ(example_bus_ina226_begin(&reader, 10000U), NX_SUCCESS);
    EXPECT_EQ(wire.registers[6] & 0x0008U, 0U);
    EXPECT_EQ(example_bus_ina226_poll(&reader), NX_ERROR_BUSY);
    wire.now = 30U;
    wire.registers[6] |= 0x0008U;
    ASSERT_EQ(example_bus_ina226_poll(&reader), NX_SUCCESS);
    const unsigned calls = wire.calls;
    example_bus_sample_t output = {};
    ASSERT_EQ(example_bus_take(&queue, 40U, 7U, 9U, 42U, &output), NX_SUCCESS);
    EXPECT_EQ(wire.calls, calls);
    EXPECT_EQ(output.sampled_at, 10U);
    EXPECT_EQ(output.observed_at, 30U);
    EXPECT_EQ(output.valid_until, 110U);
    EXPECT_FLOAT_EQ(output.voltage_v, 24);
}

TEST_F(MotorBusIna226, ReadDelayDoesNotRefreshTheOriginalConversionAuthority) {
    ASSERT_EQ(example_bus_ina226_begin(&reader, 10000U), NX_SUCCESS);
    wire.now = 110U;
    wire.registers[6] |= 0x0008U;
    EXPECT_EQ(example_bus_ina226_poll(&reader), NX_ERROR_TIMEOUT);
    example_bus_sample_t output = {};
    EXPECT_EQ(example_bus_take(&queue, 111U, 7U, 9U, 42U, &output),
              NX_ERROR_TIMEOUT);
    EXPECT_EQ(output.sequence, 0U);
}

TEST_F(MotorBusIna226, SourceI2cFailureClosesPreviouslyFreshCachedVoltage) {
    convert(30U);
    example_bus_sample_t output = {};
    ASSERT_EQ(example_bus_take(&queue, 31U, 7U, 9U, 42U, &output), NX_SUCCESS);
    wire.now = 40U;
    wire.fail = true;
    EXPECT_EQ(example_bus_ina226_begin(&reader, 10000U), NX_ERROR_NACK);
    EXPECT_EQ(example_bus_take(&queue, 41U, 7U, 9U, 42U, &output),
              NX_ERROR_NACK);
    EXPECT_EQ(output.sampled_at, 10U);
}

TEST_F(MotorBusIna226, FullQueueCompletesMeasurementAndAllowsTheNextBegin) {
    convert(30U);
    wire.now = 40U;
    convert(50U);
    wire.now = 60U;
    ASSERT_EQ(example_bus_ina226_begin(&reader, 10000U), NX_SUCCESS);
    wire.now = 70U;
    wire.registers[6] |= 0x0008U;
    ASSERT_EQ(example_bus_ina226_poll(&reader), NX_SUCCESS);
    EXPECT_FALSE(sensor.measuring);
    EXPECT_EQ(reader.sequence, 3U);
    EXPECT_EQ(queue.produced, 2U);
    EXPECT_EQ(queue.fault, NX_SUCCESS);
    example_bus_sample_t output = {};
    ASSERT_EQ(example_bus_take(&queue, 71U, 7U, 9U, 42U, &output), NX_SUCCESS);
    EXPECT_EQ(output.sequence, 1U);
    wire.now = 80U;
    convert(90U);
    EXPECT_EQ(queue.produced, 3U);
    ASSERT_EQ(example_bus_take(&queue, 91U, 7U, 9U, 42U, &output), NX_SUCCESS);
    EXPECT_EQ(output.sequence, 2U);
    ASSERT_EQ(example_bus_take(&queue, 92U, 7U, 9U, 42U, &output), NX_SUCCESS);
    EXPECT_EQ(output.sequence, 4U);
}

TEST_F(MotorBusIna226, NarrowSensorEpochCannotAliasAnotherFullEpoch) {
    ASSERT_EQ(
        example_bus_queue_initialize(&queue, 7U, UINT64_C(0x100000009), 42U),
        NX_SUCCESS);
    EXPECT_EQ(example_bus_ina226_initialize(&reader, &sensor, &queue, 100U),
              NX_ERROR_INVALID);
}
} /* namespace */
