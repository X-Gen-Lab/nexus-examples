/**
 * \file            drive.c
 * \brief           Paired velocity admission and explicit DC current control
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-24
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "drive.h"
#include <math.h>
#include <stddef.h>

nx_result_t example_drive_initialize(example_drive_t* drive,
                                     const example_drive_config_t* config) {
    if (drive == NULL || config == NULL || !isfinite(config->radius_m) ||
        config->radius_m <= 0 || !isfinite(config->track_m) ||
        config->track_m <= 0 || !isfinite(config->wheel_limit_rad_s) ||
        config->wheel_limit_rad_s <= 0 || config->command_lifetime_us == 0 ||
        config->wheel_limit_rad_s > config->parameters.velocity_limit_rad_s) {
        return NX_ERROR_INVALID;
    }
    example_drive_t next = {.config = *config};
    for (size_t i = 0; i < 2; ++i) {
        if (nx_axis_initialize(&next.axis[i], &config->axis,
                               &config->parameters) != NX_SUCCESS ||
            nx_pid_initialize(&next.current_pi[i], &config->current_pi) !=
                NX_SUCCESS) {
            return NX_ERROR_INVALID;
        }
    }
    next.initialized = true;
    *drive = next;
    return NX_SUCCESS;
}

nx_result_t example_drive_enable(example_drive_t* drive,
                                 const nx_axis_feedback_t feedback[2],
                                 nx_time_us_t now) {
    if (drive == NULL || feedback == NULL || !drive->initialized) {
        return NX_ERROR_INVALID;
    }
    if (drive->enabled || drive->fault != NX_SUCCESS) {
        return NX_ERROR_STATE;
    }
    nx_axis_t axes[2] = {drive->axis[0], drive->axis[1]};
    for (size_t i = 0; i < 2; ++i) {
        const nx_result_t result = nx_axis_enable(&axes[i], &feedback[i], now);
        if (result != NX_SUCCESS) {
            return result;
        }
    }
    drive->axis[0] = axes[0];
    drive->axis[1] = axes[1];
    drive->enabled = true;
    return NX_SUCCESS;
}

nx_result_t example_drive_accept(example_drive_t* drive,
                                 const example_drive_command_t* command,
                                 nx_time_us_t now) {
    if (drive == NULL || command == NULL || !drive->initialized ||
        !isfinite(command->linear_m_s) || !isfinite(command->angular_rad_s) ||
        command->received_at >=
            NX_DEADLINE_NEVER - drive->config.command_lifetime_us) {
        return NX_ERROR_INVALID;
    }
    if (!drive->enabled || drive->fault != NX_SUCCESS) {
        return NX_ERROR_STATE;
    }
    const float turn = drive->config.track_m * command->angular_rad_s / 2;
    const float raw[2] = {(command->linear_m_s - turn) / drive->config.radius_m,
                          (command->linear_m_s + turn) /
                              drive->config.radius_m};
    if (!isfinite(raw[0]) || !isfinite(raw[1])) {
        return NX_ERROR_EXHAUSTED;
    }
    const float magnitude = fmaxf(fabsf(raw[0]), fabsf(raw[1]));
    const float scale = magnitude > drive->config.wheel_limit_rad_s
                            ? drive->config.wheel_limit_rad_s / magnitude
                            : 1;
    nx_axis_t axes[2] = {drive->axis[0], drive->axis[1]};
    for (size_t i = 0; i < 2; ++i) {
        const nx_axis_command_t target = {.mode = NX_AXIS_VELOCITY,
                                          .clock_domain = command->clock_domain,
                                          .epoch = command->epoch,
                                          .sequence = command->sequence,
                                          .received_at = command->received_at,
                                          .valid_until =
                                              command->received_at +
                                              drive->config.command_lifetime_us,
                                          .velocity_rad_s = raw[i] * scale};
        const nx_result_t result = nx_axis_accept(&axes[i], &target, now);
        if (result != NX_SUCCESS) {
            return result;
        }
    }
    drive->axis[0] = axes[0];
    drive->axis[1] = axes[1];
    drive->observation.generation = command->sequence;
    drive->observation.scale = scale;
    for (size_t i = 0; i < 2; ++i) {
        drive->observation.raw_wheel_rad_s[i] = raw[i];
        drive->observation.limited_wheel_rad_s[i] = raw[i] * scale;
    }
    return NX_SUCCESS;
}

nx_result_t example_drive_step(example_drive_t* drive,
                               const nx_axis_feedback_t feedback[2],
                               nx_time_us_t now, float dt_s,
                               example_drive_observation_t* out) {
    if (drive == NULL || feedback == NULL || out == NULL ||
        !drive->initialized) {
        return NX_ERROR_INVALID;
    }
    if (!drive->enabled || drive->fault != NX_SUCCESS) {
        return NX_ERROR_STATE;
    }
    nx_axis_t axes[2] = {drive->axis[0], drive->axis[1]};
    example_drive_observation_t next = drive->observation;
    const nx_axis_limits_t limits = {-drive->config.parameters.current_limit_a,
                                     drive->config.parameters.current_limit_a};
    for (size_t i = 0; i < 2; ++i) {
        /* An expired axis may finish before its peer. Preserve its latched
         * zero-current decision while the other axis completes stopping. */
        if (next.axis[i].state == NX_AXIS_FAULT &&
            next.axis[i].command_expired && next.axis[i].current_a == 0) {
            continue;
        }
        const nx_result_t result = nx_axis_step(&axes[i], &feedback[i], &limits,
                                                now, dt_s, &next.axis[i]);
        if (result != NX_SUCCESS) {
            drive->fault = result;
            return result;
        }
    }
    drive->axis[0] = axes[0];
    drive->axis[1] = axes[1];
    drive->observation = next;
    *out = next;
    return NX_SUCCESS;
}

nx_result_t example_drive_current_step(example_drive_t* drive,
                                       const float current_a[2], bool valid,
                                       float dt_s, float voltage_v[2]) {
    if (drive == NULL || current_a == NULL || voltage_v == NULL ||
        !drive->initialized) {
        return NX_ERROR_INVALID;
    }
    if (!drive->enabled || drive->fault != NX_SUCCESS) {
        return NX_ERROR_STATE;
    }
    if (!valid) {
        drive->fault = NX_ERROR_IO;
        return NX_ERROR_IO;
    }
    nx_pid_t controllers[2] = {drive->current_pi[0], drive->current_pi[1]};
    float next[2];
    for (size_t i = 0; i < 2; ++i) {
        const nx_result_t result =
            nx_pid_step(&controllers[i], drive->observation.axis[i].current_a,
                        current_a[i], dt_s, &next[i]);
        if (result != NX_SUCCESS) {
            drive->fault = result;
            return result;
        }
    }
    drive->current_pi[0] = controllers[0];
    drive->current_pi[1] = controllers[1];
    voltage_v[0] = next[0];
    voltage_v[1] = next[1];
    return NX_SUCCESS;
}
