/**
 * \file            plant.c
 * \brief           Independent averaged PMSM and sensor equations
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-24
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "plant.h"
#include <math.h>
#include <stddef.h>

#define PMSM_TWO_PI 6.28318530717958647693
#define PMSM_SQRT3  1.73205080756887729353

typedef struct {
    double id, iq, speed, angle;
} derivative_t;

static bool separate(const void* a, size_t an, const void* b, size_t bn) {
    const uintptr_t first = (uintptr_t)a, second = (uintptr_t)b;
    return an <= UINTPTR_MAX - first && bn <= UINTPTR_MAX - second &&
           (first + an <= second || second + bn <= first);
}

nx_result_t example_pmsm_initialize(example_pmsm_t* plant,
                                    const example_pmsm_config_t* config) {
    if (plant == NULL || config == NULL ||
        !separate(plant, sizeof(*plant), config, sizeof(*config)) ||
        !isfinite(config->resistance_ohm) || config->resistance_ohm <= 0 ||
        !isfinite(config->ld_h) || config->ld_h <= 0 ||
        !isfinite(config->lq_h) || config->lq_h <= 0 ||
        !isfinite(config->flux_wb) || config->flux_wb <= 0 ||
        !isfinite(config->inertia_kg_m2) || config->inertia_kg_m2 <= 0 ||
        !isfinite(config->viscous_nm_per_rad_s) ||
        config->viscous_nm_per_rad_s < 0 ||
        !isfinite(config->current_a_per_code) ||
        config->current_a_per_code <= 0 || config->pole_pairs == 0 ||
        config->pole_pairs > 65535 || config->encoder_counts_per_turn < 2 ||
        config->current_zero_code > 4095) {
        return NX_ERROR_INVALID;
    }
    const example_pmsm_t initialized = {.config = *config, .initialized = true};
    *plant = initialized;
    return NX_SUCCESS;
}

static derivative_t derivative(const example_pmsm_t* p, double alpha,
                               double beta, double load) {
    const example_pmsm_config_t* c = &p->config;
    const double angle = fmod(p->position_rad * c->pole_pairs, PMSM_TWO_PI);
    const double sine = sin(angle), cosine = cos(angle);
    const double vd = alpha * cosine + beta * sine;
    const double vq = beta * cosine - alpha * sine;
    const double we = p->velocity_rad_s * c->pole_pairs;
    const double torque =
        1.5 * c->pole_pairs *
        (c->flux_wb * p->iq_a + (c->ld_h - c->lq_h) * p->id_a * p->iq_a);
    return (derivative_t){
        (vd - c->resistance_ohm * p->id_a + we * c->lq_h * p->iq_a) / c->ld_h,
        (vq - c->resistance_ohm * p->iq_a -
         we * (c->ld_h * p->id_a + c->flux_wb)) /
            c->lq_h,
        (torque - load - c->viscous_nm_per_rad_s * p->velocity_rad_s) /
            c->inertia_kg_m2,
        p->velocity_rad_s};
}

static bool finite_state(const example_pmsm_t* p) {
    return isfinite(p->id_a) && isfinite(p->iq_a) &&
           isfinite(p->velocity_rad_s) && isfinite(p->position_rad) &&
           isfinite(p->dc_current_a);
}

nx_result_t example_pmsm_advance(example_pmsm_t* plant, const float duty[3],
                                 double bus_voltage_v, double dt_s,
                                 double load_nm) {
    if (plant == NULL || duty == NULL || !plant->initialized ||
        !separate(plant, sizeof(*plant), duty, 3 * sizeof(float)) ||
        !isfinite(bus_voltage_v) || bus_voltage_v <= 0 || !isfinite(dt_s) ||
        dt_s <= 0 || !isfinite(load_nm) ||
        dt_s > fmin(plant->config.ld_h, plant->config.lq_h) /
                   (8 * plant->config.resistance_ohm) ||
        plant->cycle == UINT64_MAX || !finite_state(plant)) {
        return NX_ERROR_INVALID;
    }
    for (size_t i = 0; i < 3; ++i) {
        if (!isfinite(duty[i]) || duty[i] < 0 || duty[i] > 1) {
            return NX_ERROR_INVALID;
        }
    }
    /* Leg common mode cancels in the isolated three-wire load. The model
     * consumes applied duties only; no dq request crosses this boundary. */
    const double alpha =
        bus_voltage_v * (2 * (double)duty[0] - duty[1] - duty[2]) / 3;
    const double beta =
        bus_voltage_v * ((double)duty[1] - duty[2]) / PMSM_SQRT3;
    const derivative_t first = derivative(plant, alpha, beta, load_nm);
    example_pmsm_t mid = *plant;
    mid.id_a += first.id * dt_s * 0.5;
    mid.iq_a += first.iq * dt_s * 0.5;
    mid.velocity_rad_s += first.speed * dt_s * 0.5;
    mid.position_rad += first.angle * dt_s * 0.5;
    const derivative_t center = derivative(&mid, alpha, beta, load_nm);
    example_pmsm_t next = *plant;
    next.id_a += center.id * dt_s;
    next.iq_a += center.iq * dt_s;
    next.velocity_rad_s += center.speed * dt_s;
    next.position_rad += center.angle * dt_s;
    const double angle =
        fmod(next.position_rad * next.config.pole_pairs, PMSM_TWO_PI);
    const double ia = next.id_a * cos(angle) - next.iq_a * sin(angle);
    const double ibeta = next.id_a * sin(angle) + next.iq_a * cos(angle);
    next.dc_current_a = 1.5 * (alpha * ia + beta * ibeta) / bus_voltage_v;
    ++next.cycle;
    if (!finite_state(&next)) {
        return NX_ERROR_EXHAUSTED;
    }
    *plant = next;
    return NX_SUCCESS;
}

nx_result_t example_pmsm_sense(const example_pmsm_t* plant,
                               example_pmsm_sensor_t* sensor) {
    if (plant == NULL || sensor == NULL || !plant->initialized ||
        !separate(plant, sizeof(*plant), sensor, sizeof(*sensor)) ||
        !finite_state(plant)) {
        return NX_ERROR_INVALID;
    }
    const double angle =
        fmod(plant->position_rad * plant->config.pole_pairs, PMSM_TWO_PI);
    const double alpha = plant->id_a * cos(angle) - plant->iq_a * sin(angle);
    const double beta = plant->id_a * sin(angle) + plant->iq_a * cos(angle);
    const double phase[3] = {alpha, -0.5 * alpha + 0.5 * PMSM_SQRT3 * beta,
                             -0.5 * alpha - 0.5 * PMSM_SQRT3 * beta};
    example_pmsm_sensor_t next = {0};
    for (size_t i = 0; i < 3; ++i) {
        const double code = plant->config.current_zero_code +
                            phase[i] / plant->config.current_a_per_code;
        if (!isfinite(code)) {
            return NX_ERROR_EXHAUSTED;
        }
        next.saturated = next.saturated || code < 0 || code > 4095;
        next.current_code[i] = (uint16_t)floor(fmin(fmax(code, 0), 4095) + 0.5);
    }
    const double counts = round(plant->position_rad / PMSM_TWO_PI *
                                plant->config.encoder_counts_per_turn);
    if (!isfinite(counts)) {
        return NX_ERROR_EXHAUSTED;
    }
    double modulo = fmod(counts, 65536);
    if (modulo < 0) {
        modulo += 65536;
    }
    next.encoder_count = (uint32_t)modulo;
    *sensor = next;
    return NX_SUCCESS;
}
