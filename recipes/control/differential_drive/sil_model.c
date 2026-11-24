/**
 * \file            sil_model.c
 * \brief           Explicit DC electrical, mechanical and sensor model
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-24
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "sil_model.h"
#include <math.h>
#include <stddef.h>

static bool positive(double value) {
    return isfinite(value) && value > 0;
}

static bool configured(const example_drive_plant_config_t* config) {
    return config != NULL && positive(config->resistance_ohm) &&
           positive(config->inductance_h) &&
           positive(config->back_emf_v_per_rad_s) &&
           positive(config->torque_nm_per_a) &&
           positive(config->inertia_kg_m2) &&
           isfinite(config->viscous_nm_per_rad_s) &&
           config->viscous_nm_per_rad_s >= 0 &&
           positive(config->bus_voltage_v) && config->counts_per_turn != 0 &&
           positive(config->current_a_per_code);
}

nx_result_t
example_drive_plant_initialize(example_drive_plant_t* plant,
                               const example_drive_plant_config_t* config) {
    if (plant == NULL || !configured(config)) {
        return NX_ERROR_INVALID;
    }
    const example_drive_plant_t next = {.config = *config};
    *plant = next;
    return NX_SUCCESS;
}

nx_result_t example_drive_plant_stage(example_drive_plant_t* plant,
                                      double voltage_v) {
    if (plant == NULL || !configured(&plant->config) || !isfinite(voltage_v)) {
        return NX_ERROR_INVALID;
    }
    plant->staged_voltage_v =
        fmax(-plant->config.bus_voltage_v,
             fmin(plant->config.bus_voltage_v, voltage_v));
    return NX_SUCCESS;
}

nx_result_t example_drive_plant_advance(example_drive_plant_t* plant,
                                        double dt_s, double load_nm) {
    if (plant == NULL || !configured(&plant->config) || !positive(dt_s) ||
        dt_s >
            plant->config.inductance_h / (4 * plant->config.resistance_ohm) ||
        !isfinite(load_nm)) {
        return NX_ERROR_INVALID;
    }
    if (plant->cycle == UINT64_MAX) {
        return NX_ERROR_EXHAUSTED;
    }
    example_drive_plant_t next = *plant;
    const example_drive_plant_config_t* config = &plant->config;
    next.current_a +=
        dt_s *
        (plant->active_voltage_v - config->resistance_ohm * plant->current_a -
         config->back_emf_v_per_rad_s * plant->velocity_rad_s) /
        config->inductance_h;
    next.velocity_rad_s +=
        dt_s *
        (config->torque_nm_per_a * plant->current_a - load_nm -
         config->viscous_nm_per_rad_s * plant->velocity_rad_s) /
        config->inertia_kg_m2;
    next.position_rad += dt_s * plant->velocity_rad_s;
    if (!isfinite(next.current_a) || !isfinite(next.velocity_rad_s) ||
        !isfinite(next.position_rad)) {
        return NX_ERROR_EXHAUSTED;
    }
    next.active_voltage_v = plant->staged_voltage_v;
    next.load_nm = load_nm;
    ++next.cycle;
    *plant = next;
    return NX_SUCCESS;
}

nx_result_t example_drive_plant_sense(const example_drive_plant_t* plant,
                                      bool valid,
                                      example_drive_sensor_t* sample) {
    if (plant == NULL || sample == NULL || !configured(&plant->config) ||
        !isfinite(plant->position_rad) || !isfinite(plant->current_a)) {
        return NX_ERROR_INVALID;
    }
    const double counts = round(
        plant->position_rad * plant->config.counts_per_turn / (2 * acos(-1.0)));
    const double adc =
        round(plant->current_a / plant->config.current_a_per_code) +
        plant->config.current_zero_code;
    if (!isfinite(counts) || !isfinite(adc)) {
        return NX_ERROR_EXHAUSTED;
    }
    double modulo = fmod(counts, 4294967296.0);
    if (modulo < 0) {
        modulo += 4294967296.0;
    }
    const example_drive_sensor_t next = {
        .encoder_count = (uint32_t)modulo,
        .current_code = (uint16_t)fmax(0, fmin(UINT16_MAX, adc)),
        .sequence = plant->cycle,
        .valid = valid,
        .saturated = adc < 0 || adc > UINT16_MAX};
    *sample = next;
    return NX_SUCCESS;
}

nx_result_t example_drive_pose_advance(example_drive_pose_t* pose,
                                       double left_rad_s, double right_rad_s,
                                       double left_radius_m,
                                       double right_radius_m, double track_m,
                                       double dt_s) {
    if (pose == NULL || !isfinite(pose->x_m) || !isfinite(pose->y_m) ||
        !isfinite(pose->heading_rad) || !isfinite(left_rad_s) ||
        !isfinite(right_rad_s) || !positive(left_radius_m) ||
        !positive(right_radius_m) || !positive(track_m) || !positive(dt_s)) {
        return NX_ERROR_INVALID;
    }
    const double velocity =
        (left_rad_s * left_radius_m + right_rad_s * right_radius_m) / 2;
    const double omega =
        (right_rad_s * right_radius_m - left_rad_s * left_radius_m) / track_m;
    const double rotation = omega * dt_s;
    const double half = rotation / 2;
    const double sinc =
        fabs(half) < 1e-8 ? 1 - half * half / 6 : sin(half) / half;
    const double distance = velocity * dt_s * sinc;
    example_drive_pose_t next = *pose;
    next.x_m += distance * cos(pose->heading_rad + half);
    next.y_m += distance * sin(pose->heading_rad + half);
    next.heading_rad += rotation;
    if (!isfinite(next.x_m) || !isfinite(next.y_m) ||
        !isfinite(next.heading_rad)) {
        return NX_ERROR_EXHAUSTED;
    }
    *pose = next;
    return NX_SUCCESS;
}
