/**
 * \file            single_shunt.c
 * \brief           Selected DC-link observation and PWM planning composition
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-24
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "current_path.h"
#include <math.h>

nx_result_t
example_motor_path_validate(const example_motor_drive_config_t* config,
                            const nx_power_stage_state_t* stage) {
    if (config->topology != NX_MODULATION_SINGLE_SHUNT)
        return NX_ERROR_UNSUPPORTED;
    if (!stage->dc_link || stage->current_count != 0U ||
        stage->current_phase_mask != 0U || config->phase_samples != 1U)
        return NX_ERROR_STATE;
    if (!config->commissioned)
        return NX_ERROR_STATE;
    if (!isfinite(config->bootstrap_voltage_ab.alpha) ||
        !isfinite(config->bootstrap_voltage_ab.beta) ||
        !isfinite(config->bootstrap_bus_voltage_v) ||
        config->bootstrap_bus_voltage_v < config->motor.minimum_bus_voltage_v ||
        config->bootstrap_bus_voltage_v > config->motor.maximum_bus_voltage_v)
        return NX_ERROR_INVALID;
    const float amplitude = hypotf(config->bootstrap_voltage_ab.alpha,
                                   config->bootstrap_voltage_ab.beta);
    if (amplitude == 0.0F)
        return NX_ERROR_UNSUPPORTED;
    if (amplitude > config->bootstrap_bus_voltage_v *
                        config->motor.maximum_modulation * 0.57735026919F)
        return NX_ERROR_INVALID;
    return NX_SUCCESS;
}

nx_result_t
example_motor_path_sample(const example_motor_drive_context_t* context,
                          const nx_power_stage_sample_t* raw,
                          nx_phase_current_t* current) {
    if (raw->dc == NULL || raw->current_count != 0U ||
        raw->current_phase_mask != 0U ||
        raw->clock_epoch != context->stage.clock_epoch ||
        raw->plan_sequence != raw->sequence ||
        raw->dc->sequence != raw->plan_sequence ||
        raw->trigger_ticks != raw->dc->sample_ticks[0] ||
        raw->scan_span_us > context->config->maximum_scan_span_us)
        return NX_ERROR_STATE;
    for (size_t i = 0U; i < 2U; ++i) {
        if (raw->dc->sample_ticks[i] != context->dc_plan.sample_ticks[i] ||
            raw->dc->switching_vector[i] !=
                context->dc_plan.switching_vector[i])
            return NX_ERROR_STATE;
    }
    if (raw->dc->sample_ticks[0] >= raw->dc->sample_ticks[1] ||
        raw->dc->sample_ticks[1] >= context->stage.period_ticks)
        return NX_ERROR_STATE;
    const uint64_t skew =
        ((uint64_t)raw->scan_span_us * context->stage.tick_hz + 999999U) /
        1000000U;
    if (skew > UINT32_MAX)
        return NX_ERROR_EXHAUSTED;
    if (skew < (uint64_t)raw->dc->sample_ticks[1] - raw->dc->sample_ticks[0] +
                   context->stage.aperture_ticks)
        return NX_ERROR_STATE;
    const nx_current_bus_capture_t measured = {
        .calibration_identity = context->config->calibration_identity,
        .calibration_revision = context->config->calibration_revision,
        .code = {raw->dc->code[0], raw->dc->code[1]},
        .timing = NX_CURRENT_APERTURES_BOUNDED,
        .reference_tick = raw->dc->sample_ticks[0],
        .skew_bound_ticks = (uint32_t)skew,
        .switching_vector = {raw->dc->switching_vector[0],
                             raw->dc->switching_vector[1]},
        .valid = true};
    return nx_current_reconstruct_bus(&context->calibration, &measured,
                                      current);
}

nx_result_t example_motor_path_plan(example_motor_drive_context_t* context,
                                    const nx_modulation_config_t* constraints,
                                    nx_motor_ab_t requested, float bus_v,
                                    nx_modulation_output_t* modulation,
                                    nx_power_stage_plan_t* plan) {
    if (constraints->single_shunt_shift_ticks != 0U)
        return NX_ERROR_UNSUPPORTED;
    if (context->stage.tick_hz == 0U)
        return NX_ERROR_STATE;
    const nx_motor_ab_t selected = context->fast.active.generation == 1U
                                       ? context->config->bootstrap_voltage_ab
                                       : requested;
    nx_modulation_output_t next;
    nx_result_t result =
        nx_modulation_bus_plan(constraints, selected, bus_v, &next);
    if (result != NX_SUCCESS)
        return result;
    /* Match the provider's conservative whole-span microsecond report before
     * admitting a plan; fine carrier ticks must not hide rounding overhead. */
    const uint64_t separation =
        (uint64_t)next.samples[1].tick - next.samples[0].tick;
    const uint64_t span_us =
        (separation * 1000000U + context->stage.tick_hz - 1U) /
            context->stage.tick_hz +
        context->stage.scan_span_us;
    if (span_us > context->config->maximum_scan_span_us)
        return NX_ERROR_UNSUPPORTED;
    const uint64_t skew =
        (span_us * context->stage.tick_hz + 999999U) / 1000000U;
    if (skew > context->calibration.maximum_skew_ticks)
        return NX_ERROR_UNSUPPORTED;
    nx_power_stage_plan_t mapped = {0};
    nx_power_stage_dc_plan_t dc = {0};
    for (size_t i = 0U; i < 3U; ++i) {
        mapped.duty_ticks[i] = next.duty_ticks[i];
        mapped.start_ticks[i] = next.start_ticks[i];
    }
    for (size_t i = 0U; i < 2U; ++i) {
        dc.sample_ticks[i] = next.samples[i].tick;
        dc.switching_vector[i] = next.samples[i].switching_vector;
    }
    mapped.trigger_ticks = dc.sample_ticks[0];
    mapped.dc = &context->dc_plan;
    next.limited |=
        selected.alpha != requested.alpha || selected.beta != requested.beta;
    context->dc_plan = dc;
    *modulation = next;
    *plan = mapped;
    return NX_SUCCESS;
}

nx_result_t
example_motor_path_initial(example_motor_drive_context_t* context,
                           const nx_modulation_config_t* constraints,
                           nx_power_stage_plan_t* plan) {
    nx_result_t result =
        example_motor_path_validate(context->config, &context->stage);
    if (result != NX_SUCCESS)
        return result;
    nx_modulation_output_t ignored;
    return example_motor_path_plan(
        context, constraints, context->config->bootstrap_voltage_ab,
        context->config->bootstrap_bus_voltage_v, &ignored, plan);
}
