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
#if defined(EXAMPLE_MOTOR_RTT)
#include "nexus/components/rtt.h"
#include "telemetry.h"
#endif

static example_motor_drive_t s_drive;
#if defined(EXAMPLE_MOTOR_RTT)
static nx_rtt_t s_rtt;
static uint8_t s_up[512], s_down[16];
static nx_log_sink_port_t s_rtt_port;
static example_motor_telemetry_t s_telemetry;
#else
static example_motor_observation_t s_latest;
#endif
static uint32_t s_dropped;

#if defined(EXAMPLE_MOTOR_RTT)
static nx_result_t rtt_write(void* context, const uint8_t* bytes, size_t length,
                             size_t* written) {
    nx_log_sink_port_t* sink = context;
    *written = 0;
    const nx_result_t result =
        sink->write(sink->context, NX_LOG_INFO, bytes, length, 0);
    if (result == NX_SUCCESS) {
        *written = length;
    }
    return result;
}
#endif

/* Both sinks run only in Task. RTT is explicitly selected and never waits for
 * a debugger; its ring/name remain static for the whole image lifetime. */
static nx_result_t capture(void* context,
                           const example_motor_observation_t* record) {
#if defined(EXAMPLE_MOTOR_RTT)
    (void)context;
    s_telemetry.dropped = s_dropped;
    return example_motor_telemetry_sink(&s_telemetry, record);
#else
    *(example_motor_observation_t*)context = *record;
    return NX_SUCCESS;
#endif
}

int main(void) {
    /* These constants exercise software composition. They are not measured
     * power-board calibration or proof of an initial rotor reference. */
    example_motor_drive_config_t selected = example_motor_drive_reference();
#if defined(EXAMPLE_SINGLE_SHUNT)
    selected.topology = NX_MODULATION_SINGLE_SHUNT;
    selected.phase_samples = 1;
    selected.bootstrap_voltage_ab = (nx_motor_ab_t){2, 1};
    selected.bootstrap_bus_voltage_v = 24;
    selected.maximum_scan_span_us = 500;
    for (size_t phase = 1; phase < 3; ++phase) {
        selected.current_zero_code[phase] = 0;
        selected.current_a_per_code[phase] = 0;
    }
    /* This route has no bus/rotor ranks. A commissioned product must bind
     * qualified external observation queues before enabling the session. */
#endif
    if (nx_platform_start().primary != NX_SUCCESS) {
        return 1;
    }
    const nx_power_stage_t* port =
        nx_factory_power_stage_ref(NX_POWER_STAGE_REF_DRIVE);
#if defined(EXAMPLE_MOTOR_RTT)
    nx_power_stage_state_t state;
    if (nx_power_stage_state(port, &state) != NX_SUCCESS ||
        nx_rtt_init(&s_rtt, 1, "motor-v1", s_up, sizeof(s_up), s_down,
                    sizeof(s_down)) != NX_SUCCESS) {
        return nx_platform_stop() == NX_SUCCESS ? 2 : 3;
    }
    s_rtt_port = nx_rtt_log_sink(&s_rtt);
    if (example_motor_telemetry_initialize(&s_telemetry, rtt_write, &s_rtt_port,
                                           state.tick_hz) != NX_SUCCESS) {
        return nx_platform_stop() == NX_SUCCESS ? 2 : 3;
    }
    void* sink_context = &s_telemetry;
#else
    void* sink_context = &s_latest;
#endif
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
            &s_drive.observations, capture, sink_context,
            EXAMPLE_MOTOR_OBSERVATION_DEPTH, &delivered, &s_dropped);
        result = example_motor_drive_step(&s_drive, nx_time_now_us());
        /* A failed drain retains this static state and repeats withdrawal.
         * It never returns from main while an IRQ can still access it. */
    }
    uint32_t delivered;
    const nx_result_t observed = example_motor_observation_drain(
        &s_drive.observations, capture, sink_context,
        EXAMPLE_MOTOR_OBSERVATION_DEPTH + 1U, &delivered, &s_dropped);
    const nx_result_t cleanup = nx_platform_stop();
    return result == NX_SUCCESS && observed == NX_ERROR_EMPTY &&
                   cleanup == NX_SUCCESS
               ? 0
               : 4;
}
