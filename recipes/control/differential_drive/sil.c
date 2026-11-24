/**
 * \file            sil.c
 * \brief           Independent sensor-to-controller-to-plant experiment
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-24
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "sil.h"
#include <math.h>
#include <stddef.h>

static nx_result_t wheel_observation(nx_encoder_tracker_t* tracker,
                                     const example_drive_sensor_t* sensor,
                                     const example_drive_plant_config_t* config,
                                     uint32_t sequence, nx_time_us_t now,
                                     const nx_axis_config_t* identity,
                                     nx_axis_feedback_t* feedback) {
    nx_encoder_estimate_t estimate;
    const float scale = (float)(2 * acos(-1.0) / config->counts_per_turn);
    const nx_result_t result = nx_encoder_tracker_step(
        tracker, sensor->encoder_count, scale, 0.001F, &estimate);
    if (result != NX_SUCCESS) {
        return result;
    }
    const nx_axis_feedback_t next = {
        .clock_domain = identity->clock_domain,
        .epoch = identity->epoch,
        .sequence = sequence,
        .interval_start = now - 1000,
        .sampled_at = now,
        .position_rad = (float)estimate.position_counts * scale,
        .velocity_rad_s = estimate.velocity,
        .valid = sensor->valid && estimate.velocity_valid};
    *feedback = next;
    return NX_SUCCESS;
}

static bool experiment_valid(const example_drive_experiment_t* experiment) {
    return experiment != NULL && experiment->duration_us >= 2000 &&
           experiment->duration_us <= 60000000 &&
           experiment->duration_us % 1000 == 0 &&
           experiment->evaluation_start_us >= 2000 &&
           experiment->evaluation_start_us % 1000 == 0 &&
           experiment->evaluation_samples > 0 &&
           (uint64_t)experiment->evaluation_start_us +
                   (uint64_t)experiment->evaluation_samples * 1000 <=
               experiment->duration_us &&
           isfinite(experiment->linear_m_s) &&
           isfinite(experiment->angular_rad_s) &&
           isfinite(experiment->distance_m) && experiment->distance_m >= 0 &&
           isfinite(experiment->actual_left_radius_m) &&
           experiment->actual_left_radius_m > 0 &&
           isfinite(experiment->actual_right_radius_m) &&
           experiment->actual_right_radius_m > 0;
}

nx_result_t example_drive_run(const example_drive_experiment_t* experiment,
                              example_drive_report_t* report) {
    if (report == NULL || !experiment_valid(experiment)) {
        return NX_ERROR_INVALID;
    }
    example_drive_t drive;
    example_drive_plant_t plants[2];
    example_drive_sensor_t sensors[2];
    nx_encoder_tracker_t trackers[2];
    nx_axis_feedback_t feedback[2];
    example_drive_report_t result = {0};
    nx_result_t status = example_drive_initialize(&drive, &experiment->control);
    if (status != NX_SUCCESS) {
        return status;
    }
    for (size_t i = 0; i < 2; ++i) {
        status = example_drive_plant_initialize(&plants[i], &experiment->plant);
        if (status != NX_SUCCESS) {
            return status;
        }
        status = nx_encoder_tracker_init(&trackers[i], UINT64_C(4294967296),
                                         1000000, 0);
        if (status != NX_SUCCESS) {
            return status;
        }
        nx_encoder_estimate_t reference;
        status =
            nx_encoder_tracker_step(&trackers[i], 0, 1, 0.001F, &reference);
        if (status != NX_SUCCESS) {
            return status;
        }
        status = example_drive_plant_sense(&plants[i], true, &sensors[i]);
        if (status != NX_SUCCESS) {
            return status;
        }
        status =
            wheel_observation(&trackers[i], &sensors[i], &experiment->plant, 1,
                              1000, &experiment->control.axis, &feedback[i]);
        if (status != NX_SUCCESS) {
            return status;
        }
    }
    status = example_drive_enable(&drive, feedback, 1000);
    if (status != NX_SUCCESS) {
        return status;
    }
    nx_trajectory_plan_t trajectory = {0};
    if (experiment->distance_m > 0) {
        status = nx_trajectory_plan(&trajectory, 0, experiment->distance_m,
                                    fabsf(experiment->linear_m_s), 0.5F);
        if (status != NX_SUCCESS || experiment->angular_rad_s != 0) {
            return NX_ERROR_INVALID;
        }
    }
    example_drive_observation_t observation = {0};
    double squared_error[2] = {0, 0};
    uint32_t command_sequence = 0;
    bool axes_complete = false;
    bool stationary = true;
    const nx_time_us_t evaluation_end =
        experiment->evaluation_start_us +
        (nx_time_us_t)experiment->evaluation_samples * 1000;

    for (nx_time_us_t now = 1000; now <= experiment->duration_us; now += 50) {
        if (now > 1000) {
            const double old_velocity[2] = {plants[0].velocity_rad_s,
                                            plants[1].velocity_rad_s};
            for (size_t i = 0; i < 2; ++i) {
                status = example_drive_plant_advance(&plants[i], 0.00005, 0);
                if (status != NX_SUCCESS) {
                    return status;
                }
            }
            status = example_drive_pose_advance(
                &result.truth, old_velocity[0], old_velocity[1],
                experiment->actual_left_radius_m,
                experiment->actual_right_radius_m, experiment->control.track_m,
                0.00005);
            if (status != NX_SUCCESS) {
                return status;
            }
        }
        for (size_t i = 0; i < 2; ++i) {
            status = example_drive_plant_sense(&plants[i], true, &sensors[i]);
            if (status != NX_SUCCESS) {
                return status;
            }
        }
        if (now % 1000 == 0) {
            const float interval_reference[2] = {
                observation.axis[0].reference_velocity_rad_s,
                observation.axis[1].reference_velocity_rad_s};
            if (now > 1000) {
                for (size_t i = 0; i < 2; ++i) {
                    status = wheel_observation(
                        &trackers[i], &sensors[i], &experiment->plant,
                        (uint32_t)(now / 1000), now, &experiment->control.axis,
                        &feedback[i]);
                    if (status != NX_SUCCESS) {
                        return status;
                    }
                }
                status = example_drive_pose_advance(
                    &result.odometry, feedback[0].velocity_rad_s,
                    feedback[1].velocity_rad_s, experiment->control.radius_m,
                    experiment->control.radius_m, experiment->control.track_m,
                    0.001);
                if (status != NX_SUCCESS) {
                    return status;
                }
            }
            if (!axes_complete && (now - 1000) % 10000 == 0 &&
                (experiment->command_stop_us == 0 ||
                 now < experiment->command_stop_us)) {
                float linear = experiment->linear_m_s;
                if (experiment->distance_m > 0) {
                    nx_trajectory_sample_t point;
                    status = nx_trajectory_evaluate(
                        &trajectory, (float)(now - 1000) * 0.000001F, &point);
                    if (status != NX_SUCCESS) {
                        return status;
                    }
                    linear = point.velocity;
                }
                const example_drive_command_t command = {
                    .clock_domain = experiment->control.axis.clock_domain,
                    .epoch = experiment->control.axis.epoch,
                    .sequence = ++command_sequence,
                    .received_at = now,
                    .linear_m_s = linear,
                    .angular_rad_s = experiment->angular_rad_s};
                status = example_drive_accept(&drive, &command, now);
                if (status != NX_SUCCESS) {
                    return status;
                }
                result.last_valid_received_at = now;
            }
            if (now > 1000 && !axes_complete) {
                status = example_drive_step(&drive, feedback, now, 0.001F,
                                            &observation);
                if (status != NX_SUCCESS) {
                    return status;
                }
                for (size_t i = 0; i < 2; ++i) {
                    if (observation.axis[i].command_expired &&
                        result.stop_reference_at[i] == 0) {
                        result.stop_reference_at[i] = now;
                    }
                }
                axes_complete = observation.axis[0].state == NX_AXIS_FAULT &&
                                observation.axis[1].state == NX_AXIS_FAULT;
            }
            if (now >= experiment->evaluation_start_us &&
                now < evaluation_end && now != experiment->drop_record_at_us) {
                ++result.samples;
                for (size_t i = 0; i < 2; ++i) {
                    const float error =
                        interval_reference[i] - feedback[i].velocity_rad_s;
                    result.peak_error_rad_s[i] =
                        fmaxf(result.peak_error_rad_s[i], fabsf(error));
                    squared_error[i] += (double)error * error;
                }
            }
            result.final_velocity_rad_s[0] = feedback[0].velocity_rad_s;
            result.final_velocity_rad_s[1] = feedback[1].velocity_rad_s;
            if (experiment->duration_us >= 500000 &&
                now >= experiment->duration_us - 500000 &&
                now < experiment->duration_us &&
                now != experiment->drop_record_at_us) {
                ++result.stationary_samples;
                const double linear = (plants[0].velocity_rad_s *
                                           experiment->actual_left_radius_m +
                                       plants[1].velocity_rad_s *
                                           experiment->actual_right_radius_m) /
                                      2;
                const double angular = (plants[1].velocity_rad_s *
                                            experiment->actual_right_radius_m -
                                        plants[0].velocity_rad_s *
                                            experiment->actual_left_radius_m) /
                                       experiment->control.track_m;
                stationary =
                    stationary && fabs(linear) <= 0.01 && fabs(angular) <= 0.01;
            }
        }
        float current[2];
        for (size_t i = 0; i < 2; ++i) {
            current[i] = (float)(((int32_t)sensors[i].current_code -
                                  experiment->plant.current_zero_code) *
                                 experiment->plant.current_a_per_code);
        }
        float voltage[2];
        status = example_drive_current_step(
            &drive, current,
            sensors[0].valid && sensors[1].valid && !sensors[0].saturated &&
                !sensors[1].saturated,
            0.00005F, voltage);
        if (status != NX_SUCCESS) {
            return status;
        }
        for (size_t i = 0; i < 2; ++i) {
            status = example_drive_plant_stage(&plants[i], voltage[i]);
            if (status != NX_SUCCESS) {
                return status;
            }
        }
    }
    result.data_complete = result.samples == experiment->evaluation_samples;
    result.wheel_tracking_passed = result.data_complete;
    for (size_t i = 0; i < 2; ++i) {
        result.rms_error_rad_s[i] =
            result.samples == 0 ? 0 : sqrt(squared_error[i] / result.samples);
        result.wheel_tracking_passed = result.wheel_tracking_passed &&
                                       result.peak_error_rad_s[i] <= 0.2F &&
                                       result.rms_error_rad_s[i] <= 0.1;
    }
    result.stopped_window_passed =
        stationary && result.stationary_samples == 500;
    result.ground_endpoint_passed =
        experiment->distance_m > 0 &&
        fabs(result.truth.x_m - experiment->distance_m) <= 0.05 &&
        fabs(result.truth.y_m) <= 0.03 &&
        fabs(result.truth.heading_rad) <= 2 * acos(-1.0) / 180 &&
        result.stopped_window_passed;
    *report = result;
    return NX_SUCCESS;
}
