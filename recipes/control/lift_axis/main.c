/**
 * \file            main.c
 * \brief           Finite software-only gravity, brake and homing experiment
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-24
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "lift_axis.h"
#include "native/model.h"
#include <stdio.h>

int main(void) {
    const nx_axis_config_t axis_config = {1, 1, 2000, 2000,
                                          NX_AXIS_HOLD_ZERO_VELOCITY};
    const nx_axis_parameters_t parameters = {.revision = 1,
                                             .velocity_kp = 2,
                                             .velocity_ki = 1,
                                             .tracking_rate_per_s = 1,
                                             .integral_limit_a = 5,
                                             .position_kp = 2,
                                             .current_limit_a = 5,
                                             .velocity_limit_rad_s = 1,
                                             .acceleration_rad_s2 = 10,
                                             .minimum_position_rad = -1,
                                             .maximum_position_rad = 1,
                                             .position_tolerance_rad = 0.001F,
                                             .velocity_tolerance_rad_s = 0.01F,
                                             .settling_us = 2000,
                                             .stop_feedforward_a = 1};
    const example_lift_config_t lift_config = {
        .maximum_feedback_age_us = 2000,
        .handover_timeout_us = 50000,
        .homing_timeout_us = 1000000,
        .homing_phase_timeout_us = 500000,
        .homing_maximum_phase_distance_rad = 0.1F,
        .search_velocity_rad_s = 0.1F,
        .approach_velocity_rad_s = 0.02F,
        .home_coordinate_rad = 0,
        .support_feedforward_a = 1,
        .on_feedback_loss = EXAMPLE_LIFT_BRAKE_KEEP,
        .on_power_loss = EXAMPLE_LIFT_BRAKE_KEEP};
    const example_lift_model_config_t plant = {.torque_nm_per_a = 1,
                                               .inertia_kg_m2 = 0.01F,
                                               .load_torque_nm = 1,
                                               .damping_nm_per_rad_s = 0.02F,
                                               .current_time_constant_s =
                                                   0.002F,
                                               .encoder_rad_per_count = 1e-6F,
                                               .support_tolerance_nm = 0.08F,
                                               .brake_transition_us = 5000,
                                               .initial_position_rad = 0.01F,
                                               .home_position_rad = 0,
                                               .home_hysteresis_rad = 0.002F,
                                               .upper_limit_rad = 1,
                                               .lower_limit_rad = -0.1F};
    nx_axis_t axis;
    example_lift_t lift;
    example_lift_model_t model;
    nx_time_us_t now = 1000;
    if (nx_axis_initialize(&axis, &axis_config, &parameters) != NX_SUCCESS ||
        example_lift_initialize(&lift, &axis, &lift_config) != NX_SUCCESS ||
        example_lift_model_initialize(&model, &plant, 1, 1, now) !=
            NX_SUCCESS) {
        return 1;
    }
    example_lift_feedback_t feedback;
    example_lift_output_t output = lift.output;
    now += 1000;
    if (example_lift_model_advance(&model, &output, now, 0.001F, &feedback) !=
            NX_SUCCESS ||
        example_lift_start(&lift, &feedback, now) != NX_SUCCESS) {
        return 1;
    }
    bool homing_started = false;
    unsigned samples;
    for (samples = 0; samples < 1500; ++samples) {
        now += 1000;
        if (example_lift_model_advance(&model, &output, now, 0.001F,
                                       &feedback) != NX_SUCCESS ||
            example_lift_step(&lift, &feedback, now, 0.001F, &output) !=
                NX_SUCCESS) {
            return 1;
        }
        if (!homing_started && output.state == EXAMPLE_LIFT_ACTIVE) {
            if (example_lift_home(&lift, &feedback, now) != NX_SUCCESS) {
                return 1;
            }
            homing_started = true;
        }
        if (homing_started && output.state == EXAMPLE_LIFT_SECURED) {
            break;
        }
    }
    const bool passed = samples < 1500 && output.reference_valid &&
                        !output.motor_permit &&
                        feedback.brake == EXAMPLE_LIFT_BRAKE_ENGAGED;
    (void)printf("software_only samples=%u homed=%u brake_confirmed=%u "
                 "motor_permit=%u offset_rad=%.6f truth_rad=%.6f\n",
                 samples, (unsigned)output.reference_valid,
                 (unsigned)(feedback.brake == EXAMPLE_LIFT_BRAKE_ENGAGED),
                 (unsigned)output.motor_permit,
                 (double)output.reference_offset_rad,
                 (double)model.position_rad);
    return passed ? 0 : 1;
}
