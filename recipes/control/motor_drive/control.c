/**
 * \file            control.c
 * \brief           Explicit Task Axis and IRQ current-drive composition
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-24
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "control.h"
#include "current_path.h"
#include "nexus/arch/atomic.h"
#include <math.h>
#include <stddef.h>

#define EXAMPLE_TWO_PI 6.28318530717958647693F

example_motor_drive_config_t example_motor_drive_reference(void) {
    const example_motor_drive_config_t selected = {
        .motor = {.pole_pairs = 7,
                  .clock_domain = 1,
                  .nominal_period_us = 1000,
                  .maximum_gap_us = 1100,
                  .maximum_angle_age_us = 100,
                  .maximum_prediction_us = 1100,
                  .kp_d = 2,
                  .ki_d = 100,
                  .kp_q = 2,
                  .ki_q = 100,
                  .current_limit_a = 3,
                  .phase_current_limit_a = 5,
                  .current_sum_tolerance_a = 0.2F,
                  .integral_limit_v = 12,
                  .minimum_bus_voltage_v = 10,
                  .maximum_bus_voltage_v = 30,
                  .maximum_modulation = 0.9F,
                  .minimum_duty = 0.05F,
                  .anti_windup_per_s = 100},
        .axis = {.clock_domain = 1,
                 .maximum_step_us = 3000,
                 .maximum_feedback_age_us = 3000,
                 .stop_policy = NX_AXIS_HOLD_ZERO_VELOCITY},
        .axis_parameters = {.revision = 1,
                            .velocity_kp = 0.2F,
                            .velocity_ki = 2,
                            .tracking_rate_per_s = 20,
                            .integral_limit_a = 3,
                            .position_kp = 8,
                            .current_limit_a = 3,
                            .velocity_limit_rad_s = 50,
                            .acceleration_rad_s2 = 100,
                            .minimum_position_rad = -100,
                            .maximum_position_rad = 100,
                            .position_tolerance_rad = 0.005F,
                            .velocity_tolerance_rad_s = 0.05F,
                            .settling_us = 10000},
        .topology = NX_MODULATION_THREE_SHUNT,
        .phase_samples = 3,
        .current_zero_code = {2048, 2048, 2048},
        .current_a_per_code = {0.01F, 0.01F, 0.01F},
        .bus_v_per_code = 0.01F,
        .encoder_counts_per_turn = 4096,
        .initial_mode = NX_AXIS_CURRENT,
        .maximum_encoder_delta = 100,
        .maximum_scan_span_us = 4,
        .run_duration_us = 100000,
        .startup_budget_us = 10000,
        .reference_lifetime_us = 4000,
        .feedback_division = 1,
        .calibration_identity = 1,
        .expected_calibration_identity = 1,
        .calibration_revision = 1,
        .motor_revision = 1,
        .commissioned = false};
    return selected;
}

static nx_result_t failed(example_motor_drive_context_t* context,
                          nx_result_t reason) {
    if (context->fast.initialized) {
        (void)nx_motor_latch_fault(&context->fast.motor, reason);
    }
    return reason;
}

static nx_modulation_config_t
modulation_for(const nx_power_stage_state_t* stage,
               const example_motor_drive_config_t* config) {
    const nx_modulation_config_t selected = {
        .topology = config->topology,
        .alignment = stage->center_aligned ? NX_MODULATION_CENTER_ALIGNED
                                           : NX_MODULATION_EDGE_ALIGNED,
        .period_ticks = stage->period_ticks,
        .minimum_duty_ticks = stage->minimum_duty_ticks,
        .maximum_duty_ticks = stage->maximum_duty_ticks,
        .deadtime_ticks = stage->deadtime_ticks,
        .settling_ticks = stage->settling_ticks,
        .aperture_ticks = stage->aperture_ticks,
        .conversion_ticks = stage->conversion_ticks,
        .irq_ticks = stage->irq_ticks,
        .compute_ticks = stage->compute_ticks,
        .rearm_ticks = stage->rearm_ticks,
        .commit_before_ticks = stage->commit_before_ticks,
        .capture_begin_ticks = stage->capture_begin_ticks,
        .capture_end_ticks = stage->capture_end_ticks,
        .sensor_mask = stage->current_phase_mask,
        .samples_per_trigger = config->phase_samples};
    return selected;
}

static nx_result_t calculate(void* argument, const nx_power_stage_sample_t* raw,
                             nx_power_stage_plan_t* plan) {
    example_motor_drive_context_t* context = argument;
    const example_motor_drive_config_t* config = context->config;
    if (raw->clock_epoch != context->stage.clock_epoch ||
        raw->scan_span_us > config->maximum_scan_span_us ||
        (!raw->bus_valid && config->bus == NULL) ||
        (!raw->encoder_valid && config->rotor == NULL) ||
        raw->plan_sequence != raw->sequence || raw->sequence == UINT64_MAX ||
        raw->sample_at > raw->observed_at || raw->angle_at > raw->observed_at ||
        raw->observed_at > raw->planned_apply_at) {
        return failed(context, NX_ERROR_STATE);
    }
    if (raw->planned_apply_at >= context->run_until) {
        return failed(context, NX_ERROR_TIMEOUT);
    }
    float bus_voltage = raw->bus_raw * config->bus_v_per_code;
    if (!raw->bus_valid) {
        example_bus_sample_t measured;
        const nx_result_t result = example_bus_take(
            config->bus, raw->observed_at, config->motor.clock_domain,
            raw->clock_epoch, config->bus_source_id, &measured);
        if (result != NX_SUCCESS) {
            return failed(context, result);
        }
        if (measured.valid_until <= raw->planned_apply_at) {
            return failed(context, NX_ERROR_TIMEOUT);
        }
        bus_voltage = measured.voltage_v;
    }
    if (!isfinite(bus_voltage) ||
        bus_voltage < config->motor.minimum_bus_voltage_v ||
        bus_voltage > config->motor.maximum_bus_voltage_v) {
        return failed(context, NX_ERROR_IO);
    }
    nx_phase_current_t current;
    nx_result_t status = example_motor_path_sample(context, raw, &current);
    if (status != NX_SUCCESS) {
        return failed(context, status);
    }
    nx_motor_sample_t sample = {.bus_voltage_v = bus_voltage,
                                .sample_time_us = raw->sample_at,
                                .angle_time_us = raw->angle_at,
                                .observed_at_us = raw->observed_at,
                                .apply_time_us = raw->planned_apply_at,
                                .sequence = raw->sequence,
                                .clock_domain = config->motor.clock_domain,
                                .clock_epoch = raw->clock_epoch,
                                .valid = true};
    for (size_t i = 0; i < 3; ++i) {
        sample.phase_current_a[i] = current.phase_a[i];
    }
    const bool anchored = context->reference_seen;
    float position;
    if (config->rotor != NULL) {
        example_rotor_sample_t rotor;
        status = example_rotor_take(config->rotor, raw->observed_at,
                                    config->motor.clock_domain,
                                    raw->clock_epoch, &rotor);
        if (status != NX_SUCCESS) {
            return failed(context, status);
        }
        sample.mechanical_angle_rad = rotor.mechanical_angle_rad;
        sample.mechanical_velocity_rad_s = rotor.mechanical_velocity_rad_s;
        sample.angle_time_us = rotor.angle_at;
        position = rotor.mechanical_position_rad;
    } else {
        const float scale =
            (config->encoder_reversed ? -EXAMPLE_TWO_PI : EXAMPLE_TWO_PI) /
            (float)config->encoder_counts_per_turn;
        if (anchored && (raw->angle_at <= context->last_angle_at ||
                         raw->angle_at - context->last_angle_at >
                             config->motor.maximum_gap_us)) {
            return failed(context, NX_ERROR_STATE);
        }
        nx_encoder_estimate_t rotor;
        const float dt =
            anchored
                ? (float)(raw->angle_at - context->last_angle_at) * 0.000001F
                : (float)config->motor.nominal_period_us * 0.000001F;
        status = nx_encoder_tracker_step(&context->encoder, raw->encoder_count,
                                         scale, dt, &rotor);
        if (status != NX_SUCCESS || (anchored && !rotor.velocity_valid)) {
            return failed(context,
                          status == NX_SUCCESS ? NX_ERROR_STATE : status);
        }
        const int64_t within =
            rotor.position_counts % (int64_t)config->encoder_counts_per_turn;
        sample.mechanical_angle_rad =
            config->initial_mechanical_angle_rad + (float)within * scale;
        sample.mechanical_velocity_rad_s = rotor.velocity;
        position = (float)rotor.position_counts * scale;
    }
    /* Feedback uses the same explicit bounded constant-velocity alignment as
     * commutation. The source observation retains its own time provenance. */
    const float alignment_s =
        sample.sample_time_us >= sample.angle_time_us
            ? (float)(sample.sample_time_us - sample.angle_time_us) * 0.000001F
            : -(float)(sample.angle_time_us - sample.sample_time_us) *
                  0.000001F;
    position += sample.mechanical_velocity_rad_s * alignment_s;
    nx_drive_output_t output = {0};
    nx_power_stage_plan_t next = {0};
    const nx_modulation_config_t constraints =
        modulation_for(&context->stage, config);
    if (!anchored) {
        status = example_motor_path_initial(context, &constraints, &next);
        if (status != NX_SUCCESS) {
            return failed(context, status);
        }
    }
    if (anchored) {
        status = nx_drive_step(&context->fast, &sample, &output);
        if (status != NX_SUCCESS) {
            return failed(context, status);
        }
        if (output.reference_generation == 1 &&
            raw->observed_at >= context->startup_until) {
            return failed(context, NX_ERROR_TIMEOUT);
        }
        nx_modulation_output_t modulated;
        status = example_motor_path_plan(
            context, &constraints, output.stationary_voltage,
            sample.bus_voltage_v, &modulated, &next);
        if (status != NX_SUCCESS) {
            return failed(context, status);
        }
        nx_motor_dq_t final;
        status =
            nx_motor_park(modulated.voltage,
                          output.voltage.output_electrical_angle_rad, &final);
        if (status == NX_SUCCESS) {
            status =
                nx_drive_track_voltage(&context->fast, raw->sequence, final);
        }
        if (status != NX_SUCCESS) {
            return failed(context, status);
        }
        output.voltage.planned_voltage = final;
        output.voltage.limited = output.voltage.limited || modulated.limited;
        context->last_plan = output.voltage;
    }
    if (anchored) {
        nx_drive_feedback_t feedback;
        status = nx_drive_feedback_sample(&context->feedback_sampler, &sample,
                                          position, &output, &feedback);
        if (status == NX_SUCCESS) {
            status = nx_drive_feedback_publish(context->feedback, &feedback);
        }
        if (status != NX_SUCCESS && status != NX_ERROR_BUSY) {
            return failed(context, status);
        }
    }
    context->last_angle_at = sample.angle_time_us;
    context->last_sequence = raw->sequence;
    context->reference_seen = true;
    next.sequence = raw->sequence + 1;
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
        config->motor.nominal_period_us == 0 ||
        config->maximum_scan_span_us == 0 || config->run_duration_us == 0 ||
        config->startup_budget_us == 0 || config->feedback_division == 0 ||
        config->reference_lifetime_us == 0 || config->motor_revision == 0 ||
        ((config->bus == NULL) != (config->bus_source_id == 0)) ||
        config->axis.clock_domain != config->motor.clock_domain ||
        (config->initial_mode != NX_AXIS_CURRENT &&
         config->initial_mode != NX_AXIS_VELOCITY &&
         config->initial_mode != NX_AXIS_POSITION) ||
        !isfinite(config->initial_position_rad) ||
        !isfinite(config->initial_velocity_rad_s) ||
        (config->initial_mode == NX_AXIS_POSITION &&
         (config->initial_velocity_rad_s != 0 ||
          config->initial_position_rad <
              config->axis_parameters.minimum_position_rad ||
          config->initial_position_rad >
              config->axis_parameters.maximum_position_rad)) ||
        (config->initial_mode == NX_AXIS_VELOCITY &&
         fabsf(config->initial_velocity_rad_s) >
             config->axis_parameters.velocity_limit_rad_s) ||
        config->calibration_revision == 0 ||
        config->calibration_identity == 0 ||
        config->calibration_identity != config->expected_calibration_identity ||
        (uint64_t)config->startup_budget_us + config->motor.nominal_period_us >=
            config->run_duration_us) {
        return NX_ERROR_INVALID;
    }
    /* The finite local-reference example never silently converts unbounded
     * multi-turn counts to f32. Keep half the exact integer range as margin;
     * long-running products must explicitly re-reference after joined stop. */
    const uint64_t cycles = ((uint64_t)config->run_duration_us +
                             config->motor.nominal_period_us - 1) /
                            config->motor.nominal_period_us;
    if (config->rotor == NULL &&
        cycles * config->maximum_encoder_delta > UINT64_C(8388608)) {
        return NX_ERROR_INVALID;
    }
    if (drive->borrowed ||
        (drive->initialized &&
         nx_atomic_u32_load_acquire(&drive->observations.published) !=
             nx_atomic_u32_load_acquire(&drive->observations.consumed))) {
        return NX_ERROR_STATE;
    }
    example_motor_drive_t next = {.port = port};
    nx_current_calibration_t* calibration = &next.context.calibration;
    calibration->identity = config->calibration_identity;
    calibration->revision = config->calibration_revision;
    calibration->maximum_code = 4095;
    calibration->maximum_skew_ticks = UINT32_MAX;
    calibration->maximum_current_a = config->motor.phase_current_limit_a;
    calibration->maximum_sum_a = config->motor.current_sum_tolerance_a;
    for (size_t i = 0; i < 3; ++i) {
        calibration->zero_code[i] = config->current_zero_code[i];
        calibration->a_per_code[i] = fabsf(config->current_a_per_code[i]);
        calibration->direction[i] = config->current_a_per_code[i] == 0  ? 0
                                    : config->current_a_per_code[i] < 0 ? -1
                                                                        : 1;
    }
    nx_result_t status = nx_current_calibration_check(calibration);
    nx_motor_config_t profile = config->motor;
    profile.clock_epoch = 1;
    nx_motor_t probe = {0};
    if (status == NX_SUCCESS) {
        status = nx_motor_initialize(&probe, &profile);
    }
    nx_axis_t axis_probe;
    nx_axis_config_t axis_profile = config->axis;
    axis_profile.epoch = 1;
    if (status == NX_SUCCESS) {
        status = nx_axis_initialize(&axis_probe, &axis_profile,
                                    &config->axis_parameters);
    }
    if (status != NX_SUCCESS ||
        hypotf(config->id_a, config->iq_a) > config->motor.current_limit_a ||
        nx_encoder_tracker_init(&next.context.encoder, 65536,
                                config->maximum_encoder_delta,
                                0) != NX_SUCCESS) {
        return NX_ERROR_INVALID;
    }
    next.config = *config;
    nx_request_initialize(&next.session.base);
    next.initialized = true;
    *drive = next;
    drive->context.config = &drive->config;
    drive->context.observations = &drive->observations;
    drive->context.feedback = &drive->feedback;
    return NX_SUCCESS;
}

static nx_result_t publish(example_motor_drive_t* drive, nx_time_us_t now,
                           float iq, uint32_t command, nx_time_us_t receipt,
                           bool bootstrap) {
    if (drive->reference_generation == UINT32_MAX) {
        return NX_ERROR_EXHAUSTED;
    }
    nx_time_us_t expiry = drive->authorized_until;
    if (!bootstrap) {
        nx_result_t status = nx_deadline_finite_after(
            now, drive->config.reference_lifetime_us, &expiry);
        if (status != NX_SUCCESS) {
            return status;
        }
        if (expiry > drive->authorized_until) {
            expiry = drive->authorized_until;
        }
        if (drive->axis_output.state == NX_AXIS_RUNNING &&
            expiry > drive->axis.command.valid_until) {
            expiry = drive->axis.command.valid_until;
        }
        if (drive->limits_active && expiry > drive->limits.valid_until) {
            expiry = drive->limits.valid_until;
        }
    }
    const nx_drive_reference_t reference = {
        .current = {bootstrap ? 0 : drive->config.id_a, iq, expiry},
        .clock_domain = drive->config.motor.clock_domain,
        .epoch = drive->references.epoch,
        .generation = drive->reference_generation + 1,
        .parameters_revision = drive->config.motor_revision,
        .command_sequence = command,
        .received_at = receipt};
    const nx_result_t status =
        nx_drive_reference_publish(&drive->references, &reference);
    if (status == NX_SUCCESS) {
        ++drive->reference_generation;
        if (!bootstrap && expiry > drive->last_reference_until) {
            drive->last_reference_until = expiry;
        }
    }
    return status;
}

nx_result_t example_motor_drive_start(example_motor_drive_t* drive,
                                      nx_time_us_t now) {
    if (drive == NULL || !drive->initialized) {
        return NX_ERROR_INVALID;
    }
    if (drive->borrowed || drive->stopping || drive->primary != NX_SUCCESS ||
        nx_atomic_u32_load_acquire(&drive->observations.published) !=
            nx_atomic_u32_load_acquire(&drive->observations.consumed) ||
        !drive->config.commissioned) {
        return NX_ERROR_STATE;
    }
    const example_motor_drive_config_t* config = &drive->config;
    nx_time_us_t startup, end;
    nx_result_t status =
        nx_deadline_finite_after(now, config->startup_budget_us, &startup);
    if (status == NX_SUCCESS) {
        status = nx_deadline_finite_after(now, config->run_duration_us, &end);
    }
    if (status == NX_SUCCESS) {
        status = nx_power_stage_state(drive->port, &drive->context.stage);
    }
    if (status != NX_SUCCESS) {
        return status;
    }
    const nx_power_stage_state_t* facts = &drive->context.stage;
    if (facts->running || facts->quarantined || facts->fault != NX_SUCCESS ||
        facts->clock_epoch == 0 ||
        (!facts->bus_available && config->bus == NULL) ||
        (config->motor.clock_epoch != 0 &&
         config->motor.clock_epoch != facts->clock_epoch) ||
        (config->axis.epoch != 0 && config->axis.epoch != facts->clock_epoch) ||
        (!facts->encoder_available && config->rotor == NULL) ||
        facts->period_ticks == 0 || facts->period_ticks > UINT16_MAX ||
        facts->tick_hz == 0 ||
        facts->minimum_duty_ticks >= facts->maximum_duty_ticks ||
        facts->maximum_duty_ticks >= facts->period_ticks ||
        facts->scan_span_us > config->maximum_scan_span_us ||
        (uint64_t)facts->period_ticks * 1000000 !=
            (uint64_t)facts->tick_hz * config->motor.nominal_period_us) {
        return NX_ERROR_STATE;
    }
    const nx_modulation_config_t constraints = modulation_for(facts, config);
    status = example_motor_path_validate(config, facts);
    if (status == NX_SUCCESS) {
        status = nx_modulation_check(&constraints);
    }
    if (status != NX_SUCCESS) {
        return status;
    }
    nx_motor_config_t motor = config->motor;
    motor.clock_epoch = facts->clock_epoch;
    nx_axis_config_t axis = config->axis;
    axis.clock_domain = motor.clock_domain;
    axis.epoch = facts->clock_epoch;
    status = nx_drive_references_initialize(
        &drive->references, drive->reference_slots, 4, motor.clock_domain,
        facts->clock_epoch);
    if (status == NX_SUCCESS) {
        status = nx_drive_feedback_initialize(
            &drive->feedback, drive->feedback_slots, 4, motor.clock_domain,
            facts->clock_epoch);
    }
    if (status == NX_SUCCESS) {
        status =
            nx_drive_initialize(&drive->context.fast, &motor,
                                &drive->references, config->motor_revision);
    }
    if (status == NX_SUCCESS) {
        status =
            nx_axis_initialize(&drive->axis, &axis, &config->axis_parameters);
    }
    if (status == NX_SUCCESS) {
        status = nx_drive_feedback_sampler_initialize(
            &drive->context.feedback_sampler, motor.clock_domain,
            facts->clock_epoch, config->feedback_division,
            config->motor.maximum_gap_us);
    }
    if (status != NX_SUCCESS) {
        return status;
    }
    const uint64_t skew =
        ((uint64_t)config->maximum_scan_span_us * facts->tick_hz + 999999) /
        1000000;
    if (skew > UINT32_MAX) {
        return NX_ERROR_INVALID;
    }
    drive->context.calibration.maximum_skew_ticks = (uint32_t)skew;
    drive->context.run_until = end;
    drive->authorized_until = end;
    drive->context.startup_until = startup;
    status = publish(drive, now, 0, 0, now, true);
    nx_power_stage_plan_t initial = {0};
    if (status == NX_SUCCESS) {
        status =
            example_motor_path_initial(&drive->context, &constraints, &initial);
    }
    if (status == NX_SUCCESS) {
        status = nx_power_stage_prepare(&drive->session, &initial, compute,
                                        &drive->context, sizeof(drive->context),
                                        startup);
    }
    if (status == NX_SUCCESS) {
        status = nx_power_stage_start(drive->port, &drive->session);
    }
    if (status == NX_SUCCESS) {
        drive->borrowed = true;
        drive->run_until = end - config->motor.nominal_period_us;
    }
    return status;
}

nx_result_t example_motor_drive_accept(example_motor_drive_t* drive,
                                       const nx_axis_command_t* command,
                                       nx_time_us_t now) {
    if (drive == NULL || command == NULL || !drive->initialized) {
        return NX_ERROR_INVALID;
    }
    if (!drive->borrowed || drive->axis.state == NX_AXIS_DISABLED) {
        return NX_ERROR_BUSY;
    }
    return nx_axis_accept(&drive->axis, command, now);
}

nx_result_t
example_motor_drive_set_limits(example_motor_drive_t* drive,
                               const example_motor_drive_limits_t* limits,
                               nx_time_us_t now) {
    if (drive == NULL || limits == NULL || !drive->initialized ||
        !isfinite(limits->current.minimum_current_a) ||
        !isfinite(limits->current.maximum_current_a) ||
        limits->current.minimum_current_a > 0 ||
        limits->current.maximum_current_a < 0 || limits->revision == 0 ||
        limits->sampled_at > now || limits->valid_until == UINT64_MAX) {
        return NX_ERROR_INVALID;
    }
    if (limits->valid_until <= now) {
        return NX_ERROR_TIMEOUT;
    }
    if (!drive->borrowed || drive->stopping || drive->primary != NX_SUCCESS) {
        return NX_ERROR_STATE;
    }
    if (limits->clock_domain != drive->references.clock_domain ||
        limits->epoch != drive->references.epoch) {
        return NX_ERROR_INVALID;
    }
    const float available =
        sqrtf(fmaxf(0, drive->config.motor.current_limit_a *
                               drive->config.motor.current_limit_a -
                           drive->config.id_a * drive->config.id_a));
    if (limits->current.minimum_current_a < -available ||
        limits->current.maximum_current_a > available ||
        (drive->limits_active &&
         (limits->revision <= drive->limits.revision ||
          limits->sampled_at < drive->limits.sampled_at))) {
        return NX_ERROR_INVALID;
    }
    if (!drive->have_feedback ||
        drive->last_feedback.reference_generation <= 1 ||
        limits->valid_until < drive->last_reference_until) {
        return NX_ERROR_BUSY;
    }
    drive->limits = *limits;
    drive->limits_active = true;
    return NX_SUCCESS;
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

static nx_result_t outer_step(example_motor_drive_t* drive, nx_time_us_t now) {
    if (drive->limits_active && now >= drive->limits.valid_until) {
        return NX_ERROR_TIMEOUT;
    }
    nx_drive_feedback_t record;
    nx_result_t status = nx_drive_feedback_take(&drive->feedback, &record);
    if (status == NX_ERROR_EMPTY) {
        return NX_SUCCESS;
    }
    if (status != NX_SUCCESS) {
        return status;
    }
    drive->last_feedback = record;
    drive->have_feedback = true;
    if (drive->axis.state == NX_AXIS_DISABLED) {
        status = nx_axis_enable(&drive->axis, &record.mechanical, now);
        if (status != NX_SUCCESS) {
            return status;
        }
        const example_motor_drive_config_t* config = &drive->config;
        const nx_axis_command_t initial = {
            .mode = config->initial_mode,
            .clock_domain = drive->axis.config.clock_domain,
            .epoch = drive->axis.config.epoch,
            .sequence = 1,
            .received_at = drive->axis.last_step,
            .valid_until = drive->authorized_until,
            .position_rad = config->initial_position_rad,
            .velocity_rad_s = config->initial_velocity_rad_s,
            .current_a = config->iq_a};
        status = nx_axis_accept(&drive->axis, &initial, now);
        if (status == NX_SUCCESS &&
            config->topology != NX_MODULATION_SINGLE_SHUNT) {
            status = publish(drive, now, 0, 0, now, false);
        }
        return status;
    }
    const uint64_t stop_guard = (uint64_t)drive->config.axis.maximum_step_us +
                                drive->config.motor.nominal_period_us;
    const nx_axis_command_t* authority = drive->axis.command_pending
                                             ? &drive->axis.pending_command
                                             : &drive->axis.command;
    if ((drive->axis.command_pending || drive->axis.command_live) &&
        (authority->valid_until <= now ||
         authority->valid_until - now <= stop_guard)) {
        status = nx_axis_request_stop(&drive->axis);
        if (status != NX_SUCCESS) {
            return status;
        }
    }
    const float current_limit =
        sqrtf(fmaxf(0, drive->config.motor.current_limit_a *
                               drive->config.motor.current_limit_a -
                           drive->config.id_a * drive->config.id_a));
    nx_axis_limits_t limits = {-current_limit, current_limit};
    if (drive->limits_active) {
        limits = drive->limits.current;
    }
    const float dt = (float)(now - drive->axis.last_step) * 0.000001F;
    status = nx_axis_step(&drive->axis, &record.mechanical, &limits, now, dt,
                          &drive->axis_output);
    if (status != NX_SUCCESS) {
        return status;
    }
    if (drive->axis_output.state == NX_AXIS_READY) {
        drive->stopping = true;
        return NX_SUCCESS;
    }
    const bool local_stop = drive->axis_output.state == NX_AXIS_STOPPING ||
                            drive->axis_output.state == NX_AXIS_HOLDING;
    const nx_time_us_t receipt =
        local_stop ? now : drive->axis.command.received_at;
    return publish(drive, now, drive->axis_output.current_a,
                   drive->axis_output.command_sequence, receipt, false);
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
    nx_result_t status = nx_power_stage_service(drive->port);
    if (status == NX_SUCCESS || status == NX_ERROR_BUSY) {
        status = outer_step(drive, now);
    }
    if (nx_request_state(&drive->session.base) == NX_REQUEST_SETTLED) {
        return settled(drive);
    }
    if (drive->stopping) {
        return example_motor_drive_stop(drive);
    }
    if (status != NX_SUCCESS && status != NX_ERROR_BUSY) {
        if (drive->primary == NX_SUCCESS) {
            drive->primary = status;
        }
        (void)nx_drive_inhibit(&drive->references, status);
        return example_motor_drive_stop(drive);
    }
    return NX_ERROR_BUSY;
}
