/**
 * \file            main.c
 * \brief           Station-armed finite traffic; no implicit physical tooling.
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-10-20
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "nexus/arch/arch.h"
#include "nexus_bindings.h"
#include "nexus_factory.h"
#include "workload.h"

#define FIFO_STATION_ARM_MAGIC 0x46514631U
#define FIFO_STATION_ARM_POLLS 1000000U

/**
 * \brief Debugger control, written while halted after startup/BSS
 * initialization. \note The station waits for waiting_for_arm, writes session
 * then arm and resumes. It must not halt or rewrite control during acquisition.
 *       Symbol addresses/layout come from this exact unstripped ELF/DWARF.
 */
typedef struct {
    volatile uint32_t session;
    volatile uint32_t arm;
} fifo_station_control_t;

fifo_station_control_t g_fifo_station_control;
fifo_acquisition_t g_fifo_acquisition;
volatile uint32_t g_fifo_waiting_for_arm;
volatile uint32_t g_fifo_platform_closed;
volatile uint32_t g_fifo_cleanup_attempts;
volatile nx_result_t g_fifo_cleanup_result;
volatile nx_result_t g_fifo_arm_result;
volatile nx_result_t g_fifo_start_primary;
volatile nx_result_t g_fifo_start_cleanup;
volatile size_t g_fifo_start_remaining;

/** \brief Retain all storage forever; this is a fail-stop, not a cleanup join.
 */
static _Noreturn void keep_storage(void) {
    for (;;) {
    }
}

/** \brief No new I/O or clock reads occur after Platform withdrawal begins. */
static _Noreturn void close_platform(void) {
    uint32_t attempts = 0U;
    nx_result_t result = fifo_acquisition_join_platform(
        &g_fifo_acquisition, nx_platform_stop,
        FIFO_ACQUISITION_CLEANUP_ATTEMPTS, &attempts);
    g_fifo_cleanup_attempts = attempts;
    g_fifo_cleanup_result = result;
    if (result == NX_SUCCESS) {
        nx_arch_dmb();
        g_fifo_platform_closed = 1U;
    }
    /* BUSY/IO exhaustion retains all state, with closed still zero. Nothing
     * prints to the measured UART or uses stdio/semihosting on any exit. */
    keep_storage();
}

/** \brief Bound arming by both a live deadline and an independent poll cap. */
static bool wait_for_arm(nx_time_us_t* last) {
    *last = nx_time_now_us();
    nx_time_us_t end = nx_deadline_after(*last, 10000000U);
    if (*last == NX_DEADLINE_NEVER || end == NX_DEADLINE_NEVER) {
        g_fifo_arm_result = NX_ERROR_STATE;
        return false;
    }
    g_fifo_waiting_for_arm = 1U;
    for (uint32_t poll = 0U; poll < FIFO_STATION_ARM_POLLS; ++poll) {
        if (g_fifo_station_control.arm == FIFO_STATION_ARM_MAGIC) {
            g_fifo_waiting_for_arm = 0U;
            return true;
        }
        nx_time_us_t now = nx_time_now_us();
        if (now == NX_DEADLINE_NEVER || now < *last) {
            g_fifo_arm_result = NX_ERROR_STATE;
            g_fifo_waiting_for_arm = 0U;
            return false;
        }
        *last = now;
        if (nx_deadline_expired(end, now)) {
            g_fifo_arm_result = NX_ERROR_TIMEOUT;
            g_fifo_waiting_for_arm = 0U;
            return false;
        }
    }
    /* A stalled clock cannot make arming an unbounded provider-poll loop.
     * Count exhaustion does not prove timeout or a healthy clock for stop. */
    g_fifo_arm_result = NX_ERROR_EXHAUSTED;
    g_fifo_waiting_for_arm = 0U;
    return false;
}

/** \brief Run actual finite concurrent I/O; a station owns timing capture. */
int main(void) {
    nx_platform_start_result_t start = nx_platform_start();
    g_fifo_start_primary = start.primary;
    g_fifo_start_cleanup = start.cleanup;
    g_fifo_start_remaining = start.remaining_effects;
    if (start.primary != NX_SUCCESS || start.cleanup != NX_SUCCESS ||
        start.remaining_effects != 0U) {
        close_platform();
    }
    nx_time_us_t armed_at;
    if (!wait_for_arm(&armed_at)) {
        if (g_fifo_arm_result == NX_ERROR_TIMEOUT) {
            close_platform();
        }
        /* Invalid/stalled epoch is not authorization to call a provider or
         * use its old deadlines. Both reference and instrumented storage live.
         */
        keep_storage();
    }
    /* The station binds actual fixed values and the runtime session to its
     * protocol. CPU work and poll counts are finite loads, never SI-time units.
     */
    fifo_acquisition_config_t config = {.session =
                                            g_fifo_station_control.session,
                                        .duration_us = 100000U,
                                        .service_period_us = 500U,
                                        .transfer_timeout_us = 20000U,
                                        .drain_timeout_us = 100000U,
                                        .cpu_work_units = 64U};
    nx_time_us_t now = nx_time_now_us();
    if (now == NX_DEADLINE_NEVER || now < armed_at) {
        g_fifo_arm_result = NX_ERROR_STATE;
        keep_storage();
    }
    if (fifo_acquisition_initialize(&g_fifo_acquisition,
                                    nx_factory_uart(NX_UART_ID_CONSOLE),
                                    &config, now) != NX_SUCCESS) {
        close_platform();
    }
    while (g_fifo_acquisition.phase != FIFO_ACQUISITION_CLOSED) {
        (void)fifo_acquisition_step(&g_fifo_acquisition, nx_time_now_us());
        if (g_fifo_acquisition.clock_fault) {
            /* Retained traffic can recover by late TC only with a live epoch.
             * A broken epoch requires external recovery; storage is not freed.
             */
            keep_storage();
        }
    }
    close_platform();
    return 1;
}
