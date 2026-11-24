/**
 * \file            servo_control.c
 * \brief           Explicit slow-axis and fast scalar current-loop connection
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-24
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "servo_control.h"
#include <math.h>

static bool disjoint(const void* first, size_t first_bytes, const void* second,
                     size_t second_bytes) {
    const uintptr_t a = (uintptr_t)first;
    const uintptr_t b = (uintptr_t)second;
    return first != NULL && second != NULL && first_bytes <= UINTPTR_MAX - a &&
           second_bytes <= UINTPTR_MAX - b &&
           (a >= b + second_bytes || b >= a + first_bytes);
}

nx_result_t
example_servo_control_initialize(example_servo_control_t* control,
                                 const example_servo_config_t* config) {
    if (control == NULL || config == NULL ||
        !disjoint(control, sizeof(*control), config, sizeof(*config)) ||
        config->encoder_counts_per_turn == 0 ||
        !isfinite(config->current_a_per_code) ||
        config->current_a_per_code <= 0 || config->current_pi.kd != 0 ||
        config->current_pi.output_minimum > 0 ||
        config->current_pi.output_maximum < 0) {
        return NX_ERROR_INVALID;
    }
    example_servo_control_t next = {0};
    nx_result_t result =
        nx_axis_initialize(&next.axis, &config->axis, &config->parameters);
    if (result == NX_SUCCESS) {
        result = nx_pid_initialize(&next.current_pi, &config->current_pi);
    }
    if (result == NX_SUCCESS) {
        next.initialized = true;
        *control = next;
    }
    return result;
}

nx_result_t example_servo_current_step(example_servo_control_t* control,
                                       float current_a, bool valid, float dt_s,
                                       float* voltage_v) {
    if (control == NULL || voltage_v == NULL ||
        !disjoint(control, sizeof(*control), voltage_v, sizeof(*voltage_v))) {
        return NX_ERROR_INVALID;
    }
    if (!control->initialized || control->current_fault != NX_SUCCESS) {
        return NX_ERROR_STATE;
    }
    if (!valid || !isfinite(current_a) || !isfinite(dt_s) || dt_s <= 0) {
        control->current_fault = NX_ERROR_STATE;
        return NX_ERROR_STATE;
    }
    if (control->axis.state == NX_AXIS_DISABLED ||
        control->axis.state == NX_AXIS_READY ||
        control->axis.state == NX_AXIS_FAULT) {
        *voltage_v = 0;
        return NX_SUCCESS;
    }
    float planned;
    const nx_result_t result =
        nx_pid_step(&control->current_pi, control->axis.output.current_a,
                    current_a, dt_s, &planned);
    if (result != NX_SUCCESS) {
        control->current_fault = result;
        return result;
    }
    *voltage_v = planned;
    return NX_SUCCESS;
}
