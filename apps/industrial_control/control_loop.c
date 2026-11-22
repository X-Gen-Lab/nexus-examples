/**
 * \file            control_loop.c
 * \brief           Explicit consumer composition of real platform mechanisms
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-21
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "control_loop.h"
#include <math.h>
#include <string.h>

enum { CONTROL_EVENT_FAULT = 1, CONTROL_EVENT_STOP = 2 };

static const nx_fsm_state_t control_states[] = {
    {NULL, NULL}, {NULL, NULL}, {NULL, NULL}};
static const nx_fsm_transition_t control_transitions[] = {
    {EXAMPLE_CONTROL_RUNNING, EXAMPLE_CONTROL_FAULT, CONTROL_EVENT_FAULT,
     NX_FSM_EXTERNAL, NULL, NULL},
    {EXAMPLE_CONTROL_RUNNING, EXAMPLE_CONTROL_STOPPED, CONTROL_EVENT_STOP,
     NX_FSM_EXTERNAL, NULL, NULL},
    {EXAMPLE_CONTROL_FAULT, EXAMPLE_CONTROL_STOPPED, CONTROL_EVENT_STOP,
     NX_FSM_EXTERNAL, NULL, NULL}};
static const nx_fsm_config_t control_machine = {
    control_states, sizeof(control_states) / sizeof(control_states[0]),
    control_transitions,
    sizeof(control_transitions) / sizeof(control_transitions[0])};

static nx_result_t fault_control(example_control_loop_t* loop,
                                 nx_result_t result) {
    const nx_fsm_event_t event = {CONTROL_EVENT_FAULT, NULL, 0};
    (void)nx_fsm_dispatch(&loop->machine, &event);
    loop->output_stop_result = nx_pwm_port_stop(loop->config.pwm);
    loop->output_stop_pending = loop->output_stop_result != NX_SUCCESS;
    return result;
}

nx_result_t
example_control_loop_initialize(example_control_loop_t* loop,
                                const example_control_config_t* config) {
    if (loop == NULL || config == NULL || config->adc == NULL ||
        config->pwm == NULL || config->stream == NULL ||
        config->pwm_period_ticks == 0 || config->sample_period_us == 0 ||
        config->full_scale_counts == 0 || !isfinite(config->alpha) ||
        config->alpha <= 0 || config->alpha > 1 ||
        !isfinite(config->setpoint) || config->setpoint < 0 ||
        config->setpoint > 1 || config->pid.output_minimum < 0 ||
        config->pid.output_maximum > 1) {
        return NX_ERROR_INVALID;
    }
    example_control_loop_t next = {0};
    next.config = *config;
    nx_result_t result = nx_pid_initialize(&next.pid, &config->pid);
    if (result != NX_SUCCESS) {
        return result;
    }
    result = nx_fsm_initialize(&next.machine, &control_machine, NULL,
                               EXAMPLE_CONTROL_RUNNING);
    if (result != NX_SUCCESS) {
        return result;
    }
    *loop = next;
    return NX_SUCCESS;
}

nx_result_t example_control_loop_step(example_control_loop_t* loop,
                                      example_control_observation_t* out) {
    if (loop == NULL || out == NULL) {
        return NX_ERROR_INVALID;
    }
    if (loop->machine.config != &control_machine ||
        loop->machine.current != EXAMPLE_CONTROL_RUNNING || loop->borrowed) {
        return NX_ERROR_STATE;
    }
    nx_result_t result = nx_adc_port_stream_service(loop->config.adc);
    if (result != NX_SUCCESS && result != NX_ERROR_BUSY) {
        return fault_control(loop, result);
    }
    result = nx_stream_acquire(loop->config.stream, &loop->borrowed_block);
    if (result == NX_ERROR_EMPTY) {
        return result;
    }
    if (result != NX_SUCCESS) {
        return fault_control(loop, result);
    }
    loop->borrowed = true;
    nx_ema_t filter = loop->filter;
    nx_pid_t pid = loop->pid;
    example_control_observation_t observation = {0};
    if (loop->borrowed_block.length != sizeof(uint16_t) ||
        (loop->borrowed_block.flags &
         (NX_STREAM_BOUNDARY_LOSS | NX_STREAM_BOUNDARY_ERROR)) != 0U) {
        result = NX_ERROR_IO;
    } else {
        uint16_t raw;
        memcpy(&raw, loop->borrowed_block.data, sizeof(raw));
        if (raw > loop->config.full_scale_counts) {
            result = NX_ERROR_INVALID;
        } else {
            const float normalized =
                (float)raw / loop->config.full_scale_counts;
            result = nx_ema_step(&filter, normalized, loop->config.alpha,
                                 &observation.measurement);
        }
    }
    if (result == NX_SUCCESS) {
        const float dt_s =
            (float)((double)loop->config.sample_period_us / 1000000.0);
        result =
            nx_pid_step(&pid, loop->config.setpoint, observation.measurement,
                        dt_s, &observation.command);
    }
    if (result == NX_SUCCESS) {
        observation.duty_ticks = (uint32_t)((double)observation.command *
                                                loop->config.pwm_period_ticks +
                                            0.5);
        result =
            nx_pwm_port_set(loop->config.pwm, loop->config.pwm_period_ticks,
                            observation.duty_ticks);
    }
    const nx_result_t released =
        nx_stream_release(loop->config.stream, &loop->borrowed_block);
    if (released == NX_SUCCESS) {
        loop->borrowed = false;
    }
    if (result == NX_SUCCESS) {
        result = released;
    }
    if (result != NX_SUCCESS) {
        return fault_control(loop, result);
    }
    loop->filter = filter;
    loop->pid = pid;
    if (loop->samples != UINT32_MAX) {
        ++loop->samples;
    }
    observation.samples = loop->samples;
    *out = observation;
    return NX_SUCCESS;
}

nx_result_t example_control_loop_finish(example_control_loop_t* loop) {
    if (loop == NULL) {
        return NX_ERROR_INVALID;
    }
    if (loop->machine.config != &control_machine) {
        return NX_ERROR_STATE;
    }
    if (loop->output_stop_pending) {
        loop->output_stop_result = nx_pwm_port_stop(loop->config.pwm);
        if (loop->output_stop_result != NX_SUCCESS) {
            return loop->output_stop_result;
        }
        loop->output_stop_pending = false;
    }
    if (loop->borrowed) {
        const nx_result_t result =
            nx_stream_release(loop->config.stream, &loop->borrowed_block);
        if (result != NX_SUCCESS) {
            return result;
        }
        loop->borrowed = false;
    }
    if (loop->machine.current == EXAMPLE_CONTROL_STOPPED) {
        return NX_SUCCESS;
    }
    const nx_fsm_event_t event = {CONTROL_EVENT_STOP, NULL, 0};
    return nx_fsm_dispatch(&loop->machine, &event);
}
