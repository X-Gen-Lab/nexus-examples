/**
 * \file            phase_path.c
 * \brief           Selected phase-shunt current and complete PWM plan adapter
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-24
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "current_path.h"

nx_result_t
example_motor_path_validate(const example_motor_drive_config_t* config,
                            const nx_power_stage_state_t* stage) {
    if (config->topology == NX_MODULATION_SINGLE_SHUNT) {
        return NX_ERROR_UNSUPPORTED;
    }
    if (config->phase_samples < 2 || config->phase_samples > 3) {
        return NX_ERROR_INVALID;
    }
    return stage->current_count >= 2 && stage->current_count <= 3
               ? NX_SUCCESS
               : NX_ERROR_STATE;
}

nx_result_t
example_motor_path_sample(const example_motor_drive_context_t* context,
                          const nx_power_stage_sample_t* raw,
                          nx_phase_current_t* current) {
    if (raw->dc != NULL || raw->current_count < 2 || raw->current_count > 3) {
        return NX_ERROR_STATE;
    }
    const uint64_t scan_ticks =
        ((uint64_t)raw->scan_span_us * context->stage.tick_hz + 999999) /
        1000000;
    if (scan_ticks > UINT32_MAX) {
        return NX_ERROR_EXHAUSTED;
    }
    nx_current_capture_t capture = {
        .calibration_identity = context->config->calibration_identity,
        .calibration_revision = context->config->calibration_revision,
        .timing = NX_CURRENT_APERTURES_BOUNDED,
        .reference_tick = raw->trigger_ticks,
        .skew_bound_ticks = (uint32_t)scan_ticks,
        .measured_mask = raw->current_phase_mask,
        .valid = true};
    uint32_t rank = 0;
    for (uint32_t i = 0; i < 3; ++i) {
        if ((raw->current_phase_mask & (1U << i)) != 0U) {
            capture.code[i] = raw->current_raw[i];
            ++rank;
        }
    }
    if (rank != raw->current_count) {
        return NX_ERROR_STATE;
    }
    return nx_current_convert(&context->calibration, &capture, current);
}

nx_result_t
example_motor_path_initial(example_motor_drive_context_t* context,
                           const nx_modulation_config_t* constraints,
                           nx_power_stage_plan_t* plan) {
    (void)constraints;
    const nx_power_stage_state_t* stage = &context->stage;
    uint32_t middle =
        stage->minimum_duty_ticks +
        (stage->maximum_duty_ticks - stage->minimum_duty_ticks) / 2;
    if (stage->center_aligned) {
        middle &= ~UINT32_C(1);
    }
    nx_power_stage_plan_t result = {0};
    for (size_t i = 0; i < 3; ++i) {
        result.duty_ticks[i] = middle;
        result.start_ticks[i] =
            stage->center_aligned ? stage->period_ticks - middle / 2 : 0;
    }
    result.trigger_ticks = stage->trigger_ticks;
    result.current_phase_mask = stage->current_phase_mask;
    *plan = result;
    return NX_SUCCESS;
}

nx_result_t example_motor_path_plan(example_motor_drive_context_t* context,
                                    const nx_modulation_config_t* constraints,
                                    nx_motor_ab_t requested, float bus_v,
                                    nx_modulation_output_t* modulation,
                                    nx_power_stage_plan_t* plan) {
    (void)context;
    const nx_result_t result =
        nx_modulation_phase_plan(constraints, requested, bus_v, modulation);
    if (result != NX_SUCCESS) {
        return result;
    }
    nx_power_stage_plan_t mapped = {0};
    for (size_t i = 0; i < 3; ++i) {
        mapped.duty_ticks[i] = modulation->duty_ticks[i];
        mapped.start_ticks[i] = modulation->start_ticks[i];
    }
    mapped.trigger_ticks = modulation->samples[0].tick;
    mapped.current_phase_mask = modulation->samples[0].phase_mask;
    *plan = mapped;
    return NX_SUCCESS;
}
