/**
 * \file            main.c
 * \brief           Explicit commissioning gate and finite F407 drive entry
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-24
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "control.h"
#include "nexus_bindings.h"
#include "nexus_factory_strong.h"

static example_motor_drive_t s_drive;
static example_motor_observation_t s_latest;
static uint32_t s_dropped;

/* Task-only complete binary snapshot sink. A commissioned product may replace
 * this with its RTT/UART adapter; partial writes remain that adapter's state.
 */
static nx_result_t capture(void* context,
                           const example_motor_observation_t* record) {
    *(example_motor_observation_t*)context = *record;
    return NX_SUCCESS;
}

int main(void) {
    /* These constants exercise software composition. They are not measured
     * power-board calibration or proof of an initial rotor reference. */
    const example_motor_drive_config_t selected = {
        .motor = {7,   1, 0, 1000, 1100, 100, 1100, 0,    2,     100, 2,
                  100, 3, 5, 0.2F, 12,   10,  30,   0.9F, 0.05F, 100},
        .current_zero_code = {2048, 2048, 2048},
        .current_a_per_code = {0.01F, 0.01F, 0.01F},
        .bus_v_per_code = 0.01F,
        .encoder_counts_per_turn = 4096,
        .maximum_encoder_delta = 100,
        .maximum_scan_span_us = 4,
        .run_duration_us = 100000,
        .startup_budget_us = 10000,
        .commissioned = false};
    if (nx_platform_start().primary != NX_SUCCESS) {
        return 1;
    }
    const nx_power_stage_t* port =
        nx_factory_power_stage_ref(NX_POWER_STAGE_REF_DRIVE);
    nx_result_t result =
        example_motor_drive_initialize(&s_drive, port, &selected);
    if (result == NX_SUCCESS) {
        result = example_motor_drive_start(&s_drive, nx_time_now_us());
    }
    if (result != NX_SUCCESS) {
        return nx_platform_stop() == NX_SUCCESS ? 2 : 3;
    }
    while (s_drive.borrowed) {
        uint32_t delivered;
        (void)example_motor_observation_drain(
            &s_drive.observations, capture, &s_latest,
            EXAMPLE_MOTOR_OBSERVATION_DEPTH, &delivered, &s_dropped);
        result = example_motor_drive_step(&s_drive, nx_time_now_us());
        /* A failed drain retains this static state and repeats withdrawal.
         * It never returns from main while an IRQ can still access it. */
    }
    uint32_t delivered;
    const nx_result_t observed = example_motor_observation_drain(
        &s_drive.observations, capture, &s_latest,
        EXAMPLE_MOTOR_OBSERVATION_DEPTH + 1U, &delivered, &s_dropped);
    const nx_result_t cleanup = nx_platform_stop();
    return result == NX_SUCCESS && observed == NX_ERROR_EMPTY &&
                   cleanup == NX_SUCCESS
               ? 0
               : 4;
}
