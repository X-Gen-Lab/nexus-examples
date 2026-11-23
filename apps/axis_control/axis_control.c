/**
 * \file            axis_control.c
 * \brief           Explicit rotary control and latest-copy telemetry reference
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-22
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "axis_control.h"
#include <math.h>
#include <string.h>

static bool valid_parameters(const example_axis_parameters_t* parameters) {
    nx_pid_t pid;
    return parameters != NULL && parameters->revision != 0 &&
           isfinite(parameters->minimum_position_rad) &&
           isfinite(parameters->maximum_position_rad) &&
           parameters->minimum_position_rad <
               parameters->maximum_position_rad &&
           isfinite(parameters->maximum_velocity_rad_s) &&
           parameters->maximum_velocity_rad_s > 0 &&
           isfinite(parameters->acceleration_rad_s2) &&
           parameters->acceleration_rad_s2 > 0 &&
           parameters->pid.output_minimum >= -1 &&
           parameters->pid.output_maximum <= 1 &&
           parameters->pid.output_minimum <= 0 &&
           parameters->pid.output_maximum >= 0 &&
           nx_pid_initialize(&pid, &parameters->pid) == NX_SUCCESS;
}

static bool position_allowed(const example_axis_parameters_t* parameters,
                             float position) {
    return isfinite(position) && position >= parameters->minimum_position_rad &&
           position <= parameters->maximum_position_rad;
}

static void apply_parameters(example_axis_t* axis) {
    if (axis->parameters_pending) {
        axis->parameters = axis->pending_parameters;
        axis->pid = axis->pending_pid;
        axis->parameters_pending = false;
    }
}

nx_result_t
example_axis_initialize(example_axis_t* axis,
                        const example_axis_config_t* config,
                        const example_axis_parameters_t* parameters) {
    nx_encoder_tracker_t tracker;
    float calibrated;
    if (axis == NULL || config == NULL || config->encoder == NULL ||
        config->pwm == NULL || config->pwm_period_ticks == 0 ||
        config->trigger_ticks > config->pwm_period_ticks ||
        config->maximum_step_gap_us == 0 || config->hardware_identity == 0 ||
        config->clock_domain == 0 || config->epoch == 0 ||
        config->calibration_revision == 0 || config->calibration.scale == 0 ||
        !valid_parameters(parameters) ||
        nx_calibration_apply_f32(&config->calibration, 0, &calibrated) !=
            NX_SUCCESS ||
        nx_encoder_tracker_init(&tracker, config->encoder_modulus,
                                config->maximum_delta_counts,
                                0) != NX_SUCCESS) {
        return NX_ERROR_INVALID;
    }
    example_axis_t next = {0};
    next.config = *config;
    next.parameters = *parameters;
    next.tracker = tracker;
    (void)nx_pid_initialize(&next.pid, &parameters->pid);
    next.initialized = true;
    *axis = next;
    return NX_SUCCESS;
}

nx_result_t example_axis_stop(example_axis_t* axis) {
    if (axis == NULL || !axis->initialized) {
        return NX_ERROR_INVALID;
    }
    axis->command_live = false;
    nx_result_t first = NX_SUCCESS;
    if (axis->pwm_started) {
        first = nx_pwm_group_stop(axis->config.pwm);
        if (first == NX_SUCCESS) {
            axis->pwm_started = false;
        }
    }
    if (axis->encoder_started) {
        const nx_result_t result = nx_encoder_stop(axis->config.encoder);
        if (result == NX_SUCCESS) {
            axis->encoder_started = false;
        } else if (first == NX_SUCCESS) {
            first = result;
        }
    }
    if (first != NX_SUCCESS && axis->fault == NX_SUCCESS) {
        axis->fault = first;
    }
    axis->state =
        axis->fault == NX_SUCCESS ? EXAMPLE_AXIS_DISABLED : EXAMPLE_AXIS_FAULT;
    return first;
}

static nx_result_t fault_axis(example_axis_t* axis, nx_result_t result) {
    if (axis->fault == NX_SUCCESS) {
        axis->fault = result;
    }
    (void)example_axis_stop(axis);
    return result;
}

static nx_result_t
read_position(example_axis_t* axis, nx_encoder_tracker_t* tracker, float* dt_s,
              nx_time_us_t release, nx_encoder_sample_t* sample,
              nx_encoder_estimate_t* estimate, float* position) {
    nx_result_t result = nx_encoder_read(axis->config.encoder, sample);
    if (result != NX_SUCCESS) {
        return result;
    }
    if (sample->observed_at < release ||
        sample->observed_at - release > axis->config.maximum_step_gap_us ||
        sample->observed_at == NX_DEADLINE_NEVER) {
        return NX_ERROR_STATE;
    }
    if (sample->observed_at <= axis->last_observed) {
        return NX_ERROR_STATE;
    }
    if (sample->observed_at - axis->last_observed >
            axis->config.maximum_step_gap_us ||
        (axis->command_live && sample->observed_at >= axis->command_deadline)) {
        return NX_ERROR_TIMEOUT;
    }
    *dt_s = (float)(sample->observed_at - axis->last_observed) * 0.000001F;
    result = nx_encoder_tracker_step(tracker, sample->count,
                                     axis->config.calibration.scale, *dt_s,
                                     estimate);
    if (result == NX_SUCCESS) {
        result = nx_calibration_apply_f32(&axis->config.calibration,
                                          (float)estimate->position_counts,
                                          position);
    }
    if (result == NX_SUCCESS &&
        !position_allowed(&axis->parameters, *position)) {
        result = NX_ERROR_STATE;
    }
    return result;
}

nx_result_t example_axis_start(example_axis_t* axis, nx_time_us_t now) {
    if (axis == NULL || !axis->initialized) {
        return NX_ERROR_INVALID;
    }
    if (axis->state != EXAMPLE_AXIS_DISABLED || axis->fault != NX_SUCCESS ||
        axis->encoder_started || axis->pwm_started ||
        now == NX_DEADLINE_NEVER) {
        return NX_ERROR_STATE;
    }
    apply_parameters(axis);
    axis->encoder_started = true;
    nx_result_t result = nx_encoder_start(axis->config.encoder);
    nx_encoder_sample_t sample = {0};
    nx_encoder_estimate_t estimate;
    float position = 0;
    if (result == NX_SUCCESS) {
        result = nx_encoder_read(axis->config.encoder, &sample);
    }
    if (result == NX_SUCCESS) {
        result = nx_encoder_tracker_init(
            &axis->tracker, axis->config.encoder_modulus,
            axis->config.maximum_delta_counts, sample.count);
    }
    if (result == NX_SUCCESS &&
        (sample.observed_at < now || sample.observed_at == NX_DEADLINE_NEVER ||
         sample.observed_at - now > axis->config.maximum_step_gap_us)) {
        result = NX_ERROR_STATE;
    }
    if (result == NX_SUCCESS) {
        result = nx_encoder_tracker_step(&axis->tracker, sample.count,
                                         axis->config.calibration.scale, 1,
                                         &estimate);
    }
    if (result == NX_SUCCESS) {
        result = nx_calibration_apply_f32(&axis->config.calibration,
                                          (float)estimate.position_counts,
                                          &position);
    }
    if (result == NX_SUCCESS &&
        !position_allowed(&axis->parameters, position)) {
        result = NX_ERROR_STATE;
    }
    if (result == NX_SUCCESS) {
        result = nx_pid_track(&axis->pid, position, position, 0);
    }
    const uint32_t neutral[2] = {0, 0};
    const nx_pwm_group_frame_t frame = {neutral, 2, axis->config.trigger_ticks};
    if (result == NX_SUCCESS) {
        axis->pwm_started = true;
        result = nx_pwm_group_commit(axis->config.pwm, &frame);
    }
    if (result == NX_SUCCESS) {
        result = nx_pwm_group_start(axis->config.pwm);
    }
    if (result != NX_SUCCESS) {
        return fault_axis(axis, result);
    }
    axis->last_cycle = now;
    axis->last_observed = sample.observed_at;
    axis->position_rad = position;
    axis->state = EXAMPLE_AXIS_READY;
    return NX_SUCCESS;
}

nx_result_t example_axis_command(example_axis_t* axis,
                                 const example_axis_command_t* command,
                                 nx_time_us_t now) {
    if (axis == NULL || !axis->initialized || command == NULL ||
        command->unit != EXAMPLE_AXIS_POSITION_RADIANS ||
        !position_allowed(&axis->parameters, command->position_rad) ||
        command->sequence == 0 || command->valid_until == NX_DEADLINE_NEVER ||
        now == NX_DEADLINE_NEVER) {
        return NX_ERROR_INVALID;
    }
    if (command->epoch != axis->config.epoch ||
        command->sequence <= axis->last_sequence || now < axis->last_cycle ||
        axis->state == EXAMPLE_AXIS_DISABLED ||
        axis->state == EXAMPLE_AXIS_FAULT) {
        return NX_ERROR_STATE;
    }
    if (now >= command->valid_until) {
        return NX_ERROR_TIMEOUT;
    }
    if (axis->state == EXAMPLE_AXIS_MOVING || axis->parameters_pending) {
        return NX_ERROR_BUSY;
    }
    nx_trajectory_plan_t plan;
    const nx_result_t result =
        nx_trajectory_plan(&plan, axis->position_rad, command->position_rad,
                           axis->parameters.maximum_velocity_rad_s,
                           axis->parameters.acceleration_rad_s2);
    if (result != NX_SUCCESS) {
        return result;
    }
    axis->trajectory = plan;
    axis->command_start = now;
    axis->command_deadline = command->valid_until;
    axis->last_sequence = command->sequence;
    axis->command_live = true;
    axis->state = EXAMPLE_AXIS_MOVING;
    return NX_SUCCESS;
}

static bool valid_cycle(const example_axis_t* axis,
                        const example_axis_cycle_t* cycle) {
    return cycle != NULL && cycle->valid &&
           cycle->release.source == NX_IO_TIMESTAMP_SERVICE_OBSERVATION &&
           cycle->release.domain == axis->config.clock_domain &&
           cycle->release.epoch == axis->config.epoch &&
           cycle->release.ticks_per_second == 1000000 &&
           cycle->release.ticks != NX_DEADLINE_NEVER &&
           axis->sample_sequence != UINT32_MAX &&
           cycle->sequence == axis->sample_sequence + 1 &&
           cycle->calibration_revision == axis->config.calibration_revision;
}

nx_result_t example_axis_step(example_axis_t* axis,
                              const example_axis_cycle_t* cycle) {
    if (axis == NULL || !axis->initialized) {
        return NX_ERROR_INVALID;
    }
    if (axis->state != EXAMPLE_AXIS_READY &&
        axis->state != EXAMPLE_AXIS_MOVING) {
        return NX_ERROR_STATE;
    }
    if (!valid_cycle(axis, cycle)) {
        return fault_axis(axis, NX_ERROR_STATE);
    }
    const nx_time_us_t now = cycle->release.ticks;
    if (axis->command_live && now < axis->command_start) {
        return fault_axis(axis, NX_ERROR_STATE);
    }
    if (now <= axis->last_cycle ||
        now - axis->last_cycle > axis->config.maximum_step_gap_us ||
        (axis->command_live && now >= axis->command_deadline)) {
        return fault_axis(axis, NX_ERROR_TIMEOUT);
    }
    apply_parameters(axis);
    nx_encoder_tracker_t tracker = axis->tracker;
    nx_pid_t pid = axis->pid;
    float dt_s = 0;
    nx_encoder_sample_t sample;
    nx_encoder_estimate_t estimate;
    float position;
    nx_result_t result = read_position(axis, &tracker, &dt_s, now, &sample,
                                       &estimate, &position);
    nx_trajectory_sample_t reference = {axis->position_rad, 0, 0, true};
    if (result == NX_SUCCESS && axis->command_live) {
        result = nx_trajectory_evaluate(
            &axis->trajectory,
            (float)(sample.observed_at - axis->command_start) * 0.000001F,
            &reference);
    }
    float output = 0;
    if (result == NX_SUCCESS) {
        result = nx_pid_step(&pid, reference.position, position, dt_s, &output);
    }
    if (result != NX_SUCCESS) {
        return fault_axis(axis, result);
    }
    const double rounded =
        (double)fabsf(output) * axis->config.pwm_period_ticks + 0.5;
    const uint32_t duty = rounded >= axis->config.pwm_period_ticks
                              ? axis->config.pwm_period_ticks
                              : (uint32_t)rounded;
    const uint32_t duties[2] = {output > 0 ? duty : 0, output < 0 ? duty : 0};
    const nx_pwm_group_frame_t frame = {duties, 2, axis->config.trigger_ticks};
    result = nx_pwm_group_commit(axis->config.pwm, &frame);
    if (result != NX_SUCCESS) {
        return fault_axis(axis, result);
    }
    example_axis_observation_t observation = {0};
    observation.observed.ticks = sample.observed_at;
    observation.observed.ticks_per_second = 1000000;
    observation.observed.domain = axis->config.clock_domain;
    observation.observed.epoch = axis->config.epoch;
    observation.observed.source = NX_IO_TIMESTAMP_SERVICE_OBSERVATION;
    observation.sequence = axis->last_sequence;
    observation.sample_sequence = cycle->sequence;
    observation.parameters_revision = axis->parameters.revision;
    observation.calibration_revision = axis->config.calibration_revision;
    observation.overwritten_observations =
        axis->observation.overwritten_observations;
    if (axis->observation_pending &&
        observation.overwritten_observations != UINT32_MAX) {
        ++observation.overwritten_observations;
    }
    observation.position_rad = position;
    observation.velocity_rad_s = estimate.velocity;
    observation.reference_rad = reference.position;
    observation.normalized_output = output;
    observation.valid = true;
    axis->observation = observation;
    axis->observation_pending = true;
    axis->tracker = tracker;
    axis->pid = pid;
    axis->position_rad = position;
    axis->last_cycle = now;
    axis->last_observed = sample.observed_at;
    axis->sample_sequence = cycle->sequence;
    axis->state = reference.complete ? EXAMPLE_AXIS_READY : EXAMPLE_AXIS_MOVING;
    return NX_SUCCESS;
}

nx_result_t
example_axis_stage_parameters(example_axis_t* axis,
                              const example_axis_parameters_t* parameters) {
    if (axis == NULL || !axis->initialized || !valid_parameters(parameters)) {
        return NX_ERROR_INVALID;
    }
    if (axis->state == EXAMPLE_AXIS_FAULT ||
        parameters->revision <= axis->parameters.revision) {
        return NX_ERROR_STATE;
    }
    if (axis->parameters_pending) {
        return NX_ERROR_BUSY;
    }
    if (axis->state != EXAMPLE_AXIS_DISABLED &&
        (!position_allowed(parameters, axis->position_rad) ||
         (axis->command_live &&
          !position_allowed(parameters, axis->trajectory.target)))) {
        return NX_ERROR_INVALID;
    }
    if (axis->state == EXAMPLE_AXIS_MOVING &&
        (parameters->maximum_velocity_rad_s !=
             axis->parameters.maximum_velocity_rad_s ||
         parameters->acceleration_rad_s2 !=
             axis->parameters.acceleration_rad_s2 ||
         parameters->minimum_position_rad !=
             axis->parameters.minimum_position_rad ||
         parameters->maximum_position_rad !=
             axis->parameters.maximum_position_rad)) {
        return NX_ERROR_BUSY;
    }
    nx_pid_t pid = axis->pid;
    const nx_result_t result = axis->state == EXAMPLE_AXIS_DISABLED
                                   ? nx_pid_initialize(&pid, &parameters->pid)
                                   : nx_pid_reconfigure(&pid, &parameters->pid);
    if (result != NX_SUCCESS) {
        return result;
    }
    axis->pending_parameters = *parameters;
    axis->pending_pid = pid;
    axis->parameters_pending = true;
    return NX_SUCCESS;
}

nx_result_t example_axis_apply_parameters(example_axis_t* axis) {
    if (axis == NULL || !axis->initialized) {
        return NX_ERROR_INVALID;
    }
    if (axis->state != EXAMPLE_AXIS_DISABLED || axis->encoder_started ||
        axis->pwm_started) {
        return NX_ERROR_STATE;
    }
    apply_parameters(axis);
    return NX_SUCCESS;
}

nx_result_t example_axis_take_observation(example_axis_t* axis,
                                          example_axis_observation_t* out) {
    if (axis == NULL || !axis->initialized || out == NULL ||
        out == &axis->observation) {
        return NX_ERROR_INVALID;
    }
    if (!axis->observation_pending) {
        return NX_ERROR_EMPTY;
    }
    *out = axis->observation;
    axis->observation_pending = false;
    return NX_SUCCESS;
}
