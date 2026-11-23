/**
 * \file            imu_reference.c
 * \brief           Product-owned sampling interval and calibration composition
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-22
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "imu_reference.h"
#include <math.h>

nx_result_t example_imu_init(example_imu_reference_t* reference,
                             nx_lsm6dsox_t* sensor,
                             const example_imu_config_t* config) {
    if (reference == NULL || sensor == NULL || config == NULL ||
        config->calibration_version == 0) {
        return NX_ERROR_INVALID;
    }
    if (reference->entered) {
        return NX_ERROR_BUSY;
    }
    example_imu_reference_t next = {.sensor = sensor, .config = *config};
    nx_vec3f_t scratch;
    const nx_vec3f_t zero = {0, 0, 0};
    if (nx_vec3_calibrate(&config->accelerometer, zero, &scratch) !=
            NX_SUCCESS ||
        nx_vec3_calibrate(&config->gyroscope, zero, &scratch) != NX_SUCCESS ||
        nx_quatf_normalize(config->sensor_to_body,
                           &next.config.sensor_to_body) != NX_SUCCESS ||
        nx_attitude6_init(&next.fusion, config->initial_body_to_world) !=
            NX_SUCCESS ||
        !isfinite(config->fusion.gain_per_s) || config->fusion.gain_per_s < 0 ||
        !isfinite(config->fusion.acceleration_min) ||
        config->fusion.acceleration_min <= 0 ||
        !isfinite(config->fusion.acceleration_max) ||
        config->fusion.acceleration_max < config->fusion.acceleration_min ||
        !isfinite(config->fusion.maximum_step_rad) ||
        config->fusion.maximum_step_rad <= 0 ||
        config->fusion.maximum_step_rad > 1) {
        return NX_ERROR_INVALID;
    }
    *reference = next;
    return NX_SUCCESS;
}

nx_result_t example_imu_step(example_imu_reference_t* reference, float dt_s,
                             nx_time_us_t deadline,
                             example_imu_observation_t* out) {
    if (reference == NULL || out == NULL || reference->sensor == NULL ||
        !isfinite(dt_s) || dt_s <= 0 || deadline == NX_DEADLINE_NEVER) {
        return NX_ERROR_INVALID;
    }
    if (reference->entered) {
        return NX_ERROR_BUSY;
    }
    if (reference->faulted) {
        return NX_ERROR_STATE;
    }
    reference->entered = true;
    example_imu_observation_t next = {
        .calibration_version = reference->config.calibration_version};
    nx_result_t result =
        nx_lsm6dsox_read(reference->sensor, &next.sample, deadline);
    if (result != NX_SUCCESS) {
        reference->entered = false;
        return result;
    }
    if (reference->have_sample &&
        (next.sample.generation != reference->generation ||
         reference->sequence == UINT32_MAX ||
         next.sample.sequence != reference->sequence + 1 ||
         next.sample.observed.domain != reference->clock_domain ||
         next.sample.observed.epoch != reference->clock_epoch)) {
        reference->faulted = true;
        reference->entered = false;
        return NX_ERROR_STATE;
    }
    const nx_vec3f_t raw_acceleration = {next.sample.acceleration_m_s2[0],
                                         next.sample.acceleration_m_s2[1],
                                         next.sample.acceleration_m_s2[2]};
    const nx_vec3f_t raw_angular_rate = {next.sample.angular_rate_rad_s[0],
                                         next.sample.angular_rate_rad_s[1],
                                         next.sample.angular_rate_rad_s[2]};
    nx_vec3f_t acceleration;
    nx_vec3f_t angular_rate;
    result = nx_vec3_calibrate(&reference->config.accelerometer,
                               raw_acceleration, &acceleration);
    if (result == NX_SUCCESS) {
        result = nx_vec3_calibrate(&reference->config.gyroscope,
                                   raw_angular_rate, &angular_rate);
    }
    if (result == NX_SUCCESS) {
        result = nx_quatf_rotate(reference->config.sensor_to_body, acceleration,
                                 &next.acceleration_body);
    }
    if (result == NX_SUCCESS) {
        result = nx_quatf_rotate(reference->config.sensor_to_body, angular_rate,
                                 &next.angular_rate_body);
    }
    if (result == NX_SUCCESS) {
        result =
            nx_attitude6_step(&reference->fusion, &reference->config.fusion,
                              next.angular_rate_body, next.acceleration_body,
                              dt_s, &next.attitude);
    }
    if (result == NX_SUCCESS) {
        reference->generation = next.sample.generation;
        reference->sequence = next.sample.sequence;
        reference->clock_domain = next.sample.observed.domain;
        reference->clock_epoch = next.sample.observed.epoch;
        reference->have_sample = true;
        *out = next;
    } else {
        reference->faulted = true;
    }
    reference->entered = false;
    return result;
}
