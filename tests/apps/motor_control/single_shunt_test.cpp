/**
 * \file            single_shunt_test.cpp
 * \brief           Actual selected DC-current and integer PWM bridge contracts
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-24
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "current_path.h"
#include <cstring>
#include <gtest/gtest.h>

namespace {
struct DcPort {
    nx_power_stage_state_t facts{};
    nx_power_stage_session_t* session = nullptr;
    nx_power_stage_plan_t applied{};
    static nx_result_t start(void* context, nx_power_stage_session_t* loan) {
        auto& self = *static_cast<DcPort*>(context);
        const auto result = nx_request_admit(&loan->base, NX_REQUEST_ACTIVE);
        if (result == NX_SUCCESS) {
            self.session = loan;
            self.applied = loan->initial;
            self.facts.running = true;
        }
        return result;
    }
    static nx_result_t service(void*) {
        return NX_ERROR_BUSY;
    }
    static nx_result_t stop(void* context) {
        auto& self = *static_cast<DcPort*>(context);
        self.facts.running = false;
        nx_request_settle(&self.session->base, NX_SUCCESS, 0);
        self.session = nullptr;
        return NX_SUCCESS;
    }
    static nx_result_t state(const void* context,
                             nx_power_stage_state_t* result) {
        *result = static_cast<const DcPort*>(context)->facts;
        return NX_SUCCESS;
    }
    const nx_power_stage_ops_t operations = {start, service, stop, state};
    nx_power_stage_t port = {&operations, this};
};
class SingleShunt : public testing::Test {
  protected:
    example_motor_drive_config_t config = example_motor_drive_reference();
    example_motor_drive_context_t context{};
    nx_power_stage_sample_t raw{};
    nx_power_stage_dc_capture_t capture{};
    nx_modulation_config_t constraints{};
    void SetUp() override {
        config.topology = NX_MODULATION_SINGLE_SHUNT;
        config.phase_samples = 1;
        config.commissioned = true;
        config.maximum_scan_span_us = 400;
        config.bootstrap_voltage_ab = {2, 1};
        config.bootstrap_bus_voltage_v = 24;
        for (size_t i = 1; i < 3; ++i) {
            config.current_zero_code[i] = 0;
            config.current_a_per_code[i] = 0;
        }
        context.config = &config;
        context.stage.clock_epoch = 9;
        context.stage.tick_hz = 1000000;
        context.stage.period_ticks = 1000;
        context.stage.minimum_duty_ticks = 50;
        context.stage.maximum_duty_ticks = 900;
        context.stage.scan_span_us = 3;
        context.stage.deadtime_ticks = 1;
        context.stage.settling_ticks = 2;
        context.stage.aperture_ticks = 2;
        context.stage.conversion_ticks = 3;
        context.stage.irq_ticks = 2;
        context.stage.compute_ticks = 20;
        context.stage.rearm_ticks = 5;
        context.stage.commit_before_ticks = 990;
        context.stage.bus_available = true;
        context.stage.encoder_available = true;
        context.stage.dc_link = true;
        constraints.topology = NX_MODULATION_SINGLE_SHUNT;
        constraints.alignment = NX_MODULATION_EDGE_ALIGNED;
        constraints.period_ticks = 1000;
        constraints.minimum_duty_ticks = 50;
        constraints.maximum_duty_ticks = 900;
        constraints.deadtime_ticks = 1;
        constraints.settling_ticks = 2;
        constraints.aperture_ticks = 2;
        constraints.conversion_ticks = 3;
        constraints.irq_ticks = 2;
        constraints.compute_ticks = 20;
        constraints.rearm_ticks = 5;
        constraints.commit_before_ticks = 990;
        constraints.samples_per_trigger = 1;
        context.calibration.identity = config.calibration_identity;
        context.calibration.revision = config.calibration_revision;
        context.calibration.zero_code[0] = 2048;
        context.calibration.a_per_code[0] = 0.01F;
        context.calibration.direction[0] = 1;
        context.calibration.maximum_code = 4095;
        context.calibration.maximum_skew_ticks = 400;
        context.calibration.maximum_current_a = 5;
        context.calibration.maximum_sum_a = 0.2F;
        context.dc_plan = {{300, 500}, {3, 2}};
        capture = {{2049, 2047}, {300, 500}, {3, 2}, 7};
        raw.dc = &capture;
        raw.sequence = 7;
        raw.plan_sequence = 7;
        raw.clock_epoch = 9;
        raw.trigger_ticks = 300;
        raw.scan_span_us = 203;
    }
};
TEST_F(SingleShunt, ColdAdmissionSelectsOnlyTheActualDcLinkProfile) {
    EXPECT_EQ(example_motor_path_validate(&config, &context.stage), NX_SUCCESS);
    context.stage.dc_link = false;
    EXPECT_NE(example_motor_path_validate(&config, &context.stage), NX_SUCCESS);
    context.stage.dc_link = true;
    config.phase_samples = 2;
    EXPECT_NE(example_motor_path_validate(&config, &context.stage), NX_SUCCESS);
}
TEST_F(SingleShunt, TwoRealActiveVectorsReconstructAndRetainTheSkewBound) {
    nx_phase_current_t result{};
    ASSERT_EQ(example_motor_path_sample(&context, &raw, &result), NX_SUCCESS);
    EXPECT_NEAR(result.phase_a[0], -0.01F, 0.000001F);
    EXPECT_NEAR(result.phase_a[1], -0.01F, 0.000001F);
    EXPECT_NEAR(result.phase_a[2], 0.02F, 0.000001F);
    EXPECT_EQ(result.measured_mask, 3);
    EXPECT_EQ(result.reconstructed_mask, 4);
    EXPECT_EQ(result.timing, NX_CURRENT_APERTURES_BOUNDED);
    EXPECT_EQ(result.reference_tick, 300U);
    EXPECT_EQ(result.skew_ticks, 203U);
}
TEST_F(SingleShunt, WrongAdoptedMappingCannotBeTreatedAsARealCurrent) {
    nx_phase_current_t result{};
    result.reference_tick = 77;
    capture.switching_vector[1] = 4;
    EXPECT_NE(example_motor_path_sample(&context, &raw, &result), NX_SUCCESS);
    EXPECT_EQ(result.reference_tick, 77U);
    capture.switching_vector[1] = 2;
    --capture.sequence;
    EXPECT_NE(example_motor_path_sample(&context, &raw, &result), NX_SUCCESS);
    EXPECT_EQ(result.reference_tick, 77U);
}
TEST_F(SingleShunt, MissingOrMixedPhaseCaptureAndExcessSkewAreRejected) {
    nx_phase_current_t result{};
    raw.dc = nullptr;
    EXPECT_NE(example_motor_path_sample(&context, &raw, &result), NX_SUCCESS);
    raw.dc = &capture;
    raw.current_phase_mask = 1;
    EXPECT_NE(example_motor_path_sample(&context, &raw, &result), NX_SUCCESS);
    raw.current_phase_mask = 0;
    raw.scan_span_us = 401;
    EXPECT_NE(example_motor_path_sample(&context, &raw, &result), NX_SUCCESS);
}
TEST_F(SingleShunt, PlannerRetainsTheWholeAdoptableDescriptorInTheLoan) {
    context.fast.active.generation = 2;
    nx_modulation_output_t actual{}, expected{};
    nx_power_stage_plan_t plan{};
    const nx_motor_ab_t voltage{2, 1};
    ASSERT_EQ(nx_modulation_bus_plan(&constraints, voltage, 24, &expected),
              NX_SUCCESS);
    ASSERT_EQ(example_motor_path_plan(&context, &constraints, voltage, 24,
                                      &actual, &plan),
              NX_SUCCESS);
    EXPECT_EQ(plan.dc, &context.dc_plan);
    EXPECT_EQ(plan.current_phase_mask, 0);
    EXPECT_FLOAT_EQ(actual.voltage.alpha, expected.voltage.alpha);
    EXPECT_FLOAT_EQ(actual.voltage.beta, expected.voltage.beta);
    for (size_t i = 0; i < 3; ++i) {
        EXPECT_EQ(plan.duty_ticks[i], expected.duty_ticks[i]);
        EXPECT_EQ(plan.start_ticks[i], expected.start_ticks[i]);
    }
    for (size_t i = 0; i < 2; ++i) {
        EXPECT_EQ(plan.dc->sample_ticks[i], expected.samples[i].tick);
        EXPECT_EQ(plan.dc->switching_vector[i],
                  expected.samples[i].switching_vector);
    }
}
TEST_F(SingleShunt, ZeroVoltageNeverInventsAnObservableSteadyCurrentCycle) {
    context.fast.active.generation = 2;
    const auto saved = context.dc_plan;
    nx_modulation_output_t modulation{};
    nx_power_stage_plan_t plan{};
    plan.sequence = 77;
    EXPECT_EQ(example_motor_path_plan(&context, &constraints, {0, 0}, 24,
                                      &modulation, &plan),
              NX_ERROR_UNSUPPORTED);
    EXPECT_EQ(plan.sequence, 77U);
    EXPECT_EQ(std::memcmp(&saved, &context.dc_plan, sizeof(saved)), 0);
}
TEST_F(SingleShunt, PlannerQualifiesTheProvidersRoundedWholeScanSpan) {
    context.fast.active.generation = 2;
    context.stage.tick_hz = 8000000;
    context.stage.conversion_ticks = 24;
    constraints.conversion_ticks = 24;
    nx_modulation_output_t measured{};
    ASSERT_EQ(nx_modulation_bus_plan(&constraints, {2, 1}, 24, &measured),
              NX_SUCCESS);
    const auto separation = measured.samples[1].tick - measured.samples[0].tick;
    ASSERT_NE(separation % 8U, 0U);
    context.calibration.maximum_skew_ticks = separation + 24;
    nx_power_stage_plan_t plan{};
    EXPECT_EQ(example_motor_path_plan(&context, &constraints, {2, 1}, 24,
                                      &measured, &plan),
              NX_ERROR_UNSUPPORTED);
}
TEST_F(SingleShunt, OnlyTheExplicitFiniteBootstrapMayReplaceTheZeroRequest) {
    context.fast.active.generation = 1;
    nx_modulation_output_t modulation{};
    nx_power_stage_plan_t plan{};
    ASSERT_EQ(example_motor_path_plan(&context, &constraints, {0, 0}, 24,
                                      &modulation, &plan),
              NX_SUCCESS);
    EXPECT_GT(modulation.voltage.alpha, 1.9F);
    EXPECT_GT(modulation.voltage.beta, 0.9F);
    EXPECT_EQ(plan.dc, &context.dc_plan);
}
TEST_F(SingleShunt,
       InitialPlanNeedsCommissioningAndAnObservableExplicitVoltage) {
    nx_power_stage_plan_t plan{};
    ASSERT_EQ(example_motor_path_initial(&context, &constraints, &plan),
              NX_SUCCESS);
    EXPECT_EQ(plan.sequence, 0U);
    EXPECT_EQ(plan.dc, &context.dc_plan);
    EXPECT_NE(plan.dc->switching_vector[0], plan.dc->switching_vector[1]);
    config.bootstrap_voltage_ab = {0, 0};
    EXPECT_EQ(example_motor_path_initial(&context, &constraints, &plan),
              NX_ERROR_UNSUPPORTED);
    config.bootstrap_voltage_ab = {2, 1};
    config.commissioned = false;
    EXPECT_NE(example_motor_path_initial(&context, &constraints, &plan),
              NX_SUCCESS);
}
TEST_F(SingleShunt, ActualCallbackUsesTheSelectedCurrentAndFinalVoltagePath) {
    DcPort wire;
    wire.facts = context.stage;
    config.motor.maximum_angle_age_us = 500;
    example_motor_drive_t drive{};
    ASSERT_EQ(example_motor_drive_initialize(&drive, &wire.port, &config),
              NX_SUCCESS);
    ASSERT_EQ(example_motor_drive_start(&drive, 1000), NX_SUCCESS);
    ASSERT_NE(wire.session, nullptr);
    EXPECT_EQ(wire.session->initial.dc, &drive.context.dc_plan);
    nx_time_us_t now = 1000;
    for (uint64_t sequence = 0; sequence < 3; ++sequence) {
        const auto adopted = *wire.applied.dc;
        nx_power_stage_dc_capture_t observed{};
        observed.code[0] = 2048;
        observed.code[1] = 2048;
        observed.sequence = sequence;
        for (size_t i = 0; i < 2; ++i) {
            observed.sample_ticks[i] = adopted.sample_ticks[i];
            observed.switching_vector[i] = adopted.switching_vector[i];
        }
        nx_power_stage_sample_t sample{};
        sample.dc = &observed;
        sample.bus_valid = true;
        sample.encoder_valid = true;
        sample.bus_raw = 2400;
        sample.sequence = sequence;
        sample.plan_sequence = sequence;
        sample.clock_epoch = 9;
        sample.trigger_ticks = adopted.sample_ticks[0];
        sample.sample_at = 1000 + sequence * 1000 + sample.trigger_ticks;
        sample.scan_span_us =
            adopted.sample_ticks[1] - adopted.sample_ticks[0] + 3;
        sample.observed_at = sample.sample_at + sample.scan_span_us;
        sample.angle_at = sample.observed_at;
        sample.planned_apply_at = 2000 + sequence * 1000;
        nx_power_stage_plan_t next{};
        ASSERT_EQ(wire.session->compute(wire.session->context, &sample, &next),
                  NX_SUCCESS);
        EXPECT_EQ(next.sequence, sequence + 1);
        EXPECT_EQ(next.dc, &drive.context.dc_plan);
        wire.applied = next;
        now = sample.observed_at;
        EXPECT_EQ(example_motor_drive_step(&drive, now), NX_ERROR_BUSY);
    }
    EXPECT_TRUE(drive.have_feedback);
    EXPECT_GT(drive.context.last_plan.planned_voltage.d, 1.9F);
    EXPECT_GT(drive.context.last_plan.planned_voltage.q, 0.9F);
    EXPECT_EQ(example_motor_drive_stop(&drive), NX_SUCCESS);
    EXPECT_FALSE(drive.borrowed);
    EXPECT_EQ(wire.session, nullptr);
}
} /* namespace */
