/**
 * \file            lift_axis.c
 * \brief           Qualified support handover and finite switch-reference
 * search \author          Nexus Team \version         1.0.0 \date 2026-11-24
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "lift_axis.h"
#include <math.h>

static bool disjoint(const void* first, size_t first_size, const void* second,
                     size_t second_size) {
    uintptr_t a = (uintptr_t)first;
    uintptr_t b = (uintptr_t)second;
    if (first == NULL || second == NULL || first_size == 0U ||
        second_size == 0U || first_size - 1U > UINTPTR_MAX - a ||
        second_size - 1U > UINTPTR_MAX - b)
        return false;
    return a > b ? a - b >= second_size : b - a >= first_size;
}
static bool fresh(nx_time_us_t at, nx_time_us_t now, uint32_t age) {
    return at != NX_DEADLINE_NEVER && now != NX_DEADLINE_NEVER && at <= now &&
           now - at <= age;
}
static bool observations(const example_lift_t* l,
                         const example_lift_feedback_t* f, nx_time_us_t now) {
    uint32_t age = l->config.maximum_feedback_age_us;
    return f != NULL && f->motion.valid && f->brake_valid &&
           f->switches_valid &&
           f->motion.clock_domain == l->axis->config.clock_domain &&
           f->motion.epoch == l->axis->config.epoch &&
           isfinite(f->motion.position_rad) &&
           isfinite(f->motion.velocity_rad_s) &&
           fresh(f->motion.sampled_at, now, age) &&
           fresh(f->brake_sampled_at, now, age) &&
           fresh(f->support_sampled_at, now, age) &&
           fresh(f->switches_sampled_at, now, age) &&
           f->brake >= EXAMPLE_LIFT_BRAKE_UNKNOWN &&
           f->brake <= EXAMPLE_LIFT_BRAKE_RELEASED &&
           !(f->upper_limit && f->lower_limit);
}
static bool policy(example_lift_brake_request_t action) {
    return action == EXAMPLE_LIFT_BRAKE_KEEP ||
           action == EXAMPLE_LIFT_BRAKE_CLOSE;
}
static nx_result_t command(example_lift_t* l, float velocity,
                           nx_time_us_t received, nx_time_us_t expiry,
                           nx_time_us_t now) {
    if (l->sequence == UINT32_MAX)
        return NX_ERROR_EXHAUSTED;
    nx_axis_command_t c = {.mode = NX_AXIS_VELOCITY,
                           .clock_domain = l->axis->config.clock_domain,
                           .epoch = l->axis->config.epoch,
                           .sequence = l->sequence + 1U,
                           .received_at = received,
                           .valid_until = expiry,
                           .position_rad = 0,
                           .velocity_rad_s = velocity,
                           .current_a = l->config.support_feedforward_a};
    nx_result_t result = nx_axis_accept(l->axis, &c, now);
    if (result == NX_SUCCESS) {
        ++l->sequence;
        l->commanded_velocity_rad_s = velocity;
    }
    return result;
}
static void publish(example_lift_t* l, const example_lift_feedback_t* f) {
    l->output.state = l->state;
    l->output.homing = l->homing;
    l->output.fault = l->fault;
    l->output.reference_valid = l->reference_valid;
    l->output.reference_offset_rad = l->reference_offset_rad;
    l->output.motor_permit =
        l->state != EXAMPLE_LIFT_SECURED && l->state != EXAMPLE_LIFT_FAULT;
    if (l->state != EXAMPLE_LIFT_FAULT) {
        l->output.support_unconfirmed =
            f->brake != EXAMPLE_LIFT_BRAKE_ENGAGED && !f->support_confirmed;
    }
}
static nx_result_t fail(example_lift_t* l, const example_lift_feedback_t* f,
                        nx_time_us_t now, nx_result_t reason, bool power) {
    if (l->fault == NX_SUCCESS)
        l->fault = reason;
    l->state = EXAMPLE_LIFT_FAULT;
    l->reference_valid = false;
    l->candidate_reference = false;
    l->output.brake_request =
        power ? l->config.on_power_loss : l->config.on_feedback_loss;
    (void)nx_axis_disable(l->axis);
    publish(l, f);
    l->output.axis.current_a = 0;
    l->output.support_unconfirmed =
        !(f->brake_valid && f->brake == EXAMPLE_LIFT_BRAKE_ENGAGED &&
          fresh(f->brake_sampled_at, now, l->config.maximum_feedback_age_us));
    return NX_ERROR_STATE;
}
nx_result_t example_lift_initialize(example_lift_t* l, nx_axis_t* axis,
                                    const example_lift_config_t* c) {
    if (l == NULL || axis == NULL || c == NULL ||
        !disjoint(l, sizeof(*l), axis, sizeof(*axis)) ||
        !disjoint(l, sizeof(*l), c, sizeof(*c)) || !axis->initialized ||
        axis->config.stop_policy != NX_AXIS_HOLD_ZERO_VELOCITY ||
        c->maximum_feedback_age_us == 0U || c->handover_timeout_us == 0U ||
        c->homing_timeout_us == 0U || c->homing_phase_timeout_us == 0U ||
        c->homing_phase_timeout_us > c->homing_timeout_us ||
        !isfinite(c->homing_maximum_phase_distance_rad) ||
        c->homing_maximum_phase_distance_rad <= 0 ||
        !isfinite(c->search_velocity_rad_s) || c->search_velocity_rad_s <= 0 ||
        !isfinite(c->approach_velocity_rad_s) ||
        c->approach_velocity_rad_s <= 0 ||
        c->approach_velocity_rad_s >= c->search_velocity_rad_s ||
        c->search_velocity_rad_s > axis->parameters.velocity_limit_rad_s ||
        !isfinite(c->home_coordinate_rad) ||
        !isfinite(c->support_feedforward_a) ||
        c->support_feedforward_a != axis->parameters.stop_feedforward_a ||
        !policy(c->on_feedback_loss) || !policy(c->on_power_loss))
        return NX_ERROR_INVALID;
    if (axis->state != NX_AXIS_DISABLED)
        return NX_ERROR_STATE;
    *l = (example_lift_t){.axis = axis,
                          .config = *c,
                          .state = EXAMPLE_LIFT_SECURED,
                          .sequence = axis->accepted_sequence,
                          .initialized = true};
    l->output.state = EXAMPLE_LIFT_SECURED;
    l->output.brake_request = EXAMPLE_LIFT_BRAKE_CLOSE;
    return NX_SUCCESS;
}
nx_result_t example_lift_start(example_lift_t* l,
                               const example_lift_feedback_t* f,
                               nx_time_us_t now) {
    if (l == NULL || !l->initialized || l->state != EXAMPLE_LIFT_SECURED ||
        l->fault != NX_SUCCESS || !observations(l, f, now) ||
        f->brake != EXAMPLE_LIFT_BRAKE_ENGAGED || !f->power_available)
        return NX_ERROR_STATE;
    nx_time_us_t deadline;
    nx_result_t result =
        nx_deadline_finite_after(now, l->config.handover_timeout_us, &deadline);
    if (result != NX_SUCCESS)
        return result;
    result = nx_axis_enable(l->axis, &f->motion, now);
    if (result != NX_SUCCESS)
        return result;
    result = command(l, 0, now, deadline, now);
    if (result != NX_SUCCESS) {
        (void)nx_axis_disable(l->axis);
        return result;
    }
    l->state = EXAMPLE_LIFT_BUILD_SUPPORT;
    l->deadline = deadline;
    l->last_step = now;
    l->output.brake_request = EXAMPLE_LIFT_BRAKE_CLOSE;
    return NX_SUCCESS;
}
nx_result_t example_lift_move(example_lift_t* l, float velocity,
                              nx_time_us_t received, nx_time_us_t expiry,
                              nx_time_us_t now) {
    if (l == NULL || !l->initialized || l->state != EXAMPLE_LIFT_ACTIVE ||
        l->homing != EXAMPLE_LIFT_HOME_IDLE || l->fault != NX_SUCCESS)
        return NX_ERROR_STATE;
    if (!isfinite(velocity))
        return NX_ERROR_INVALID;
    return command(l, velocity, received, expiry, now);
}
nx_result_t example_lift_request_stop(example_lift_t* l, nx_time_us_t now) {
    if (l == NULL || !l->initialized || now == NX_DEADLINE_NEVER ||
        now < l->last_step)
        return NX_ERROR_INVALID;
    if (l->state == EXAMPLE_LIFT_STOPPING || l->state == EXAMPLE_LIFT_ENGAGING)
        return NX_SUCCESS;
    if (l->state != EXAMPLE_LIFT_ACTIVE)
        return NX_ERROR_STATE;
    nx_time_us_t deadline;
    nx_result_t result =
        nx_deadline_finite_after(now, l->config.handover_timeout_us, &deadline);
    if (result != NX_SUCCESS)
        return result;
    result = nx_axis_request_stop(l->axis);
    if (result == NX_SUCCESS) {
        l->state = EXAMPLE_LIFT_STOPPING;
        l->deadline = deadline;
        l->commanded_velocity_rad_s = 0;
    }
    return result;
}
static nx_result_t home_phase(example_lift_t* l, example_lift_homing_t phase,
                              const example_lift_feedback_t* f,
                              nx_time_us_t now) {
    nx_time_us_t deadline;
    nx_result_t result = nx_deadline_finite_after(
        now, l->config.homing_phase_timeout_us, &deadline);
    if (result != NX_SUCCESS)
        return result;
    if (deadline > l->homing_deadline)
        deadline = l->homing_deadline;
    float velocity = phase == EXAMPLE_LIFT_HOME_BACKOFF
                         ? l->config.search_velocity_rad_s
                     : phase == EXAMPLE_LIFT_HOME_SEARCH
                         ? -l->config.search_velocity_rad_s
                         : -l->config.approach_velocity_rad_s;
    result = command(l, velocity, now, deadline, now);
    if (result == NX_SUCCESS) {
        l->homing = phase;
        l->deadline = deadline;
        l->phase_origin_rad = f->motion.position_rad;
    }
    return result;
}
nx_result_t example_lift_home(example_lift_t* l,
                              const example_lift_feedback_t* f,
                              nx_time_us_t now) {
    if (l == NULL || !l->initialized || l->state != EXAMPLE_LIFT_ACTIVE ||
        l->homing != EXAMPLE_LIFT_HOME_IDLE || l->fault != NX_SUCCESS ||
        !observations(l, f, now) || !f->drive_ready || !f->power_available)
        return NX_ERROR_STATE;
    nx_time_us_t deadline;
    nx_result_t result =
        nx_deadline_finite_after(now, l->config.homing_timeout_us, &deadline);
    if (result != NX_SUCCESS)
        return result;
    l->homing_deadline = deadline;
    result = home_phase(l,
                        f->home_switch ? EXAMPLE_LIFT_HOME_BACKOFF
                                       : EXAMPLE_LIFT_HOME_SEARCH,
                        f, now);
    if (result == NX_SUCCESS) {
        l->reference_valid = false;
        l->candidate_reference = false;
    }
    return result;
}
static nx_result_t homing_step(example_lift_t* l,
                               const example_lift_feedback_t* f,
                               nx_time_us_t now) {
    if (l->homing == EXAMPLE_LIFT_HOME_IDLE ||
        l->homing == EXAMPLE_LIFT_HOME_SETTLING)
        return NX_SUCCESS;
    if (now >= l->deadline || now >= l->homing_deadline ||
        fabsf(f->motion.position_rad - l->phase_origin_rad) >
            l->config.homing_maximum_phase_distance_rad)
        return NX_ERROR_TIMEOUT;
    if (l->homing == EXAMPLE_LIFT_HOME_SEARCH && f->home_switch)
        return home_phase(l, EXAMPLE_LIFT_HOME_BACKOFF, f, now);
    if (l->homing == EXAMPLE_LIFT_HOME_BACKOFF && !f->home_switch)
        return home_phase(l, EXAMPLE_LIFT_HOME_APPROACH, f, now);
    if (l->homing == EXAMPLE_LIFT_HOME_APPROACH && f->home_switch) {
        l->candidate_offset_rad =
            l->config.home_coordinate_rad - f->motion.position_rad;
        nx_result_t result = example_lift_request_stop(l, now);
        if (result == NX_SUCCESS) {
            l->candidate_reference = true;
            l->homing = EXAMPLE_LIFT_HOME_SETTLING;
        }
        return result;
    }
    return NX_SUCCESS;
}
nx_result_t example_lift_step(example_lift_t* l,
                              const example_lift_feedback_t* f,
                              nx_time_us_t now, float dt_s,
                              example_lift_output_t* out) {
    if (l == NULL || f == NULL || out == NULL || !l->initialized ||
        !isfinite(dt_s) || dt_s <= 0 || now == NX_DEADLINE_NEVER ||
        !disjoint(out, sizeof(*out), l, sizeof(*l)) ||
        !disjoint(out, sizeof(*out), l->axis, sizeof(*l->axis)) ||
        !disjoint(out, sizeof(*out), f, sizeof(*f)) ||
        !disjoint(l, sizeof(*l), f, sizeof(*f)) ||
        !disjoint(l->axis, sizeof(*l->axis), f, sizeof(*f)))
        return NX_ERROR_INVALID;
    if (l->state == EXAMPLE_LIFT_FAULT) {
        publish(l, f);
        l->output.support_unconfirmed = !(
            f->brake_valid && f->brake == EXAMPLE_LIFT_BRAKE_ENGAGED &&
            fresh(f->brake_sampled_at, now, l->config.maximum_feedback_age_us));
        *out = l->output;
        return NX_ERROR_STATE;
    }
    if (!observations(l, f, now) || now <= l->last_step ||
        l->axis->parameters.stop_feedforward_a !=
            l->config.support_feedforward_a) {
        nx_result_t result = fail(l, f, now, NX_ERROR_STATE, false);
        *out = l->output;
        return result;
    }
    l->last_step = now;
    if (l->state == EXAMPLE_LIFT_SECURED) {
        if (f->brake != EXAMPLE_LIFT_BRAKE_ENGAGED) {
            nx_result_t result = fail(l, f, now, NX_ERROR_STATE, false);
            *out = l->output;
            return result;
        }
        publish(l, f);
        *out = l->output;
        return NX_SUCCESS;
    }
    if (!f->power_available ||
        (l->state != EXAMPLE_LIFT_BUILD_SUPPORT && !f->drive_ready)) {
        nx_result_t result = fail(l, f, now, NX_ERROR_STATE, true);
        *out = l->output;
        return result;
    }
    if ((l->state != EXAMPLE_LIFT_ACTIVE && now >= l->deadline) ||
        (l->state == EXAMPLE_LIFT_ACTIVE &&
         ((l->commanded_velocity_rad_s > 0 && f->upper_limit) ||
          (l->commanded_velocity_rad_s < 0 && f->lower_limit)))) {
        nx_result_t result = fail(l, f, now, NX_ERROR_TIMEOUT, false);
        *out = l->output;
        return result;
    }
    nx_result_t result = homing_step(l, f, now);
    if (result != NX_SUCCESS) {
        result = fail(l, f, now, result, false);
        *out = l->output;
        return result;
    }
    const nx_axis_limits_t limits = {-l->axis->parameters.current_limit_a,
                                     l->axis->parameters.current_limit_a};
    nx_axis_output_t axis_output;
    result =
        nx_axis_step(l->axis, &f->motion, &limits, now, dt_s, &axis_output);
    if (result != NX_SUCCESS) {
        result = fail(l, f, now, result, false);
        *out = l->output;
        return result;
    }
    l->output.axis = axis_output;
    switch (l->state) {
        case EXAMPLE_LIFT_BUILD_SUPPORT:
            if (f->brake != EXAMPLE_LIFT_BRAKE_ENGAGED) {
                result = fail(l, f, now, NX_ERROR_STATE, false);
                break;
            }
            if (f->support_confirmed && f->drive_ready) {
                l->state = EXAMPLE_LIFT_RELEASING;
                l->output.brake_request = EXAMPLE_LIFT_BRAKE_RELEASE;
            }
            break;
        case EXAMPLE_LIFT_RELEASING:
            if (!f->support_confirmed) {
                result = fail(l, f, now, NX_ERROR_STATE, false);
                break;
            }
            if (f->brake == EXAMPLE_LIFT_BRAKE_RELEASED) {
                l->state = EXAMPLE_LIFT_ACTIVE;
                /* Support building was a finite internal command. Adopt a
                 * local zero-speed stop session without renewing any network
                 * motion authority; the next explicit move supplies its own. */
            }
            break;
        case EXAMPLE_LIFT_ACTIVE:
            if (f->brake != EXAMPLE_LIFT_BRAKE_RELEASED) {
                result = fail(l, f, now, NX_ERROR_STATE, false);
                break;
            }
            if (axis_output.state == NX_AXIS_STOPPING ||
                axis_output.state == NX_AXIS_HOLDING) {
                if (axis_output.command_expired && l->fault == NX_SUCCESS)
                    l->fault = NX_ERROR_TIMEOUT;
                result = example_lift_request_stop(l, now);
            }
            break;
        case EXAMPLE_LIFT_STOPPING:
            if (f->brake != EXAMPLE_LIFT_BRAKE_RELEASED) {
                result = fail(l, f, now, NX_ERROR_STATE, false);
                break;
            }
            if (axis_output.state == NX_AXIS_HOLDING && f->support_confirmed) {
                l->state = EXAMPLE_LIFT_ENGAGING;
                l->output.brake_request = EXAMPLE_LIFT_BRAKE_CLOSE;
            }
            break;
        case EXAMPLE_LIFT_ENGAGING:
            if (f->brake == EXAMPLE_LIFT_BRAKE_ENGAGED) {
                (void)nx_axis_disable(l->axis);
                l->output.axis.current_a = 0;
                l->state = EXAMPLE_LIFT_SECURED;
                l->homing = EXAMPLE_LIFT_HOME_IDLE;
                if (l->candidate_reference) {
                    l->reference_offset_rad = l->candidate_offset_rad;
                    l->reference_valid = true;
                    l->candidate_reference = false;
                }
            } else if (!f->support_confirmed) {
                result = fail(l, f, now, NX_ERROR_STATE, false);
            }
            break;
        default:
            result = fail(l, f, now, NX_ERROR_STATE, false);
            break;
    }
    publish(l, f);
    if (l->state == EXAMPLE_LIFT_FAULT)
        result = NX_ERROR_STATE;
    *out = l->output;
    return result;
}
nx_result_t example_lift_reset(example_lift_t* l,
                               const example_lift_feedback_t* f,
                               nx_time_us_t now) {
    if (l == NULL || !l->initialized || !observations(l, f, now) ||
        (l->state != EXAMPLE_LIFT_FAULT && l->state != EXAMPLE_LIFT_SECURED) ||
        f->brake != EXAMPLE_LIFT_BRAKE_ENGAGED || f->drive_ready ||
        fabsf(f->motion.velocity_rad_s) >
            l->axis->parameters.velocity_tolerance_rad_s)
        return NX_ERROR_STATE;
    (void)nx_axis_disable(l->axis);
    nx_result_t result = nx_axis_clear_fault(l->axis);
    if (result != NX_SUCCESS)
        return result;
    l->state = EXAMPLE_LIFT_SECURED;
    l->homing = EXAMPLE_LIFT_HOME_IDLE;
    l->fault = NX_SUCCESS;
    l->candidate_reference = false;
    l->commanded_velocity_rad_s = 0;
    l->last_step = now;
    l->output =
        (example_lift_output_t){.state = EXAMPLE_LIFT_SECURED,
                                .brake_request = EXAMPLE_LIFT_BRAKE_CLOSE,
                                .reference_offset_rad = l->reference_offset_rad,
                                .reference_valid = l->reference_valid};
    return NX_SUCCESS;
}
