/**
 * \file            motor_drive_test.cpp
 * \brief           Actual drive consumer, kernel and public session lifecycle
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-24
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "as5047p_wire.h"
#include "control.h"
#include "observation.h"
#include <cmath>
#include <cstring>
#include <gtest/gtest.h>
#include <vector>

namespace {
nx_power_stage_plan_t duty_plan(uint32_t a, uint32_t b, uint32_t c) {
    nx_power_stage_plan_t plan{};
    plan.duty_ticks[0] = a;
    plan.duty_ticks[1] = b;
    plan.duty_ticks[2] = c;
    return plan;
}
struct Stage {
    nx_power_stage_session_t* active = nullptr;
    size_t calls = 0;
    bool refuse_stop = false;
    nx_result_t result = NX_SUCCESS;
    nx_power_stage_state_t facts = [] {
        nx_power_stage_state_t selected{};
        selected.clock_epoch = 9;
        selected.tick_hz = 1000000;
        selected.period_ticks = 1000;
        selected.trigger_ticks = 800;
        selected.minimum_duty_ticks = 50;
        selected.maximum_duty_ticks = 700;
        selected.scan_span_us = 3;
        selected.deadtime_ticks = 1;
        selected.settling_ticks = 25;
        selected.aperture_ticks = 3;
        selected.conversion_ticks = 3;
        selected.irq_ticks = 20;
        selected.compute_ticks = 100;
        selected.commit_before_ticks = 990;
        selected.current_count = 3;
        selected.current_phase_mask = 7;
        selected.bus_available = true;
        selected.encoder_available = true;
        return selected;
    }();
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
    auto config = example_motor_drive_reference();
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
    config.startup_budget_us = 6000;
    config.commissioned = true;
    return config;
}

class MotorDrive : public testing::Test {
  protected:
    Stage stage;
    nx_power_stage_t port = {&operations, &stage};
    example_motor_drive_t drive = {};
    nx_power_stage_sample_t sample = [] {
        nx_power_stage_sample_t result{};
        for (auto& code : result.current_raw) {
            code = 2048;
        }
        result.bus_raw = 2400;
        result.current_count = 3;
        result.current_phase_mask = 7;
        result.bus_valid = true;
        result.encoder_valid = true;
        result.encoder_count = 100;
        result.clock_epoch = 9;
        result.sample_at = 1800;
        result.observed_at = 1804;
        result.angle_at = 1803;
        result.planned_apply_at = 2000;
        result.scan_span_us = 3;
        result.trigger_ticks = 800;
        return result;
    }();
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
        if (status == NX_SUCCESS && drive.borrowed) {
            const auto progress =
                example_motor_drive_step(&drive, sample.observed_at);
            if (progress != NX_ERROR_BUSY && progress != NX_SUCCESS) {
                return progress;
            }
        }
        return status;
    }
    nx_result_t cycles(unsigned count) {
        nx_power_stage_plan_t plan{};
        for (unsigned i = 0; i < count; ++i) {
            const auto result = callback(&plan);
            if (result != NX_SUCCESS) {
                return result;
            }
            ++sample.sequence;
            ++sample.plan_sequence;
            sample.sample_at += 1000;
            sample.observed_at += 1000;
            sample.angle_at += 1000;
            sample.planned_apply_at += 1000;
        }
        return NX_SUCCESS;
    }
};

TEST_F(MotorDrive, ActualSpiRotorFeedsMotorWithoutAQuadratureEncoder) {
    example_test::As5047pWire wire;
    const nx_as5047p_config_t sensor_config = {
        &wire.endpoint,
        {example_test::As5047pWire::time, &wire},
        1U,
        9U,
        false};
    nx_as5047p_t sensor{};
    ASSERT_EQ(nx_as5047p_init(&sensor, &sensor_config, 10000U), NX_SUCCESS);
    example_rotor_queue_t rotor{};
    ASSERT_EQ(example_rotor_queue_initialize(&rotor, 1U, 9U, 2000U),
              NX_SUCCESS);
    example_rotor_reader_t reader{};
    ASSERT_EQ(example_rotor_reader_initialize(&reader, &sensor, &rotor, false,
                                              0, 1000U, 2000U),
              NX_SUCCESS);
    auto config = configuration();
    config.rotor = &rotor;
    config.motor.maximum_angle_age_us = 1500U;
    config.motor.maximum_prediction_us = 1500U;
    stage.facts.encoder_available = false;
    sample.encoder_valid = false;
    sample.encoder_count = UINT32_MAX;
    sample.angle_at = 0;
    sample.sample_at = 2800U;
    sample.observed_at = 2804U;
    sample.planned_apply_at = 3000U;
    ASSERT_EQ(example_motor_drive_initialize(&drive, &port, &config),
              NX_SUCCESS);
    ASSERT_EQ(example_motor_drive_start(&drive, 1000U), NX_SUCCESS);
    wire.now = 1700U;
    EXPECT_EQ(example_rotor_poll(&reader, 10000U), NX_ERROR_BUSY);
    wire.angle = 1001U;
    wire.now = 2700U;
    ASSERT_EQ(example_rotor_poll(&reader, 10000U), NX_SUCCESS);
    const auto transfers = wire.transfers;
    ASSERT_EQ(cycles(2), NX_SUCCESS);
    EXPECT_EQ(wire.transfers, transfers);
    EXPECT_TRUE(drive.context.fast.motor.have_sample);
    EXPECT_FALSE(drive.context.fast.motor.track_pending);
    EXPECT_EQ(drive.context.fast.motor.last_sample_time_us, 3800U);
    EXPECT_EQ(rotor.cached.angle_at, 2700U);
    const float scale = 6.283185307179586F / 16384.0F;
    const float angle = (1001.0F + 1.1F) * scale * 7.0F;
    EXPECT_NEAR(drive.context.last_plan.sample_electrical_angle_rad, angle,
                0.00001F);
    EXPECT_EQ(drive.context.last_plan.sequence, 1U);
    EXPECT_EQ(example_motor_drive_stop(&drive), NX_SUCCESS);
    EXPECT_FALSE(drive.borrowed);
}

TEST_F(MotorDrive, QualifiedTaskLimitsFeedActualAxisAndFiniteFastReferences) {
    auto config = configuration();
    config.run_duration_us = 30000;
    config.iq_a = 2;
    ASSERT_EQ(example_motor_drive_initialize(&drive, &port, &config),
              NX_SUCCESS);
    ASSERT_EQ(example_motor_drive_start(&drive, 1000), NX_SUCCESS);
    ASSERT_EQ(cycles(6), NX_SUCCESS);
    example_motor_drive_limits_t limits{};
    limits.current = {-0.25F, 0.25F};
    limits.sampled_at = sample.observed_at;
    limits.valid_until = sample.observed_at + 10000;
    limits.clock_domain = 1;
    limits.epoch = 9;
    limits.revision = 1;
    ASSERT_EQ(
        example_motor_drive_set_limits(&drive, &limits, sample.observed_at),
        NX_SUCCESS);
    ASSERT_EQ(cycles(3), NX_SUCCESS);
    EXPECT_FLOAT_EQ(drive.axis_output.current_a, 0.25F);
    EXPECT_FLOAT_EQ(drive.context.fast.active.current.iq_a, 0.25F);
    EXPECT_LE(drive.last_reference_until, limits.valid_until);
    const auto before = drive.limits;
    ++limits.revision;
    limits.valid_until = sample.observed_at;
    EXPECT_EQ(
        example_motor_drive_set_limits(&drive, &limits, sample.observed_at),
        NX_ERROR_TIMEOUT);
    EXPECT_EQ(std::memcmp(&drive.limits, &before, sizeof(before)), 0);
    EXPECT_EQ(example_motor_drive_stop(&drive), NX_SUCCESS);
}

TEST_F(MotorDrive, LimitAuthorityCannotShortenExistingAcceptedReference) {
    auto config = configuration();
    config.run_duration_us = 30000;
    ASSERT_EQ(example_motor_drive_initialize(&drive, &port, &config),
              NX_SUCCESS);
    ASSERT_EQ(example_motor_drive_start(&drive, 1000), NX_SUCCESS);
    ASSERT_EQ(cycles(6), NX_SUCCESS);
    example_motor_drive_limits_t limits{};
    limits.current = {-1, 1};
    limits.sampled_at = sample.observed_at;
    limits.valid_until = sample.observed_at + 1;
    limits.clock_domain = 1;
    limits.epoch = 9;
    limits.revision = 1;
    EXPECT_EQ(
        example_motor_drive_set_limits(&drive, &limits, sample.observed_at),
        NX_ERROR_BUSY);
    EXPECT_FALSE(drive.limits_active);
    EXPECT_EQ(example_motor_drive_stop(&drive), NX_SUCCESS);
}

TEST_F(MotorDrive, ShortNewCommandCannotHideOlderFastAuthorityFromBudget) {
    auto config = configuration();
    config.run_duration_us = 30000;
    config.reference_lifetime_us = 10000;
    ASSERT_EQ(example_motor_drive_initialize(&drive, &port, &config),
              NX_SUCCESS);
    ASSERT_EQ(example_motor_drive_start(&drive, 1000), NX_SUCCESS);
    ASSERT_EQ(cycles(6), NX_SUCCESS);
    const auto old_expiry = drive.last_reference_until;
    nx_axis_command_t short_command{};
    short_command.mode = NX_AXIS_CURRENT;
    short_command.clock_domain = 1;
    short_command.epoch = 9;
    short_command.sequence = 2;
    short_command.received_at = sample.observed_at;
    short_command.valid_until = sample.observed_at + 6000;
    ASSERT_EQ(
        example_motor_drive_accept(&drive, &short_command, sample.observed_at),
        NX_SUCCESS);
    ASSERT_EQ(cycles(2), NX_SUCCESS);
    EXPECT_GE(drive.last_reference_until, old_expiry);
    example_motor_drive_limits_t limits{};
    limits.current = {-1, 1};
    limits.sampled_at = sample.observed_at;
    limits.valid_until = old_expiry - 1;
    limits.clock_domain = 1;
    limits.epoch = 9;
    limits.revision = 1;
    EXPECT_EQ(
        example_motor_drive_set_limits(&drive, &limits, sample.observed_at),
        NX_ERROR_BUSY);
    EXPECT_EQ(example_motor_drive_stop(&drive), NX_SUCCESS);
}

TEST(MotorServoComposition, ReferenceIsUncommissionedAndMotionNeedsFeedback) {
    const auto config = example_motor_drive_reference();
    EXPECT_FALSE(config.commissioned);
    EXPECT_GT(config.axis_parameters.revision, 0U);
    EXPECT_GT(config.reference_lifetime_us, 0U);
    Stage stage;
    nx_power_stage_t port{&operations, &stage};
    example_motor_drive_t drive{};
    ASSERT_EQ(example_motor_drive_initialize(&drive, &port, &config),
              NX_SUCCESS);
    nx_axis_command_t command{};
    command.mode = NX_AXIS_VELOCITY;
    command.clock_domain = 1;
    command.epoch = 9;
    command.sequence = 1;
    command.received_at = 1000;
    command.valid_until = 9000;
    command.velocity_rad_s = 1;
    EXPECT_EQ(example_motor_drive_accept(&drive, &command, 1000),
              NX_ERROR_BUSY);
    EXPECT_EQ(stage.calls, 0U);
}

TEST_F(MotorDrive, ActualAxisVelocityDecisionReachesIrqCurrentReference) {
    auto config = example_motor_drive_reference();
    config.commissioned = true;
    config.initial_mode = NX_AXIS_VELOCITY;
    config.initial_velocity_rad_s = 2;
    config.run_duration_us = 30000;
    ASSERT_EQ(example_motor_drive_initialize(&drive, &port, &config),
              NX_SUCCESS);
    ASSERT_EQ(example_motor_drive_start(&drive, 1000), NX_SUCCESS);
    for (unsigned cycle = 0; cycle < 6; ++cycle) {
        nx_power_stage_plan_t plan{};
        ASSERT_EQ(callback(&plan), NX_SUCCESS);
        ASSERT_EQ(example_motor_drive_step(&drive, sample.observed_at),
                  NX_ERROR_BUSY);
        ++sample.sequence;
        ++sample.plan_sequence;
        sample.sample_at += 1000;
        sample.angle_at += 1000;
        sample.observed_at += 1000;
        sample.planned_apply_at += 1000;
    }
    EXPECT_EQ(drive.axis_output.state, NX_AXIS_RUNNING);
    EXPECT_GT(drive.axis_output.current_a, 0);
    EXPECT_GT(drive.context.fast.active.current.iq_a, 0);
    EXPECT_GT(drive.context.fast.active.generation, 1U);
    EXPECT_EQ(example_motor_drive_stop(&drive), NX_SUCCESS);
}

TEST_F(MotorDrive, MismatchedCalibrationCannotAcquirePower) {
    auto config = example_motor_drive_reference();
    config.commissioned = true;
    ++config.calibration_identity;
    EXPECT_EQ(example_motor_drive_initialize(&drive, &port, &config),
              NX_ERROR_INVALID);
    EXPECT_EQ(stage.calls, 0U);
}

TEST_F(MotorDrive, TwoMeasuredPhasesNeedNoInventedThirdSensorCalibration) {
    auto config = configuration();
    config.current_zero_code[2] = 0;
    config.current_a_per_code[2] = 0;
    config.topology = NX_MODULATION_TWO_SHUNT;
    config.phase_samples = 2;
    EXPECT_EQ(example_motor_drive_initialize(&drive, &port, &config),
              NX_SUCCESS);
}

TEST_F(MotorDrive, ColdInvalidPositionFormatNeverAcquiresHardware) {
    auto config = configuration();
    config.initial_mode = NX_AXIS_POSITION;
    config.initial_velocity_rad_s = 1;
    EXPECT_EQ(example_motor_drive_initialize(&drive, &port, &config),
              NX_ERROR_INVALID);
    EXPECT_EQ(stage.calls, 0U);
}

TEST_F(MotorDrive, ExplicitWrongEpochIsNotRepairedByStartup) {
    auto config = configuration();
    config.motor.clock_epoch = 8;
    ASSERT_EQ(example_motor_drive_initialize(&drive, &port, &config),
              NX_SUCCESS);
    EXPECT_EQ(example_motor_drive_start(&drive, 1000), NX_ERROR_STATE);
    EXPECT_EQ(stage.active, nullptr);
}

TEST_F(MotorDrive, LifetimeCannotSilentlyExceedLocalFloatCountPrecision) {
    auto config = configuration();
    config.run_duration_us = UINT32_MAX;
    EXPECT_EQ(example_motor_drive_initialize(&drive, &port, &config),
              NX_ERROR_INVALID);
    EXPECT_EQ(stage.calls, 0U);
}

TEST_F(MotorDrive, RunningReferenceCannotOutliveOriginalMotionCommand) {
    auto config = configuration();
    config.run_duration_us = 30000;
    config.axis.maximum_step_us = 1000;
    ASSERT_EQ(example_motor_drive_initialize(&drive, &port, &config),
              NX_SUCCESS);
    ASSERT_EQ(example_motor_drive_start(&drive, 1000), NX_SUCCESS);
    nx_power_stage_plan_t plan{};
    for (unsigned i = 0; i < 6; ++i) {
        ASSERT_EQ(callback(&plan), NX_SUCCESS);
        ++sample.sequence;
        ++sample.plan_sequence;
        sample.sample_at += 1000;
        sample.observed_at += 1000;
        sample.angle_at += 1000;
        sample.planned_apply_at += 1000;
    }
    nx_axis_command_t command{};
    command.mode = NX_AXIS_CURRENT;
    command.clock_domain = 1;
    command.epoch = 9;
    command.sequence = 2;
    command.received_at = sample.observed_at;
    command.valid_until = sample.observed_at + 5500;
    command.current_a = 0.5F;
    ASSERT_EQ(example_motor_drive_accept(&drive, &command, sample.observed_at),
              NX_SUCCESS);
    for (unsigned i = 0; i < 4; ++i) {
        ASSERT_EQ(callback(&plan), NX_SUCCESS);
        ++sample.sequence;
        ++sample.plan_sequence;
        sample.sample_at += 1000;
        sample.observed_at += 1000;
        sample.angle_at += 1000;
        sample.planned_apply_at += 1000;
    }
    const auto& published =
        drive.reference_slots[(drive.references.produced - 1U) & 3U];
    ASSERT_EQ(drive.axis_output.state, NX_AXIS_RUNNING);
    EXPECT_EQ(published.received_at, command.received_at);
    EXPECT_LE(published.current.valid_until_us, command.valid_until);
    EXPECT_EQ(example_motor_drive_stop(&drive), NX_SUCCESS);
}

TEST_F(MotorDrive, QualifiedExternalBusReplacesAbsentAdcBusSource) {
    example_bus_queue_t bus{};
    ASSERT_EQ(example_bus_queue_initialize(&bus, 1, 9, 47), NX_SUCCESS);
    const example_bus_sample_t observed = {1000, 1001, 10000, 9, 1, 47, 1, 24};
    ASSERT_EQ(example_bus_publish(&bus, &observed), NX_SUCCESS);
    auto config = configuration();
    config.bus = &bus;
    config.bus_source_id = 47;
    stage.facts.bus_available = false;
    sample.bus_valid = false;
    sample.bus_raw = 0;
    ASSERT_EQ(example_motor_drive_initialize(&drive, &port, &config),
              NX_SUCCESS);
    ASSERT_EQ(example_motor_drive_start(&drive, 1001), NX_SUCCESS);
    nx_power_stage_plan_t plan{};
    ASSERT_EQ(callback(&plan), NX_SUCCESS);
    ++sample.sequence;
    ++sample.plan_sequence;
    sample.sample_at += 1000;
    sample.observed_at += 1000;
    sample.angle_at += 1000;
    sample.planned_apply_at += 1000;
    ASSERT_EQ(callback(&plan), NX_SUCCESS);
    EXPECT_TRUE(drive.context.fast.motor.have_sample);
    EXPECT_EQ(example_motor_drive_stop(&drive), NX_SUCCESS);
}

TEST_F(MotorDrive, ExternalBusMustRemainValidThroughPlannedApply) {
    example_bus_queue_t bus{};
    ASSERT_EQ(example_bus_queue_initialize(&bus, 1, 9, 47), NX_SUCCESS);
    const example_bus_sample_t observed = {1000, 1001, 2000, 9, 1, 47, 1, 24};
    ASSERT_EQ(example_bus_publish(&bus, &observed), NX_SUCCESS);
    auto config = configuration();
    config.bus = &bus;
    config.bus_source_id = 47;
    sample.bus_valid = false;
    sample.bus_raw = 0;
    ASSERT_EQ(example_motor_drive_initialize(&drive, &port, &config),
              NX_SUCCESS);
    ASSERT_EQ(example_motor_drive_start(&drive, 1001), NX_SUCCESS);
    nx_power_stage_plan_t plan{};
    EXPECT_EQ(callback(&plan), NX_ERROR_TIMEOUT);
    EXPECT_TRUE(drive.borrowed);
    EXPECT_EQ(example_motor_drive_stop(&drive), NX_ERROR_TIMEOUT);
}

TEST_F(MotorDrive, CenteredInitialPlanUsesUnfoldedWrappingPulseCoordinates) {
    stage.facts.center_aligned = true;
    ASSERT_EQ(example_motor_drive_start(&drive, 1000), NX_SUCCESS);
    const auto& initial = stage.active->initial;
    for (size_t phase = 0; phase < 3; ++phase) {
        EXPECT_EQ(initial.duty_ticks[phase] % 2, 0U);
        EXPECT_EQ(initial.start_ticks[phase],
                  stage.facts.period_ticks - initial.duty_ticks[phase] / 2);
    }
    EXPECT_EQ(example_motor_drive_stop(&drive), NX_SUCCESS);
}

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
    EXPECT_FALSE(drive.context.fast.motor.have_sample);
    EXPECT_EQ(plan.duty_ticks[0], 375U);
    ++sample.sequence;
    ++sample.plan_sequence;
    sample.sample_at += 1000;
    sample.observed_at += 1000;
    sample.angle_at += 1000;
    sample.planned_apply_at += 1000;
    ASSERT_EQ(callback(&plan), NX_SUCCESS);
    EXPECT_TRUE(drive.context.fast.motor.have_sample);
    EXPECT_EQ(drive.context.fast.motor.config.clock_epoch, 9U);
    EXPECT_FALSE(drive.context.fast.motor.track_pending);
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
    nx_power_stage_plan_t plan{};
    plan.duty_ticks[0] = 11;
    plan.duty_ticks[1] = 22;
    plan.duty_ticks[2] = 33;
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
    ++sample.plan_sequence;
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
    ++sample.plan_sequence;
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
    ++sample.plan_sequence;
    sample.sample_at += 1000;
    sample.observed_at += 1000;
    sample.angle_at += 1000;
    sample.planned_apply_at += 1000;
    ASSERT_EQ(callback(&plan), NX_SUCCESS);
    /* Startup first establishes real encoder/feedback intervals, then Task
     * Axis publishes the current target for a subsequent IRQ adoption. */
    for (unsigned i = 0; i < 3; ++i) {
        ++sample.sequence;
        ++sample.plan_sequence;
        sample.sample_at += 1000;
        sample.observed_at += 1000;
        sample.angle_at += 1000;
        sample.planned_apply_at += 1000;
        ASSERT_EQ(callback(&plan), NX_SUCCESS);
    }
    for (const auto duty : plan.duty_ticks) {
        EXPECT_GE(duty, 50U);
        EXPECT_LE(duty, 400U);
    }
    EXPECT_TRUE(drive.context.last_plan.limited);
    EXPECT_NEAR(drive.context.last_plan.planned_voltage.d, 0, 0.03F);
    EXPECT_GT(drive.context.last_plan.planned_voltage.q, 0);
    EXPECT_FALSE(drive.context.fast.motor.track_pending);
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

TEST(MotorObservation, PublicationNeverRetainsCallbackOrAlgorithmPointers) {
    example_motor_observations_t channel = {};
    nx_power_stage_dc_capture_t captured = {{2000, 2100}, {10, 20}, {3, 1}, 7};
    nx_power_stage_dc_plan_t descriptor = {{30, 40}, {3, 1}};
    nx_power_stage_sample_t sample = {};
    nx_power_stage_plan_t plan = {};
    sample.dc = &captured;
    plan.dc = &descriptor;
    example_motor_observation_publish(&channel, &sample, &plan, NX_SUCCESS);
    captured.code[0] = 0;
    descriptor.sample_ticks[0] = 0;
    ObservationSink sink;
    uint32_t delivered = 0, dropped = 0;
    ASSERT_EQ(example_motor_observation_drain(&channel, ObservationSink::accept,
                                              &sink, 1, &delivered, &dropped),
              NX_SUCCESS);
    ASSERT_EQ(sink.records.size(), 1U);
    EXPECT_EQ(sink.records[0].sample.dc, nullptr);
    EXPECT_EQ(sink.records[0].plan.dc, nullptr);
}

TEST(MotorObservation, FullChannelNeverOverwritesAndExposesFinalLoss) {
    example_motor_observations_t channel = {};
    nx_power_stage_sample_t sample = {};
    nx_power_stage_plan_t plan = duty_plan(50, 60, 70);
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
    const nx_power_stage_plan_t plan = duty_plan(50, 60, 70);
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
    const nx_power_stage_plan_t plan = duty_plan(50, 60, 70);
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
        ++sample.plan_sequence;
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
