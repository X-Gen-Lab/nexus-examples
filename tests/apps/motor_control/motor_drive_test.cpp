/**
 * \file            motor_drive_test.cpp
 * \brief           Actual drive consumer, kernel and public session lifecycle
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-24
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "control.h"
#include "observation.h"
#include <cmath>
#include <cstring>
#include <gtest/gtest.h>
#include <vector>

namespace {
struct Stage {
    nx_power_stage_session_t* active = nullptr;
    size_t calls = 0;
    bool refuse_stop = false;
    nx_result_t result = NX_SUCCESS;
    nx_power_stage_state_t facts = {1000000, 1000, 800,        50,    700,
                                    3,       0,    NX_SUCCESS, false, false};
    static nx_result_t start(void* context, nx_power_stage_session_t* session) {
        auto& self = *static_cast<Stage*>(context);
        ++self.calls;
        if (self.active != nullptr) {
            return NX_ERROR_BUSY;
        }
        const auto status = nx_request_admit(&session->base, NX_REQUEST_ACTIVE);
        if (status == NX_SUCCESS) {
            self.active = session;
        }
        return status;
    }
    static nx_result_t service(void* context) {
        ++static_cast<Stage*>(context)->calls;
        return NX_ERROR_BUSY;
    }
    static nx_result_t stop(void* context) {
        auto& self = *static_cast<Stage*>(context);
        ++self.calls;
        if (self.active == nullptr) {
            return NX_ERROR_STATE;
        }
        if (self.refuse_stop) {
            (void)nx_request_transition(&self.active->base,
                                        NX_REQUEST_QUARANTINED);
            return NX_ERROR_IO;
        }
        if (self.active != nullptr) {
            auto* ended = self.active;
            self.active = nullptr;
            nx_request_settle(&ended->base, self.result, 0);
        }
        return NX_SUCCESS;
    }
    static nx_result_t state(const void* context,
                             nx_power_stage_state_t* state) {
        *state = static_cast<const Stage*>(context)->facts;
        return NX_SUCCESS;
    }
};
const nx_power_stage_ops_t operations = {Stage::start, Stage::service,
                                         Stage::stop, Stage::state};

example_motor_drive_config_t configuration() {
    example_motor_drive_config_t config = {};
    config.motor = {7,   1, 0, 1000, 1100, 100, 1100, 0,    2,    100, 2,
                    100, 3, 5, 0.2F, 12,   10,  30,   0.9F, 0.3F, 100};
    for (size_t i = 0; i < 3; ++i) {
        config.current_zero_code[i] = 2048;
        config.current_a_per_code[i] = 0.01F;
    }
    config.bus_v_per_code = 0.01F;
    config.encoder_counts_per_turn = 4096;
    config.maximum_encoder_delta = 100;
    config.maximum_scan_span_us = 4;
    config.run_duration_us = 10000;
    config.startup_budget_us = 2000;
    config.commissioned = true;
    return config;
}

class MotorDrive : public testing::Test {
  protected:
    Stage stage;
    nx_power_stage_t port = {&operations, &stage};
    example_motor_drive_t drive = {};
    nx_power_stage_sample_t sample = {
        {2048, 2048, 2048}, 2400, 100, 0, 9, 1800, 1804, 1803, 2000, 3};
    void SetUp() override {
        const auto config = configuration();
        ASSERT_EQ(example_motor_drive_initialize(&drive, &port, &config),
                  NX_SUCCESS);
    }
    nx_result_t callback(nx_power_stage_plan_t* plan) {
        const auto status =
            stage.active->compute(stage.active->context, &sample, plan);
        if (status != NX_SUCCESS) {
            stage.result = status;
        }
        return status;
    }
};

TEST_F(MotorDrive, UncommissionedReferenceRejectsBeforePowerAdmission) {
    auto config = configuration();
    config.commissioned = false;
    ASSERT_EQ(example_motor_drive_initialize(&drive, &port, &config),
              NX_SUCCESS);
    EXPECT_EQ(example_motor_drive_start(&drive, 1000), NX_ERROR_STATE);
    EXPECT_EQ(stage.calls, 0U);
    EXPECT_EQ(stage.active, nullptr);
}

TEST_F(MotorDrive, FirstEncoderObservationIsExplicitWarmupThenRealKernelRuns) {
    ASSERT_EQ(example_motor_drive_start(&drive, 1000), NX_SUCCESS);
    nx_power_stage_plan_t plan = {};
    ASSERT_EQ(callback(&plan), NX_SUCCESS);
    EXPECT_FALSE(drive.context.motor.have_sample);
    EXPECT_EQ(plan.duty_ticks[0], 375U);
    ++sample.sequence;
    sample.sample_at += 1000;
    sample.observed_at += 1000;
    sample.angle_at += 1000;
    sample.planned_apply_at += 1000;
    ASSERT_EQ(callback(&plan), NX_SUCCESS);
    EXPECT_TRUE(drive.context.motor.have_sample);
    EXPECT_EQ(drive.context.motor.config.clock_epoch, 9U);
    EXPECT_FALSE(drive.context.motor.track_pending);
    for (const auto duty : plan.duty_ticks) {
        EXPECT_GE(duty, 50U);
        EXPECT_LE(duty, 700U);
    }
    EXPECT_EQ(example_motor_drive_stop(&drive), NX_SUCCESS);
    EXPECT_FALSE(drive.borrowed);
}

TEST_F(MotorDrive, ExcessiveSequentialScanDoesNotProduceAVoltagePlan) {
    ASSERT_EQ(example_motor_drive_start(&drive, 1000), NX_SUCCESS);
    sample.scan_span_us = 100;
    nx_power_stage_plan_t plan = {{11, 22, 33}};
    const auto before = plan;
    EXPECT_NE(callback(&plan), NX_SUCCESS);
    EXPECT_EQ(std::memcmp(&before, &plan, sizeof(plan)), 0);
    EXPECT_NE(example_motor_drive_stop(&drive), NX_SUCCESS);
}

TEST_F(MotorDrive, FailedStopRetainsOriginalContextAndRetriesFinalWithdrawal) {
    ASSERT_EQ(example_motor_drive_start(&drive, 1000), NX_SUCCESS);
    auto* const context = stage.active->context;
    stage.refuse_stop = true;
    EXPECT_EQ(example_motor_drive_stop(&drive), NX_ERROR_BUSY);
    EXPECT_TRUE(drive.borrowed);
    EXPECT_EQ(stage.active->context, context);
    EXPECT_EQ(nx_request_state(&drive.session.base), NX_REQUEST_QUARANTINED);
    stage.refuse_stop = false;
    EXPECT_EQ(example_motor_drive_stop(&drive), NX_ERROR_IO);
    EXPECT_FALSE(drive.borrowed);
    EXPECT_EQ(stage.active, nullptr);
    EXPECT_EQ(example_motor_drive_start(&drive, 2000), NX_ERROR_STATE);
}

TEST_F(MotorDrive, ElectricalPhaseRetainsCountResolutionAfterManyTurns) {
    auto config = configuration();
    config.encoder_counts_per_turn = 4096;
    ASSERT_EQ(example_motor_drive_initialize(&drive, &port, &config),
              NX_SUCCESS);
    ASSERT_EQ(example_motor_drive_start(&drive, 1000), NX_SUCCESS);
    nx_power_stage_plan_t plan = {};
    ASSERT_EQ(callback(&plan), NX_SUCCESS);
    drive.context.encoder.position_counts = INT64_C(4096) * 1000000000 + 17;
    ++sample.sequence;
    sample.sample_at += 1000;
    sample.observed_at += 1000;
    sample.angle_at += 1000;
    sample.planned_apply_at += 1000;
    ASSERT_EQ(callback(&plan), NX_SUCCESS);
    EXPECT_NEAR(drive.context.last_plan.sample_electrical_angle_rad,
                17 * static_cast<float>(2 * std::acos(-1.0) / 4096) *
                    static_cast<float>(config.motor.pole_pairs),
                1e-5F);
    EXPECT_EQ(example_motor_drive_stop(&drive), NX_SUCCESS);
}

TEST_F(MotorDrive, ReinitializationCannotEraseAnAcceptedContextLoan) {
    ASSERT_EQ(example_motor_drive_start(&drive, 1000), NX_SUCCESS);
    const auto before = drive;
    const auto config = configuration();
    EXPECT_EQ(example_motor_drive_initialize(&drive, &port, &config),
              NX_ERROR_STATE);
    EXPECT_EQ(std::memcmp(&drive, &before, sizeof(drive)), 0);
    EXPECT_EQ(example_motor_drive_stop(&drive), NX_SUCCESS);
}

TEST_F(MotorDrive, AlreadySettledProviderPreservesItsActualFinalResult) {
    ASSERT_EQ(example_motor_drive_start(&drive, 1000), NX_SUCCESS);
    stage.active = nullptr;
    nx_request_settle(&drive.session.base, NX_ERROR_TIMEOUT, 0);
    EXPECT_EQ(example_motor_drive_stop(&drive), NX_ERROR_TIMEOUT);
    EXPECT_FALSE(drive.borrowed);
}

TEST_F(MotorDrive, ZeroVoltageUsesTheActualAsymmetricHardwareWindow) {
    stage.facts.maximum_duty_ticks = 400;
    ASSERT_EQ(example_motor_drive_start(&drive, 1000), NX_SUCCESS);
    nx_power_stage_plan_t plan = {};
    ASSERT_EQ(callback(&plan), NX_SUCCESS);
    EXPECT_EQ(plan.duty_ticks[0], 225U);
    ++sample.sequence;
    sample.sample_at += 1000;
    sample.observed_at += 1000;
    sample.angle_at += 1000;
    sample.planned_apply_at += 1000;
    ASSERT_EQ(callback(&plan), NX_SUCCESS);
    EXPECT_EQ(plan.duty_ticks[0], 225U);
    EXPECT_EQ(plan.duty_ticks[1], 225U);
    EXPECT_EQ(plan.duty_ticks[2], 225U);
    EXPECT_EQ(example_motor_drive_stop(&drive), NX_SUCCESS);
}

TEST_F(MotorDrive, CommonModeMappingPreservesTheLimitedVoltageDirection) {
    auto config = configuration();
    config.motor.minimum_duty = 0.05F;
    config.motor.kp_q = 30;
    config.iq_a = 3;
    ASSERT_EQ(example_motor_drive_initialize(&drive, &port, &config),
              NX_SUCCESS);
    stage.facts.maximum_duty_ticks = 400;
    ASSERT_EQ(example_motor_drive_start(&drive, 1000), NX_SUCCESS);
    nx_power_stage_plan_t plan = {};
    ASSERT_EQ(callback(&plan), NX_SUCCESS);
    ++sample.sequence;
    sample.sample_at += 1000;
    sample.observed_at += 1000;
    sample.angle_at += 1000;
    sample.planned_apply_at += 1000;
    ASSERT_EQ(callback(&plan), NX_SUCCESS);
    for (const auto duty : plan.duty_ticks) {
        EXPECT_GE(duty, 50U);
        EXPECT_LE(duty, 400U);
    }
    EXPECT_TRUE(drive.context.last_plan.limited);
    EXPECT_NEAR(drive.context.last_plan.planned_voltage.d, 0, 0.03F);
    EXPECT_GT(drive.context.last_plan.planned_voltage.q, 0);
    EXPECT_FALSE(drive.context.motor.track_pending);
    EXPECT_EQ(example_motor_drive_stop(&drive), NX_SUCCESS);
}

TEST_F(MotorDrive, FiniteTargetWithdrawsBeforeItsLastPlannedApplyBoundary) {
    ASSERT_EQ(example_motor_drive_start(&drive, 1000), NX_SUCCESS);
    EXPECT_EQ(example_motor_drive_step(&drive, 10000), NX_SUCCESS);
    EXPECT_FALSE(drive.borrowed);
    EXPECT_EQ(stage.active, nullptr);
}
struct ObservationSink {
    std::vector<example_motor_observation_t> records;
    nx_result_t status = NX_SUCCESS;
    static nx_result_t accept(void* context,
                              const example_motor_observation_t* record) {
        auto& self = *static_cast<ObservationSink*>(context);
        if (self.status == NX_SUCCESS) {
            self.records.push_back(*record);
        }
        return self.status;
    }
};

TEST(MotorObservation, FullChannelNeverOverwritesAndExposesFinalLoss) {
    example_motor_observations_t channel = {};
    nx_power_stage_sample_t sample = {};
    nx_power_stage_plan_t plan = {{50, 60, 70}};
    sample.clock_epoch = UINT64_C(0x100000001);
    sample.sample_at = 123;
    sample.observed_at = 127;
    sample.angle_at = 126;
    sample.planned_apply_at = 200;
    for (uint64_t sequence = 0; sequence < 7; ++sequence) {
        sample.sequence = sequence;
        example_motor_observation_publish(&channel, &sample, &plan, NX_SUCCESS);
    }
    ObservationSink sink;
    uint32_t delivered = 0;
    uint32_t dropped = 0;
    EXPECT_EQ(example_motor_observation_drain(&channel, ObservationSink::accept,
                                              &sink, 8, &delivered, &dropped),
              NX_ERROR_EMPTY);
    ASSERT_EQ(sink.records.size(), 4U);
    EXPECT_EQ(delivered, 4U);
    EXPECT_EQ(dropped, 3U);
    for (size_t i = 0; i < sink.records.size(); ++i) {
        EXPECT_EQ(sink.records[i].sample.sequence, i);
        EXPECT_EQ(sink.records[i].sample.clock_epoch, UINT64_C(0x100000001));
        EXPECT_EQ(sink.records[i].sample.sample_at, 123U);
        EXPECT_EQ(sink.records[i].sample.observed_at, 127U);
        EXPECT_EQ(sink.records[i].sample.angle_at, 126U);
        EXPECT_EQ(sink.records[i].sample.planned_apply_at, 200U);
        EXPECT_EQ(sink.records[i].plan.duty_ticks[2], 70U);
    }
    sample.sequence = 7;
    example_motor_observation_publish(&channel, &sample, nullptr, NX_ERROR_IO);
    EXPECT_EQ(example_motor_observation_drain(&channel, ObservationSink::accept,
                                              &sink, 1, &delivered, &dropped),
              NX_SUCCESS);
    EXPECT_EQ(sink.records.back().sample.sequence, 7U);
    EXPECT_EQ(sink.records.back().result, NX_ERROR_IO);
    EXPECT_EQ(sink.records.back().plan.duty_ticks[0], 0U);
}

TEST(MotorObservation, BusySinkRetainsWholeRecordAndBudgetIsExact) {
    example_motor_observations_t channel = {};
    const nx_power_stage_sample_t sample = {};
    const nx_power_stage_plan_t plan = {{50, 60, 70}};
    example_motor_observation_publish(&channel, &sample, &plan, NX_SUCCESS);
    example_motor_observation_publish(&channel, &sample, &plan, NX_SUCCESS);
    ObservationSink sink;
    sink.status = NX_ERROR_BUSY;
    uint32_t delivered = 99;
    uint32_t dropped = 99;
    EXPECT_EQ(example_motor_observation_drain(&channel, ObservationSink::accept,
                                              &sink, 1, &delivered, &dropped),
              NX_ERROR_BUSY);
    EXPECT_EQ(delivered, 0U);
    EXPECT_EQ(channel.consumed, 0U);
    sink.status = NX_SUCCESS;
    EXPECT_EQ(example_motor_observation_drain(&channel, ObservationSink::accept,
                                              &sink, 1, &delivered, &dropped),
              NX_SUCCESS);
    EXPECT_EQ(delivered, 1U);
    EXPECT_EQ(sink.records.size(), 1U);
    EXPECT_EQ(channel.consumed, 1U);
}

TEST(MotorObservation, CounterWrapMaintainsDistanceAndLossSaturates) {
    example_motor_observations_t channel = {};
    channel.published = UINT32_MAX - 1;
    channel.consumed = UINT32_MAX - 1;
    channel.dropped = UINT32_MAX - 1;
    nx_power_stage_sample_t sample = {};
    const nx_power_stage_plan_t plan = {{50, 60, 70}};
    for (uint64_t i = 0; i < 7; ++i) {
        sample.sequence = i;
        example_motor_observation_publish(&channel, &sample, &plan, NX_SUCCESS);
    }
    ObservationSink sink;
    uint32_t delivered = 0;
    uint32_t dropped = 0;
    EXPECT_EQ(example_motor_observation_drain(&channel, ObservationSink::accept,
                                              &sink, 8, &delivered, &dropped),
              NX_ERROR_EMPTY);
    ASSERT_EQ(sink.records.size(), 4U);
    EXPECT_EQ(sink.records.front().sample.sequence, 0U);
    EXPECT_EQ(sink.records.back().sample.sequence, 3U);
    EXPECT_EQ(dropped, UINT32_MAX);
    EXPECT_EQ(channel.consumed, 2U);
}

TEST_F(MotorDrive, FullObservationChannelCannotBlockActualKernel) {
    ASSERT_EQ(example_motor_drive_start(&drive, 1000), NX_SUCCESS);
    nx_power_stage_plan_t plan = {};
    for (unsigned i = 0; i < 7; ++i) {
        ASSERT_EQ(callback(&plan), NX_SUCCESS);
        ++sample.sequence;
        sample.sample_at += 1000;
        sample.observed_at += 1000;
        sample.angle_at += 1000;
        sample.planned_apply_at += 1000;
    }
    /* The independently synchronized channel does not read context. */
    ObservationSink sink;
    uint32_t delivered = 0;
    uint32_t dropped = 0;
    EXPECT_EQ(example_motor_observation_drain(&drive.observations,
                                              ObservationSink::accept, &sink, 8,
                                              &delivered, &dropped),
              NX_ERROR_EMPTY);
    EXPECT_EQ(delivered, 4U);
    EXPECT_EQ(dropped, 3U);
    EXPECT_TRUE(drive.borrowed);
    EXPECT_EQ(example_motor_drive_stop(&drive), NX_SUCCESS);
}

TEST_F(MotorDrive, ReuseRequiresSettlementAndCompleteObservationDrain) {
    ASSERT_EQ(example_motor_drive_start(&drive, 1000), NX_SUCCESS);
    nx_power_stage_plan_t plan = {};
    ASSERT_EQ(callback(&plan), NX_SUCCESS);
    const auto config = configuration();
    EXPECT_EQ(example_motor_drive_initialize(&drive, &port, &config),
              NX_ERROR_STATE);
    EXPECT_EQ(example_motor_drive_stop(&drive), NX_SUCCESS);
    EXPECT_EQ(example_motor_drive_initialize(&drive, &port, &config),
              NX_ERROR_STATE);
    ObservationSink sink;
    uint32_t delivered = 0;
    uint32_t dropped = 0;
    ASSERT_EQ(example_motor_observation_drain(&drive.observations,
                                              ObservationSink::accept, &sink, 8,
                                              &delivered, &dropped),
              NX_ERROR_EMPTY);
    EXPECT_EQ(example_motor_drive_initialize(&drive, &port, &config),
              NX_SUCCESS);
    EXPECT_EQ(drive.observations.published, 0U);
}
} /* namespace */
