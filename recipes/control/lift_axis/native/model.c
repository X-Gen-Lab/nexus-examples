/**
 * \file            model.c
 * \brief           Independent current-lag and gravity-load software plant
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-24
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "native/model.h"
#include <math.h>

nx_result_t example_lift_model_initialize(example_lift_model_t* m,
                                          const example_lift_model_config_t* c,
                                          uint32_t domain, uint64_t epoch,
                                          nx_time_us_t now) {
    if (m == NULL || c == NULL || domain == 0U || epoch == 0U ||
        now == NX_DEADLINE_NEVER || !isfinite(c->torque_nm_per_a) ||
        c->torque_nm_per_a <= 0 || !isfinite(c->inertia_kg_m2) ||
        c->inertia_kg_m2 <= 0 || !isfinite(c->load_torque_nm) ||
        !isfinite(c->damping_nm_per_rad_s) || c->damping_nm_per_rad_s < 0 ||
        !isfinite(c->current_time_constant_s) ||
        c->current_time_constant_s <= 0 ||
        !isfinite(c->encoder_rad_per_count) || c->encoder_rad_per_count <= 0 ||
        !isfinite(c->support_tolerance_nm) || c->support_tolerance_nm <= 0 ||
        c->brake_transition_us == 0U || !isfinite(c->initial_position_rad) ||
        !isfinite(c->home_position_rad) || !isfinite(c->home_hysteresis_rad) ||
        c->home_hysteresis_rad <= 0 || !isfinite(c->upper_limit_rad) ||
        !isfinite(c->lower_limit_rad) ||
        c->lower_limit_rad >= c->home_position_rad ||
        c->home_position_rad + c->home_hysteresis_rad >= c->upper_limit_rad ||
        c->initial_position_rad <= c->lower_limit_rad ||
        c->initial_position_rad >= c->upper_limit_rad)
        return NX_ERROR_INVALID;
    float quantized =
        roundf(c->initial_position_rad / c->encoder_rad_per_count) *
        c->encoder_rad_per_count;
    if (!isfinite(quantized))
        return NX_ERROR_EXHAUSTED;
    *m = (example_lift_model_t){.config = *c,
                                .now = now,
                                .position_rad = c->initial_position_rad,
                                .last_encoder_rad = quantized,
                                .clock_domain = domain,
                                .epoch = epoch,
                                .brake = EXAMPLE_LIFT_BRAKE_ENGAGED,
                                .requested_brake = EXAMPLE_LIFT_BRAKE_ENGAGED,
                                .initialized = true,
                                .power_available = true};
    return NX_SUCCESS;
}
nx_result_t example_lift_model_advance(example_lift_model_t* m,
                                       const example_lift_output_t* request,
                                       nx_time_us_t now, float dt_s,
                                       example_lift_feedback_t* out) {
    if (m == NULL || request == NULL || out == NULL || !m->initialized ||
        now == NX_DEADLINE_NEVER || now <= m->now || !isfinite(dt_s) ||
        dt_s <= 0 || dt_s > 0.01F ||
        fabsf(dt_s - (float)(now - m->now) * 1e-6F) > 1.1e-6F ||
        !isfinite(request->axis.current_a) ||
        request->brake_request < EXAMPLE_LIFT_BRAKE_KEEP ||
        request->brake_request > EXAMPLE_LIFT_BRAKE_RELEASE)
        return NX_ERROR_INVALID;
    if (m->sequence == UINT32_MAX)
        return NX_ERROR_EXHAUSTED;
    example_lift_model_t next = *m;
    if (request->brake_request != EXAMPLE_LIFT_BRAKE_KEEP) {
        example_lift_brake_state_t target =
            request->brake_request == EXAMPLE_LIFT_BRAKE_CLOSE
                ? EXAMPLE_LIFT_BRAKE_ENGAGED
                : EXAMPLE_LIFT_BRAKE_RELEASED;
        if (target != next.requested_brake) {
            nx_result_t result = nx_deadline_finite_after(
                m->now, m->config.brake_transition_us, &next.brake_due);
            if (result != NX_SUCCESS)
                return result;
            next.requested_brake = target;
        }
    }
    if (next.brake_due != 0U && now >= next.brake_due) {
        next.brake = next.requested_brake;
        next.brake_due = 0U;
    }
    bool drive = request->motor_permit && next.power_available;
    float commanded = drive ? request->axis.current_a : 0;
    next.current_a += (commanded - next.current_a) * dt_s /
                      (next.config.current_time_constant_s + dt_s);
    if (!next.power_available)
        next.current_a = 0;
    if (next.brake == EXAMPLE_LIFT_BRAKE_ENGAGED) {
        next.velocity_rad_s = 0;
    } else {
        float acceleration =
            (next.current_a * next.config.torque_nm_per_a -
             next.config.load_torque_nm -
             next.config.damping_nm_per_rad_s * next.velocity_rad_s) /
            next.config.inertia_kg_m2;
        next.velocity_rad_s += acceleration * dt_s;
        next.position_rad += next.velocity_rad_s * dt_s;
    }
    float encoder =
        roundf(next.position_rad / next.config.encoder_rad_per_count) *
        next.config.encoder_rad_per_count;
    float velocity = (encoder - m->last_encoder_rad) / dt_s;
    if (!isfinite(next.current_a) || !isfinite(next.velocity_rad_s) ||
        !isfinite(next.position_rad) || !isfinite(encoder) ||
        !isfinite(velocity))
        return NX_ERROR_EXHAUSTED;
    if (next.position_rad <= next.config.home_position_rad)
        next.home_switch = true;
    else if (next.position_rad >=
             next.config.home_position_rad + next.config.home_hysteresis_rad)
        next.home_switch = false;
    next.last_encoder_rad = encoder;
    next.now = now;
    ++next.sequence;
    const example_lift_feedback_t feedback = {
        .motion = {.clock_domain = next.clock_domain,
                   .epoch = next.epoch,
                   .sequence = next.sequence,
                   .interval_start = m->now,
                   .sampled_at = now,
                   .position_rad = encoder,
                   .velocity_rad_s = velocity,
                   .valid = true},
        .brake_sampled_at = now,
        .support_sampled_at = now,
        .switches_sampled_at = now,
        .brake = next.brake,
        .brake_valid = true,
        .support_confirmed =
            drive && fabsf(next.current_a * next.config.torque_nm_per_a -
                           next.config.load_torque_nm) <=
                         next.config.support_tolerance_nm,
        .switches_valid = true,
        .home_switch = next.home_switch,
        .upper_limit = next.position_rad >= next.config.upper_limit_rad,
        .lower_limit = next.position_rad <= next.config.lower_limit_rad,
        .power_available = next.power_available,
        .drive_ready = drive};
    *m = next;
    *out = feedback;
    return NX_SUCCESS;
}
