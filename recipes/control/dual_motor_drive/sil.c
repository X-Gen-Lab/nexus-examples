/**
 * \file            sil.c
 * \brief           Sensor-only dual PMSM control experiment
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-24
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "sil.h"
#include <math.h>
#include <stddef.h>

#define FAST_US      50U
#define TICKS_PER_US 20U

typedef struct {
    nx_power_stage_state_t facts;
    nx_power_stage_session_t* active;
    nx_power_stage_plan_t applied;
    nx_result_t fault;
    uint64_t cycles;
    bool inhibited;
} model_stage_t;

static nx_result_t model_start(void* argument,
                               nx_power_stage_session_t* session) {
    model_stage_t* stage = argument;
    if (stage->active != NULL || stage->inhibited) {
        return NX_ERROR_STATE;
    }
    const nx_result_t result =
        nx_request_admit(&session->base, NX_REQUEST_ACTIVE);
    if (result == NX_SUCCESS) {
        stage->active = session;
        stage->applied = session->initial;
        stage->facts.running = true;
    }
    return result;
}

static nx_result_t model_service(void* argument) {
    const model_stage_t* stage = argument;
    return stage->fault == NX_SUCCESS ? NX_ERROR_BUSY : stage->fault;
}

static nx_result_t model_stop(void* argument) {
    model_stage_t* stage = argument;
    nx_power_stage_session_t* ended = stage->active;
    if (ended == NULL) {
        return NX_ERROR_STATE;
    }
    stage->active = NULL;
    stage->facts.running = false;
    stage->inhibited = true;
    nx_request_settle(&ended->base, stage->fault, 0);
    return NX_SUCCESS;
}

static nx_result_t model_state(const void* argument,
                               nx_power_stage_state_t* out) {
    const model_stage_t* stage = argument;
    *out = stage->facts;
    out->fault = stage->fault;
    out->completed_cycles = stage->cycles;
    return NX_SUCCESS;
}

static const nx_power_stage_ops_t model_ops = {model_start, model_service,
                                               model_stop, model_state};

example_dual_motor_experiment_t example_dual_motor_reference(void) {
    example_dual_motor_experiment_t result = {0};
    result.bus_voltage_v = 24;
    result.duration_us = 1000000;
    result.evaluation_start_us = 500000;
    result.stop_axis_mask = 3;
    result.load_nm[0] = 0.005F;
    result.load_nm[1] = -0.003F;
    for (size_t i = 0; i < 2; ++i) {
        result.control[i] = example_motor_drive_reference();
        example_motor_drive_config_t* c = &result.control[i];
        c->commissioned = true;
        c->motor.nominal_period_us = FAST_US;
        c->motor.maximum_gap_us = FAST_US + 10;
        c->motor.maximum_angle_age_us = 5;
        c->motor.maximum_prediction_us = FAST_US + 10;
        c->motor.kp_d = 2;
        c->motor.kp_q = 2;
        c->motor.ki_d = 400;
        c->motor.ki_q = 400;
        c->motor.current_sum_tolerance_a = 0.03F;
        c->axis_parameters.velocity_kp = 0.15F;
        c->axis_parameters.velocity_ki = 1;
        c->axis_parameters.integral_limit_a = 1;
        c->axis.maximum_step_us = 3000;
        c->axis.maximum_feedback_age_us = 3000;
        c->initial_mode = NX_AXIS_VELOCITY;
        c->initial_velocity_rad_s = i == 0 ? 20 : -10;
        c->encoder_counts_per_turn = 16384;
        c->maximum_scan_span_us = 1;
        c->feedback_division = 20;
        result.power_bound_w_per_a[i] = 20;
        for (size_t phase = 0; phase < 3; ++phase) {
            c->current_a_per_code[phase] = 0.005F;
        }
        result.plant[i] = (example_pmsm_config_t){i == 0 ? 0.8 : 1.1,
                                                  0.0004,
                                                  0.0004,
                                                  0.015,
                                                  i == 0 ? 0.0002 : 0.0003,
                                                  0.0001,
                                                  0.005,
                                                  7,
                                                  16384,
                                                  2048};
    }
    return result;
}

static bool valid_experiment(const example_dual_motor_experiment_t* e) {
    return e != NULL && e->duration_us >= 20000 && e->duration_us <= 5000000 &&
           e->duration_us % FAST_US == 0 &&
           e->evaluation_start_us < e->duration_us &&
           e->evaluation_start_us % FAST_US == 0 &&
           isfinite(e->bus_voltage_v) && e->bus_voltage_v > 0 &&
           isfinite(e->load_nm[0]) && isfinite(e->load_nm[1]) &&
           e->task_pause_at_us <= e->duration_us &&
           e->task_pause_for_us <= e->duration_us &&
           e->sensor_fault_at_us <= e->duration_us &&
           e->controlled_stop_at_us <= e->duration_us &&
           e->stop_axis_mask <= 3 && e->bus_limits_at_us <= e->duration_us &&
           e->bus_source_stop_at_us <= e->duration_us &&
           (e->bus_limits_at_us == 0 ||
            (e->bus_limits_at_us >= 20000 && e->control[0].id_a == 0 &&
             e->control[1].id_a == 0 && isfinite(e->motoring_budget_w) &&
             e->motoring_budget_w >= 0 && isfinite(e->regeneration_budget_w) &&
             e->regeneration_budget_w >= 0 &&
             isfinite(e->power_bound_w_per_a[0]) &&
             e->power_bound_w_per_a[0] > 0 &&
             isfinite(e->power_bound_w_per_a[1]) &&
             e->power_bound_w_per_a[1] > 0));
}

/* Reserve both signs independently. The supplied W/A bounds must cover the
 * selected model operating range, losses and uncertainty. They are not a
 * conversion of phase amperes into measured DC amperes. No opposing-axis
 * cancellation credit is taken. This Task policy has finite adoption latency
 * and is not a replacement for bus hardware protection. */
static nx_result_t
budget_limits(const example_dual_motor_experiment_t* experiment,
              example_motor_drive_t drives[2], nx_time_us_t now,
              uint32_t revision, example_dual_motor_report_t* report) {
    const nx_drive_bus_budget_t budget = {
        .sampled_at = now,
        .valid_until = now + 5000,
        .motoring_power_w = experiment->motoring_budget_w,
        .regeneration_power_w = experiment->regeneration_budget_w};
    float current[2], demand[2], motoring[2], regeneration[2];
    for (size_t i = 0; i < 2; ++i) {
        current[i] =
            sqrtf(fmaxf(0, drives[i].config.motor.current_limit_a *
                                   drives[i].config.motor.current_limit_a -
                               drives[i].config.id_a * drives[i].config.id_a));
        demand[i] = current[i] * experiment->power_bound_w_per_a[i];
    }
    nx_result_t result = nx_drive_bus_limit(&budget, demand, 2, now, motoring);
    for (size_t i = 0; i < 2; ++i) {
        demand[i] = -demand[i];
    }
    if (result == NX_SUCCESS) {
        result = nx_drive_bus_limit(&budget, demand, 2, now, regeneration);
    }
    if (result != NX_SUCCESS) {
        return result;
    }
    float reserved = 0;
    for (size_t i = 0; i < 2; ++i) {
        const float bound = current[i] * fminf(motoring[i], regeneration[i]);
        const example_motor_drive_limits_t limits = {
            .current = {-bound, bound},
            .sampled_at = budget.sampled_at,
            .valid_until = budget.valid_until,
            .epoch = drives[i].references.epoch,
            .clock_domain = drives[i].references.clock_domain,
            .revision = revision};
        if (drives[i].borrowed) {
            result = example_motor_drive_set_limits(&drives[i], &limits, now);
            if (result != NX_SUCCESS) {
                return result;
            }
        }
        reserved += bound * experiment->power_bound_w_per_a[i];
    }
    report->admitted_power_bound_w =
        fmaxf(report->admitted_power_bound_w, reserved);
    ++report->budget_updates;
    return NX_SUCCESS;
}

static nx_result_t withdraw(example_motor_drive_t drives[2]) {
    nx_result_t failure = NX_SUCCESS;
    for (size_t i = 0; i < 2; ++i) {
        if (drives[i].borrowed) {
            const nx_result_t result = example_motor_drive_stop(&drives[i]);
            if (drives[i].borrowed) {
                failure = result == NX_SUCCESS ? NX_ERROR_STATE : result;
            }
        }
    }
    return failure;
}

static nx_result_t model_cycle(model_stage_t* stage, example_pmsm_t* plant,
                               const example_motor_drive_config_t* config,
                               nx_time_us_t beginning, float bus, float load,
                               bool sensor_fault) {
    const bool live = stage->active != NULL && !stage->inhibited;
    float duty[3];
    for (size_t phase = 0; phase < 3; ++phase) {
        duty[phase] =
            live ? (float)stage->applied.duty_ticks[phase] / 1000 : 0.5F;
    }
    const uint32_t trigger = live ? stage->applied.trigger_ticks : 800U;
    if (trigger == 0 || trigger >= 1000) {
        return NX_ERROR_STATE;
    }
    const double before_s = (double)trigger / 20000000;
    nx_result_t result = example_pmsm_advance(plant, duty, bus, before_s, load);
    if (result != NX_SUCCESS) {
        return result;
    }
    nx_power_stage_plan_t next = stage->applied;
    if (live) {
        example_pmsm_sensor_t sensed;
        result = example_pmsm_sense(plant, &sensed);
        if (result != NX_SUCCESS) {
            return result;
        }
        const nx_time_us_t sample_at = beginning + trigger / TICKS_PER_US;
        nx_power_stage_sample_t sample = {0};
        sample.current_phase_mask = stage->applied.current_phase_mask;
        for (size_t phase = 0; phase < 3; ++phase) {
            if ((sample.current_phase_mask & (1U << phase)) != 0U) {
                sample.current_raw[phase] =
                    sensor_fault ? 4095 : sensed.current_code[phase];
                ++sample.current_count;
            }
        }
        sample.bus_raw = (uint16_t)floorf(bus / config->bus_v_per_code + 0.5F);
        sample.bus_valid = true;
        sample.encoder_valid = true;
        sample.encoder_count = sensed.encoder_count;
        sample.plan_sequence = stage->applied.sequence;
        sample.sequence = stage->cycles;
        sample.clock_epoch = 9;
        sample.sample_at = sample_at;
        sample.observed_at = sample_at + 1;
        sample.angle_at = sample_at;
        sample.planned_apply_at = beginning + FAST_US;
        sample.scan_span_us = 1;
        sample.trigger_ticks = trigger;
        result = stage->active->compute(stage->active->context, &sample, &next);
        if (result == NX_SUCCESS &&
            (next.sequence != sample.sequence + 1 || next.trigger_ticks == 0 ||
             next.trigger_ticks >= 1000 ||
             (next.current_phase_mask != 3 && next.current_phase_mask != 5 &&
              next.current_phase_mask != 6 && next.current_phase_mask != 7))) {
            result = NX_ERROR_STATE;
        }
        if (result != NX_SUCCESS) {
            stage->fault = result;
            stage->facts.fault = result;
            stage->facts.running = false;
            stage->inhibited = true;
        } else {
            ++stage->cycles;
        }
    }
    /* The previously active voltage remains for the rest of this carrier;
     * only its boundary adopts the next complete plan. Output-inhibit diode
     * behavior is outside this averaged model, so no coast/brake claim follows.
     */
    const double after_s = (double)(1000 - trigger) / 20000000;
    result = example_pmsm_advance(plant, duty, bus, after_s, load);
    if (result == NX_SUCCESS && !stage->inhibited) {
        stage->applied = next;
    }
    return result;
}

nx_result_t
example_dual_motor_run(const example_dual_motor_experiment_t* experiment,
                       example_dual_motor_report_t* report) {
    if (report == NULL || !valid_experiment(experiment)) {
        return NX_ERROR_INVALID;
    }
    const uintptr_t input = (uintptr_t)experiment, output = (uintptr_t)report;
    if (sizeof(*experiment) > UINTPTR_MAX - input ||
        sizeof(*report) > UINTPTR_MAX - output ||
        (input < output + sizeof(*report) &&
         output < input + sizeof(*experiment))) {
        return NX_ERROR_INVALID;
    }
    example_motor_drive_t drives[2] = {0};
    example_pmsm_t plants[2];
    model_stage_t stages[2] = {0};
    nx_power_stage_t ports[2];
    example_motor_drive_config_t configs[2];
    nx_result_t status = NX_SUCCESS;
    for (size_t i = 0; i < 2; ++i) {
        configs[i] = experiment->control[i];
        configs[i].run_duration_us = experiment->duration_us + 20000;
        stages[i].facts = (nx_power_stage_state_t){.clock_epoch = 9,
                                                   .tick_hz = 20000000,
                                                   .period_ticks = 1000,
                                                   .trigger_ticks = 800,
                                                   .minimum_duty_ticks = 50,
                                                   .maximum_duty_ticks = 700,
                                                   .scan_span_us = 1,
                                                   .deadtime_ticks = 10,
                                                   .settling_ticks = 10,
                                                   .aperture_ticks = 20,
                                                   .conversion_ticks = 20,
                                                   .irq_ticks = 20,
                                                   .compute_ticks = 40,
                                                   .commit_before_ticks = 980,
                                                   .current_count = 3,
                                                   .current_phase_mask = 7,
                                                   .bus_available = true,
                                                   .encoder_available = true};
        ports[i] = (nx_power_stage_t){&model_ops, &stages[i]};
        status = example_pmsm_initialize(&plants[i], &experiment->plant[i]);
        if (status == NX_SUCCESS) {
            status = example_motor_drive_initialize(&drives[i], &ports[i],
                                                    &configs[i]);
        }
        if (status == NX_SUCCESS) {
            status = example_motor_drive_start(&drives[i], 1000);
        }
        if (status != NX_SUCCESS) {
            (void)withdraw(drives);
            return status;
        }
    }
    double squared_speed[2] = {0}, squared_current[2] = {0};
    uint32_t samples[2] = {0};
    uint32_t budget_revision = 0;
    example_dual_motor_report_t result = {0};
    bool stop_requested = false;
    for (uint32_t elapsed = 0; elapsed < experiment->duration_us;
         elapsed += FAST_US) {
        const nx_time_us_t beginning = 1000U + elapsed;
        if (experiment->bus_limits_at_us != 0 &&
            elapsed >= experiment->bus_limits_at_us && elapsed % 1000 == 0 &&
            (experiment->bus_source_stop_at_us == 0 ||
             elapsed < experiment->bus_source_stop_at_us)) {
            status = budget_limits(experiment, drives, beginning,
                                   ++budget_revision, &result);
            if (status != NX_SUCCESS) {
                (void)withdraw(drives);
                return status;
            }
        }
        for (size_t i = 0; i < 2; ++i) {
            const nx_time_us_t task_at =
                beginning + stages[i].applied.trigger_ticks / TICKS_PER_US + 1;
            const bool sensor_fault = i == 0 &&
                                      experiment->sensor_fault_at_us != 0 &&
                                      elapsed >= experiment->sensor_fault_at_us;
            status = model_cycle(&stages[i], &plants[i], &configs[i], beginning,
                                 experiment->bus_voltage_v,
                                 experiment->load_nm[i], sensor_fault);
            if (status != NX_SUCCESS) {
                (void)withdraw(drives);
                return status;
            }
            const bool task_paused =
                i == 0 && experiment->task_pause_at_us != 0 &&
                elapsed >= experiment->task_pause_at_us &&
                (uint64_t)elapsed < (uint64_t)experiment->task_pause_at_us +
                                        experiment->task_pause_for_us;
            if (!task_paused && drives[i].borrowed) {
                (void)example_motor_drive_step(&drives[i], task_at);
            }
            if (experiment->bus_limits_at_us != 0 &&
                elapsed >= experiment->bus_limits_at_us + 10000 &&
                drives[i].have_feedback) {
                result.admitted_current_a[i] =
                    fmaxf(result.admitted_current_a[i],
                          fabsf(drives[i].last_feedback.accepted_iq_a));
            }
            if (elapsed >= experiment->evaluation_start_us &&
                stages[i].fault == NX_SUCCESS) {
                const double speed_error = plants[i].velocity_rad_s -
                                           configs[i].initial_velocity_rad_s;
                const double current_error = plants[i].iq_a - configs[i].iq_a;
                squared_speed[i] += speed_error * speed_error;
                squared_current[i] += current_error * current_error;
                ++samples[i];
            }
        }
        /* Model truth is only result assessment, never input to the policy. */
        if (plants[0].dc_current_a > 0.0001 &&
            plants[1].dc_current_a > 0.0001) {
            ++result.motoring_samples;
        }
        if (plants[0].dc_current_a < -0.0001 ||
            plants[1].dc_current_a < -0.0001) {
            ++result.regenerating_samples;
        }
        if (plants[0].dc_current_a < -0.0001 &&
            plants[1].dc_current_a < -0.0001) {
            ++result.both_regenerating_samples;
        }
        if (plants[0].dc_current_a * plants[1].dc_current_a < -0.00000001) {
            ++result.opposed_flow_samples;
        }
        if (!stop_requested && experiment->controlled_stop_at_us != 0 &&
            elapsed >= experiment->controlled_stop_at_us) {
            for (size_t i = 0; i < 2; ++i) {
                if ((experiment->stop_axis_mask & (1U << i)) != 0 &&
                    drives[i].borrowed &&
                    drives[i].axis.state != NX_AXIS_DISABLED) {
                    (void)nx_axis_request_stop(&drives[i].axis);
                }
            }
            stop_requested = true;
        }
    }
    status = withdraw(drives);
    if (status != NX_SUCCESS) {
        return status;
    }
    result.all_settled = !drives[0].borrowed && !drives[1].borrowed;
    result.evaluation_samples =
        samples[0] > samples[1] ? samples[0] : samples[1];
    for (size_t i = 0; i < 2; ++i) {
        result.position_rad[i] = plants[i].position_rad;
        result.velocity_rad_s[i] = plants[i].velocity_rad_s;
        result.iq_a[i] = plants[i].iq_a;
        result.measured_position_rad[i] = drives[i].axis.feedback.position_rad;
        result.axis_state[i] = drives[i].axis.state;
        result.fault[i] = drives[i].primary;
        result.adopted_generation[i] = drives[i].context.fast.active.generation;
        result.completed_cycles[i] = stages[i].cycles;
        if (samples[i] != 0) {
            result.rms_velocity_error_rad_s[i] =
                sqrt(squared_speed[i] / samples[i]);
            result.rms_current_error_a[i] =
                sqrt(squared_current[i] / samples[i]);
        }
    }
    *report = result;
    return NX_SUCCESS;
}
