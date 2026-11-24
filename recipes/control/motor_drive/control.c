/**
 * \file            control.c
 * \brief           Calibrated drive callback and retained session withdrawal
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-24
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "control.h"
#include "nexus/arch/atomic.h"
#include <math.h>
#include <stddef.h>

static nx_result_t failed(example_motor_drive_context_t* context,
                          nx_result_t reason) {
    if (context->motor.initialized) {
        (void)nx_motor_latch_fault(&context->motor, reason);
    }
    return reason;
}

static nx_result_t calculate(void* argument, const nx_power_stage_sample_t* raw,
                             nx_power_stage_plan_t* plan) {
    example_motor_drive_context_t* context = argument;
    const example_motor_drive_config_t* config = &context->config;
    const float encoder_scale =
        (config->encoder_reversed ? -6.283185307179586F : 6.283185307179586F) /
        (float)config->encoder_counts_per_turn;
    if (raw->clock_epoch == 0 ||
        raw->scan_span_us > config->maximum_scan_span_us ||
        raw->sample_at > raw->observed_at || raw->angle_at > raw->observed_at ||
        raw->observed_at > raw->planned_apply_at) {
        return failed(context, NX_ERROR_STATE);
    }
    if (raw->planned_apply_at >= context->run_until) {
        return failed(context, NX_ERROR_TIMEOUT);
    }
    nx_motor_sample_t sample = {.bus_voltage_v =
                                    raw->bus_raw * config->bus_v_per_code,
                                .sample_time_us = raw->sample_at,
                                .angle_time_us = raw->angle_at,
                                .observed_at_us = raw->observed_at,
                                .apply_time_us = raw->planned_apply_at,
                                .sequence = raw->sequence,
                                .clock_domain = config->motor.clock_domain,
                                .clock_epoch = raw->clock_epoch,
                                .valid = true};
    float sum = 0;
    for (size_t i = 0; i < 3; ++i) {
        sample.phase_current_a[i] =
            ((float)raw->current_raw[i] - config->current_zero_code[i]) *
            config->current_a_per_code[i];
        if (!isfinite(sample.phase_current_a[i]) ||
            fabsf(sample.phase_current_a[i]) >
                config->motor.phase_current_limit_a) {
            return failed(context, NX_ERROR_IO);
        }
        sum += sample.phase_current_a[i];
    }
    if (!isfinite(sample.bus_voltage_v) ||
        sample.bus_voltage_v < config->motor.minimum_bus_voltage_v ||
        sample.bus_voltage_v > config->motor.maximum_bus_voltage_v ||
        fabsf(sum) > config->motor.current_sum_tolerance_a) {
        return failed(context, NX_ERROR_IO);
    }

    if (!context->reference_seen) {
        nx_motor_config_t kernel = config->motor;
        kernel.clock_epoch = raw->clock_epoch;
        const float available = (float)(context->stage.maximum_duty_ticks -
                                        context->stage.minimum_duty_ticks) /
                                (float)context->stage.period_ticks;
        kernel.minimum_duty = fmaxf(kernel.minimum_duty, (1 - available) / 2);
        nx_result_t status = nx_motor_initialize(&context->motor, &kernel);
        if (status != NX_SUCCESS) {
            return status;
        }
        nx_encoder_estimate_t origin;
        status = nx_encoder_tracker_step(
            &context->encoder, raw->encoder_count, encoder_scale,
            (float)config->motor.nominal_period_us * 0.000001F, &origin);
        if (status != NX_SUCCESS) {
            return failed(context, status);
        }
        context->last_angle_at = raw->angle_at;
        context->last_sequence = raw->sequence;
        context->reference_seen = true;
        const uint32_t neutral = context->stage.minimum_duty_ticks +
                                 (context->stage.maximum_duty_ticks -
                                  context->stage.minimum_duty_ticks) /
                                     2;
        const nx_power_stage_plan_t warmup = {{neutral, neutral, neutral}};
        *plan = warmup;
        return NX_SUCCESS;
    }
    if (raw->clock_epoch != context->motor.config.clock_epoch ||
        context->last_sequence == UINT64_MAX ||
        raw->sequence != context->last_sequence + 1 ||
        raw->angle_at <= context->last_angle_at ||
        raw->angle_at - context->last_angle_at > config->motor.maximum_gap_us) {
        return failed(context, NX_ERROR_STATE);
    }
    nx_encoder_estimate_t estimate;
    const nx_result_t tracked = nx_encoder_tracker_step(
        &context->encoder, raw->encoder_count, encoder_scale,
        (float)(raw->angle_at - context->last_angle_at) * 0.000001F, &estimate);
    if (tracked != NX_SUCCESS || !estimate.velocity_valid) {
        return failed(context,
                      tracked == NX_SUCCESS ? NX_ERROR_STATE : tracked);
    }
    const int64_t within_turn =
        estimate.position_counts % (int64_t)config->encoder_counts_per_turn;
    sample.mechanical_angle_rad = config->initial_mechanical_angle_rad +
                                  (float)within_turn * encoder_scale;
    sample.mechanical_velocity_rad_s = estimate.velocity;
    const nx_motor_reference_t reference = {config->id_a, config->iq_a,
                                            context->run_until};
    nx_motor_output_t output;
    nx_result_t status =
        nx_motor_step(&context->motor, &sample, &reference, &output);
    if (status != NX_SUCCESS) {
        return status;
    }
    nx_power_stage_plan_t next;
    float leg_voltage[3];
    const float low =
        fminf(output.duty[0], fminf(output.duty[1], output.duty[2]));
    const float high =
        fmaxf(output.duty[0], fmaxf(output.duty[1], output.duty[2]));
    const float minimum = (float)context->stage.minimum_duty_ticks /
                          (float)context->stage.period_ticks;
    const float maximum = (float)context->stage.maximum_duty_ticks /
                          (float)context->stage.period_ticks;
    const float width = maximum - minimum;
    const float scale = high - low > width ? width / (high - low) : 1;
    const float common = (minimum + maximum - (high + low) * scale) / 2;
    for (size_t i = 0; i < 3; ++i) {
        const float ticks = (output.duty[i] * scale + common) *
                            (float)context->stage.period_ticks;
        if (!isfinite(ticks) || ticks < 0 || ticks > UINT16_MAX) {
            return failed(context, NX_ERROR_INVALID);
        }
        next.duty_ticks[i] = (uint32_t)floorf(ticks + 0.5F);
        if (next.duty_ticks[i] < context->stage.minimum_duty_ticks ||
            next.duty_ticks[i] > context->stage.maximum_duty_ticks) {
            return failed(context, NX_ERROR_INVALID);
        }
        leg_voltage[i] = sample.bus_voltage_v * (float)next.duty_ticks[i] /
                         (float)context->stage.period_ticks;
        output.duty[i] =
            (float)next.duty_ticks[i] / (float)context->stage.period_ticks;
    }
    const nx_motor_ab_t voltage = {
        (2 * leg_voltage[0] - leg_voltage[1] - leg_voltage[2]) / 3,
        (leg_voltage[1] - leg_voltage[2]) * 0.5773502691896258F};
    nx_motor_dq_t quantized;
    status =
        nx_motor_park(voltage, output.output_electrical_angle_rad, &quantized);
    if (status == NX_SUCCESS) {
        status =
            nx_motor_track_voltage(&context->motor, output.sequence, quantized);
    }
    if (status != NX_SUCCESS) {
        return failed(context, status);
    }
    output.planned_voltage = quantized;
    output.limited = output.limited || scale < 1;
    context->last_plan = output;
    context->last_angle_at = raw->angle_at;
    context->last_sequence = raw->sequence;
    *plan = next;
    return NX_SUCCESS;
}

static nx_result_t compute(void* argument, const nx_power_stage_sample_t* raw,
                           nx_power_stage_plan_t* plan) {
    example_motor_drive_context_t* context = argument;
    const nx_result_t status = calculate(argument, raw, plan);
    example_motor_observation_publish(
        context->observations, raw, status == NX_SUCCESS ? plan : NULL, status);
    return status;
}

nx_result_t
example_motor_drive_initialize(example_motor_drive_t* drive,
                               const nx_power_stage_t* port,
                               const example_motor_drive_config_t* config) {
    if (drive == NULL || port == NULL || config == NULL ||
        !isfinite(config->bus_v_per_code) || config->bus_v_per_code <= 0 ||
        config->encoder_counts_per_turn < 2 ||
        !isfinite(config->initial_mechanical_angle_rad) ||
        fabsf(config->initial_mechanical_angle_rad) > 3.141592653589793F ||
        !isfinite(config->id_a) || !isfinite(config->iq_a) ||
        config->maximum_scan_span_us == 0 || config->run_duration_us == 0 ||
        config->startup_budget_us == 0 ||
        (uint64_t)config->startup_budget_us + config->motor.nominal_period_us >=
            config->run_duration_us) {
        return NX_ERROR_INVALID;
    }
    if (drive->borrowed ||
        (drive->initialized &&
         nx_atomic_u32_load_acquire(&drive->observations.published) !=
             nx_atomic_u32_load_acquire(&drive->observations.consumed))) {
        return NX_ERROR_STATE;
    }
    for (size_t i = 0; i < 3; ++i) {
        if (!isfinite(config->current_zero_code[i]) ||
            config->current_zero_code[i] < 0 ||
            config->current_zero_code[i] > 4095 ||
            !isfinite(config->current_a_per_code[i]) ||
            config->current_a_per_code[i] == 0) {
            return NX_ERROR_INVALID;
        }
    }
    example_motor_drive_t next = {.port = port};
    /* Validate numeric constants without asserting a runtime clock identity.
     * The first actual provider frame binds the real, nonzero Core epoch. */
    nx_motor_config_t probe_config = config->motor;
    probe_config.clock_epoch = 1;
    nx_motor_t probe = {0};
    if (nx_motor_initialize(&probe, &probe_config) != NX_SUCCESS ||
        hypotf(config->id_a, config->iq_a) > config->motor.current_limit_a ||
        nx_encoder_tracker_init(&next.context.encoder, 65536,
                                config->maximum_encoder_delta,
                                0) != NX_SUCCESS) {
        return NX_ERROR_INVALID;
    }
    next.context.config = *config;
    nx_request_initialize(&next.session.base);
    next.initialized = true;
    *drive = next;
    drive->context.observations = &drive->observations;
    return NX_SUCCESS;
}

nx_result_t example_motor_drive_start(example_motor_drive_t* drive,
                                      nx_time_us_t now) {
    if (drive == NULL || !drive->initialized) {
        return NX_ERROR_INVALID;
    }
    if (drive->borrowed || drive->stopping || drive->primary != NX_SUCCESS ||
        nx_atomic_u32_load_acquire(&drive->observations.published) !=
            nx_atomic_u32_load_acquire(&drive->observations.consumed)) {
        return NX_ERROR_STATE;
    }
    if (!drive->context.config.commissioned) {
        return NX_ERROR_STATE;
    }
    nx_time_us_t startup;
    nx_time_us_t end;
    nx_result_t status = nx_deadline_finite_after(
        now, drive->context.config.startup_budget_us, &startup);
    if (status == NX_SUCCESS) {
        status = nx_deadline_finite_after(
            now, drive->context.config.run_duration_us, &end);
    }
    if (status != NX_SUCCESS) {
        return status;
    }
    status = nx_power_stage_state(drive->port, &drive->context.stage);
    if (status != NX_SUCCESS) {
        return status;
    }
    const nx_power_stage_state_t* facts = &drive->context.stage;
    const uint32_t neutral =
        facts->minimum_duty_ticks +
        (facts->maximum_duty_ticks - facts->minimum_duty_ticks) / 2;
    if (facts->running || facts->quarantined || facts->fault != NX_SUCCESS ||
        facts->period_ticks == 0 || facts->period_ticks > UINT16_MAX ||
        facts->tick_hz == 0 ||
        facts->minimum_duty_ticks >= facts->maximum_duty_ticks ||
        facts->maximum_duty_ticks >= facts->period_ticks ||
        neutral < facts->minimum_duty_ticks ||
        neutral > facts->maximum_duty_ticks ||
        facts->scan_span_us > drive->context.config.maximum_scan_span_us ||
        (uint64_t)facts->period_ticks * 1000000 !=
            (uint64_t)facts->tick_hz *
                drive->context.config.motor.nominal_period_us) {
        return NX_ERROR_STATE;
    }
    const nx_power_stage_plan_t initial = {{neutral, neutral, neutral}};
    const nx_time_us_t stop_at =
        end - drive->context.config.motor.nominal_period_us;
    drive->context.run_until = end;
    status = nx_power_stage_prepare(&drive->session, &initial, compute,
                                    &drive->context, sizeof(drive->context),
                                    startup);
    if (status == NX_SUCCESS) {
        status = nx_power_stage_start(drive->port, &drive->session);
    }
    if (status == NX_SUCCESS) {
        drive->borrowed = true;
        drive->run_until = stop_at;
    }
    return status;
}

static nx_result_t settled(example_motor_drive_t* drive) {
    nx_result_t final;
    size_t transferred;
    if (nx_request_result(&drive->session.base, &final, &transferred) !=
        NX_SUCCESS) {
        return NX_ERROR_BUSY;
    }
    (void)transferred;
    drive->borrowed = false;
    if (drive->primary == NX_SUCCESS) {
        drive->primary = final;
    }
    return drive->primary;
}

nx_result_t example_motor_drive_stop(example_motor_drive_t* drive) {
    if (drive == NULL || !drive->initialized) {
        return NX_ERROR_INVALID;
    }
    if (!drive->borrowed) {
        return NX_ERROR_STATE;
    }
    if (nx_request_state(&drive->session.base) == NX_REQUEST_SETTLED) {
        return settled(drive);
    }
    drive->stopping = true;
    const nx_result_t status = nx_power_stage_stop(drive->port);
    if (status != NX_SUCCESS && status != NX_ERROR_BUSY &&
        drive->primary == NX_SUCCESS) {
        drive->primary = status;
    }
    if (nx_request_state(&drive->session.base) == NX_REQUEST_SETTLED) {
        return settled(drive);
    }
    (void)nx_power_stage_service(drive->port);
    return nx_request_state(&drive->session.base) == NX_REQUEST_SETTLED
               ? settled(drive)
               : NX_ERROR_BUSY;
}

nx_result_t example_motor_drive_step(example_motor_drive_t* drive,
                                     nx_time_us_t now) {
    if (drive == NULL || !drive->initialized) {
        return NX_ERROR_INVALID;
    }
    if (!drive->borrowed) {
        return NX_ERROR_STATE;
    }
    if (nx_request_state(&drive->session.base) == NX_REQUEST_SETTLED) {
        return settled(drive);
    }
    if (drive->stopping || now >= drive->run_until) {
        return example_motor_drive_stop(drive);
    }
    const nx_result_t status = nx_power_stage_service(drive->port);
    if (nx_request_state(&drive->session.base) == NX_REQUEST_SETTLED) {
        return settled(drive);
    }
    if (status != NX_SUCCESS && status != NX_ERROR_BUSY) {
        if (drive->primary == NX_SUCCESS) {
            drive->primary = status;
        }
        return example_motor_drive_stop(drive);
    }
    return NX_ERROR_BUSY;
}
