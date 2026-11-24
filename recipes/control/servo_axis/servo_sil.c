/**
 * \file            servo_sil.c
 * \brief           Finite sensor-only servo cascade against independent truth
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-24
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "servo_sil.h"
#include <math.h>

static bool aligned_optional(uint32_t time, uint32_t duration) {
    return time == 0 || (time >= 2000 && time <= duration && time % 1000 == 0);
}

static bool valid_experiment(const example_servo_experiment_t* e) {
    return e != NULL && e->duration_us >= 2000 && e->duration_us <= 60000000 &&
           e->duration_us % 1000 == 0 && e->evaluation_start_us >= 2000 &&
           e->evaluation_start_us % 1000 == 0 && e->evaluation_samples > 0 &&
           (uint64_t)e->evaluation_start_us +
                   (uint64_t)e->evaluation_samples * 1000 <=
               e->duration_us &&
           e->valid_until_us > 2000 &&
           (e->mode == NX_AXIS_CURRENT || e->mode == NX_AXIS_VELOCITY ||
            e->mode == NX_AXIS_POSITION) &&
           isfinite(e->position_rad) && isfinite(e->velocity_rad_s) &&
           isfinite(e->current_a) && isfinite(e->load_nm) &&
           isfinite(e->derated_current_limit_a) &&
           e->derated_current_limit_a > 0 &&
           aligned_optional(e->load_at_us, e->duration_us) &&
           aligned_optional(e->parameters_at_us, e->duration_us) &&
           aligned_optional(e->derating_at_us, e->duration_us) &&
           aligned_optional(e->stop_at_us, e->duration_us) &&
           aligned_optional(e->drop_record_at_us, e->duration_us) &&
           aligned_optional(e->sensor_fault_at_us, e->duration_us);
}

static nx_result_t observe(nx_encoder_tracker_t* tracker,
                           const example_drive_sensor_t* sensor,
                           const example_servo_config_t* config,
                           nx_time_us_t now, nx_axis_feedback_t* feedback) {
    nx_encoder_estimate_t estimate;
    const float scale =
        (float)(2 * acos(-1.0) / config->encoder_counts_per_turn);
    const nx_result_t status = nx_encoder_tracker_step(
        tracker, sensor->encoder_count, scale, 0.001F, &estimate);
    if (status != NX_SUCCESS) {
        return status;
    }
    const nx_axis_feedback_t next = {
        .clock_domain = config->axis.clock_domain,
        .epoch = config->axis.epoch,
        .sequence = (uint32_t)(now / 1000),
        .interval_start = now - 1000,
        .sampled_at = now,
        .position_rad = (float)estimate.position_counts * scale,
        .velocity_rad_s = estimate.velocity,
        .valid =
            sensor->valid && !sensor->saturated && estimate.velocity_valid};
    *feedback = next;
    return NX_SUCCESS;
}

example_servo_experiment_t example_servo_reference(void) {
    const example_servo_experiment_t result = {
        .control = {.axis = {1, 1, 2000, 2000, NX_AXIS_HOLD_ZERO_VELOCITY},
                    .parameters = {1, 0.8F, 8, 20, 3, 8, 3, 2, 4, -100, 100,
                                   0.005F, 0.02F, 20000, 0},
                    .current_pi = {2, 200, 0, 0, -12, 12, -12, 12},
                    .encoder_counts_per_turn = 1000000,
                    .current_a_per_code = 0.0005F,
                    .current_zero_code = 32768},
        .plant = {1, 0.01, 0.1, 0.1, 0.002, 0.02, 12, 1000000, 0.0005, 32768},
        .mode = NX_AXIS_POSITION,
        .position_rad = 1,
        .duration_us = 6001000,
        .valid_until_us = 10000000,
        .evaluation_start_us = 4000000,
        .evaluation_samples = 1000,
        .load_at_us = 1500000,
        .load_nm = 0.03,
        .parameters_at_us = 2000000,
        .candidate_parameters = {2, 0.9F, 9, 20, 3, 8, 3, 2, 4, -100, 100,
                                 0.005F, 0.02F, 20000, 0},
        .derated_current_limit_a = 3};
    return result;
}

static void record_sample(example_servo_report_t* report,
                          const nx_axis_output_t* output,
                          const nx_axis_feedback_t* feedback, float current,
                          float interval_velocity, float sampled_current_target,
                          double squared[3]) {
    const float errors[3] = {output->reference_position_rad -
                                 feedback->position_rad,
                             interval_velocity - feedback->velocity_rad_s,
                             sampled_current_target - current};
    float* peaks[3] = {&report->peak_position_error_rad,
                       &report->peak_velocity_error_rad_s,
                       &report->peak_current_error_a};
    for (size_t i = 0; i < 3; ++i) {
        *peaks[i] = fmaxf(*peaks[i], fabsf(errors[i]));
        squared[i] += (double)errors[i] * errors[i];
    }
    ++report->samples;
}

nx_result_t example_servo_run(const example_servo_experiment_t* experiment,
                              example_servo_report_t* report) {
    if (report == NULL || !valid_experiment(experiment)) {
        return NX_ERROR_INVALID;
    }
    const uintptr_t e = (uintptr_t)experiment;
    const uintptr_t r = (uintptr_t)report;
    if (sizeof(*experiment) > UINTPTR_MAX - e ||
        sizeof(*report) > UINTPTR_MAX - r ||
        (e < r + sizeof(*report) && r < e + sizeof(*experiment))) {
        return NX_ERROR_INVALID;
    }
    example_servo_control_t control;
    example_drive_plant_t plant;
    nx_encoder_tracker_t tracker;
    example_servo_report_t result = {0};
    nx_result_t status =
        example_servo_control_initialize(&control, &experiment->control);
    if (status == NX_SUCCESS) {
        status = example_drive_plant_initialize(&plant, &experiment->plant);
    }
    if (status == NX_SUCCESS) {
        status =
            nx_encoder_tracker_init(&tracker, UINT64_C(4294967296), 1000000, 0);
    }
    if (status != NX_SUCCESS) {
        return status;
    }
    example_drive_sensor_t sensor;
    status = example_drive_plant_sense(&plant, true, &sensor);
    if (status != NX_SUCCESS) {
        return status;
    }
    nx_encoder_estimate_t reference;
    status = nx_encoder_tracker_step(&tracker, sensor.encoder_count, 1, 0.001F,
                                     &reference);
    nx_axis_feedback_t feedback;
    if (status == NX_SUCCESS) {
        status =
            observe(&tracker, &sensor, &experiment->control, 1000, &feedback);
    }
    if (status == NX_SUCCESS) {
        status = nx_axis_enable(&control.axis, &feedback, 1000);
    }
    const nx_axis_command_t command = {
        .mode = experiment->mode,
        .clock_domain = experiment->control.axis.clock_domain,
        .epoch = experiment->control.axis.epoch,
        .sequence = 1,
        .received_at = 1000,
        .valid_until = experiment->valid_until_us,
        .position_rad = experiment->position_rad,
        .velocity_rad_s = experiment->mode == NX_AXIS_VELOCITY
                              ? experiment->velocity_rad_s
                              : 0,
        .current_a = experiment->current_a};
    if (status == NX_SUCCESS) {
        status = nx_axis_accept(&control.axis, &command, 1000);
    }
    if (status != NX_SUCCESS) {
        return status;
    }
    double squared[3] = {0};
    nx_time_us_t stationary_since = 0;
    const nx_time_us_t evaluation_end =
        experiment->evaluation_start_us +
        (nx_time_us_t)experiment->evaluation_samples * 1000;
    nx_axis_output_t output = {0};
    for (nx_time_us_t now = 1050; now <= experiment->duration_us; now += 50) {
        const double load =
            experiment->load_at_us != 0 && now >= experiment->load_at_us
                ? experiment->load_nm
                : 0;
        status = example_drive_plant_advance(&plant, 0.00005, load);
        if (status == NX_SUCCESS) {
            status = example_drive_plant_sense(
                &plant,
                experiment->sensor_fault_at_us == 0 ||
                    now < experiment->sensor_fault_at_us,
                &sensor);
        }
        if (status != NX_SUCCESS) {
            return status;
        }
        const float current = ((float)sensor.current_code -
                               experiment->control.current_zero_code) *
                              experiment->control.current_a_per_code;
        if (now % 1000 == 0) {
            const float interval_velocity = output.reference_velocity_rad_s;
            const float sampled_current_target = output.current_a;
            status = observe(&tracker, &sensor, &experiment->control, now,
                             &feedback);
            if (status != NX_SUCCESS) {
                return status;
            }
            if (now == experiment->parameters_at_us &&
                (control.axis.state == NX_AXIS_RUNNING ||
                 control.axis.state == NX_AXIS_READY)) {
                status = nx_axis_stage_parameters(
                    &control.axis, &experiment->candidate_parameters);
            }
            if (status == NX_SUCCESS && now == experiment->stop_at_us) {
                status = nx_axis_request_stop(&control.axis);
                if (status == NX_SUCCESS) {
                    result.stop_adopted_at = now;
                }
            }
            const float limit =
                experiment->derating_at_us != 0 &&
                        now >= experiment->derating_at_us
                    ? experiment->derated_current_limit_a
                    : experiment->control.parameters.current_limit_a;
            const nx_axis_limits_t limits = {-limit, limit};
            if (status == NX_SUCCESS && control.axis.state != NX_AXIS_FAULT) {
                status = nx_axis_step(&control.axis, &feedback, &limits, now,
                                      0.001F, &output);
            }
            if (status != NX_SUCCESS) {
                return status;
            }
            if (result.adopted_at == 0 && output.command_sequence != 0) {
                result.adopted_at = output.adopted_at;
            }
            if (result.trajectory_complete_at == 0 &&
                output.trajectory_complete) {
                result.trajectory_complete_at = now;
            }
            if (result.measured_reached_at == 0 && output.target_reached) {
                result.measured_reached_at = now;
            }
            result.command_expired |= output.command_expired;
            if (result.stop_adopted_at == 0 && output.command_expired) {
                result.stop_adopted_at = now;
            }
            if (result.stop_adopted_at != 0 &&
                fabs(plant.velocity_rad_s) <= 0.02) {
                if (stationary_since == 0) {
                    stationary_since = now;
                }
                if (result.model_stopped_at == 0 &&
                    now - stationary_since >= 20000) {
                    result.model_stopped_at = now;
                }
            } else {
                stationary_since = 0;
            }
            if (fabsf(output.requested_current_a - output.current_a) >
                0.00001F) {
                ++result.limited_samples;
            }
            if (now >= experiment->evaluation_start_us &&
                now < evaluation_end && now != experiment->drop_record_at_us) {
                record_sample(&result, &output, &feedback, current,
                              interval_velocity, sampled_current_target,
                              squared);
            }
        }
        float voltage;
        status = example_servo_current_step(&control, current,
                                            sensor.valid && !sensor.saturated,
                                            0.00005F, &voltage);
        if (status == NX_SUCCESS) {
            status = example_drive_plant_stage(&plant, voltage);
        }
        if (status != NX_SUCCESS) {
            return status;
        }
    }
    if (result.samples != 0) {
        result.rms_position_error_rad = sqrt(squared[0] / result.samples);
        result.rms_velocity_error_rad_s = sqrt(squared[1] / result.samples);
        result.rms_current_error_a = sqrt(squared[2] / result.samples);
    }
    result.parameter_revision = output.parameters_revision;
    result.measured_position_rad = feedback.position_rad;
    result.measured_velocity_rad_s = feedback.velocity_rad_s;
    result.truth_position_rad = plant.position_rad;
    result.truth_velocity_rad_s = plant.velocity_rad_s;
    result.truth_current_a = plant.current_a;
    result.final_state = control.axis.state;
    result.axis_fault = control.axis.fault;
    result.target_reached = output.target_reached;
    result.data_complete = result.samples == experiment->evaluation_samples;
    result.tracking_passed =
        result.data_complete && result.stop_adopted_at == 0 &&
        result.peak_current_error_a < 0.02F &&
        (experiment->mode == NX_AXIS_CURRENT ||
         result.peak_velocity_error_rad_s < 0.03F) &&
        (experiment->mode != NX_AXIS_POSITION ||
         (result.target_reached && result.peak_position_error_rad < 0.01F));
    *report = result;
    return NX_SUCCESS;
}
