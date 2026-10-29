/**
 * \file            main.c
 * \brief           Opt-in finite U575 SRAM4 copy with compound Stop2 recovery
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-10-30
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "nexus/components/stm32u575_stop2_copy.h"
#include "nexus/core/memory.h"
#include "nexus_bindings.h"
#include "stop2_application.h"
#include <string.h>

static nx_stm32u575_retained_time_storage_t s_clock;
static nx_clock_t s_clock_face;
static nx_stm32u575_lpdma_copy_storage_t s_copy;
static nx_stm32u575_lpdma_copy_t s_copy_face;
static nx_stm32u575_lpdma_copy_request_t s_request;
static nx_stm32u575_stop2_copy_result_t s_entered;
static example_stop2_application_t s_application;
static bool s_enter_attempted;
static bool s_result_acquired;
static bool s_copy_stopped;
static nx_result_t s_operation;

NX_MEMORY_SRAM4_DATA static unsigned char s_source[17] = {
    1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U, 11U, 12U, 13U, 14U, 15U, 16U, 17U};
NX_MEMORY_SRAM4_BSS static unsigned char s_destination[17];
NX_MEMORY_SRAM4_NOINIT static nx_stm32u575_lpdma_copy_workspace_t s_workspace;

_Static_assert(sizeof(s_workspace) == 24U,
               "The complete generated LLI is 24 B");
_Static_assert(sizeof(s_source) == sizeof(s_destination),
               "Both finite copy spans have the same exact byte extent");

/** \brief Explicit opt-in after prepared reset, RTC and electrical review.
 * \note Set 0x4e584350 at a breakpoint in main; this is not authentication.
 */
volatile uint32_t example_stop2_copy_authorization;
volatile nx_result_t example_stop2_copy_result = NX_ERROR_PERMISSION;
volatile example_stop2_phase_t example_stop2_copy_phase;
volatile bool example_stop2_copy_accepted;
volatile bool example_stop2_copy_dma_wake;
volatile nx_result_t example_stop2_copy_settled_result = NX_ERROR_STATE;
volatile size_t example_stop2_copy_transferred;
volatile bool example_stop2_copy_restarted;
volatile nx_time_us_t example_stop2_copy_elapsed_us;

/** \brief Preserve every exact owner, borrow, barrier, lease and saved mask. */
static _Noreturn void retain(void) {
    for (;;) {
    }
}

/** \brief Stop the independent clock only after all of its users joined. */
static int close_clock(int result) {
    nx_result_t stopped = nx_stm32u575_retained_time_stop(&s_clock);
    if (stopped != NX_SUCCESS) {
        if (example_stop2_copy_result == NX_SUCCESS) {
            example_stop2_copy_result = stopped;
        }
        retain();
    }
    return result;
}

/** \brief Join a constructed but unborrowed receiver on preparation failure. */
static int close_copy_clock(int result) {
    if (nx_stm32u575_lpdma_copy_stop(&s_copy_face, 10000U) != NX_SUCCESS) {
        retain();
    }
    return close_clock(result);
}

/** \brief Keep original admission and wake facts across later recover calls. */
nx_stm32u575_stop2_result_t
example_stop2_application_enter(nx_clock_barrier_t owners) {
    s_enter_attempted = true;
    s_entered = nx_stm32u575_stop2_copy_enter(owners, &s_copy_face, &s_request,
                                              4096U, 10000U);
    example_stop2_copy_accepted = s_entered.accepted;
    example_stop2_copy_dma_wake = s_entered.dma_wake;
    return s_entered.transition;
}

/** \brief Validate original retained observations before any RUN restart. */
static bool entry_observed(void) {
    const nx_stm32u575_stop2_result_t* transition = &s_entered.transition;
    return s_entered.accepted && s_entered.dma_wake &&
           transition->wait_executed && !transition->wake_flag &&
           transition->before.identity != NULL &&
           transition->before.identity == transition->after.identity &&
           transition->before.generation != 0U &&
           transition->before.generation == transition->after.generation &&
           transition->after.now >= transition->before.now;
}

/** \brief Acquire once, then join the receiver before unmasking or restart. */
example_stop2_join_result_t example_stop2_application_join(void) {
    if (s_copy_stopped) {
        const example_stop2_join_result_t joined = {NX_SUCCESS, s_operation};
        return joined;
    }
    if (!s_result_acquired) {
        if (s_entered.accepted) {
            nx_result_t final;
            size_t transferred;
            nx_result_t acquired =
                nx_request_result(&s_request.base, &final, &transferred);
            if (acquired != NX_SUCCESS) {
                const example_stop2_join_result_t pending = {acquired,
                                                             NX_SUCCESS};
                return pending;
            }
            example_stop2_copy_settled_result = final;
            example_stop2_copy_transferred = transferred;
            s_operation = final;
            if (s_operation == NX_SUCCESS &&
                (transferred != sizeof(s_source) || s_request.source == NULL ||
                 s_request.destination == NULL ||
                 memcmp(s_request.source, s_request.destination,
                        sizeof(s_source)) != 0 ||
                 !entry_observed())) {
                s_operation = NX_ERROR_IO;
            }
        } else if (s_enter_attempted) {
            /* Rejection retains no borrow; READY has no settled result. */
            s_operation = NX_ERROR_STATE;
        }
        s_result_acquired = true;
    }
    nx_result_t stopped = nx_stm32u575_lpdma_copy_stop(&s_copy_face, 10000U);
    if (stopped == NX_SUCCESS) {
        s_copy_stopped = true;
    }
    const example_stop2_join_result_t result = {stopped, s_operation};
    return result;
}

/** \brief Prepared boot owns the source and gates; application owns one copy.
 */
int main(void) {
    if (example_stop2_copy_authorization != UINT32_C(0x4e584350)) {
        return 0;
    }
    const nx_stm32u575_retained_time_config_t retained = {
        NX_U575_RETAINED_TIME_RTC, 60U, nx_stm32u575_stop2_identity()};
    example_stop2_copy_result =
        nx_stm32u575_retained_time_initialize_alarm_a_copy(&s_clock, &retained,
                                                           &s_clock_face);
    if (example_stop2_copy_result != NX_SUCCESS) {
        return 1;
    }
    example_stop2_copy_result = nx_stm32u575_stop2_initialize(1U, &s_clock);
    if (example_stop2_copy_result != NX_SUCCESS) {
        return close_clock(2);
    }
    const nx_stm32u575_lpdma_copy_config_t copy = {
        &s_clock, nx_stm32u575_stop2_identity()};
    example_stop2_copy_result =
        nx_stm32u575_lpdma_copy_initialize(&s_copy, &copy, &s_copy_face);
    if (example_stop2_copy_result != NX_SUCCESS) {
        return close_clock(3);
    }
    nx_request_initialize(&s_request.base);
    nx_stm32u575_retained_time_snapshot_t snapshot;
    example_stop2_copy_result =
        nx_stm32u575_retained_time_sample(&s_clock, &snapshot);
    if (example_stop2_copy_result != NX_SUCCESS) {
        return close_copy_clock(4);
    }
    example_stop2_copy_result = nx_stm32u575_lpdma_copy_request_prepare(
        &s_request, &snapshot, s_source, s_destination, sizeof(s_source),
        &s_workspace, NX_DEADLINE_NEVER);
    if (example_stop2_copy_result != NX_SUCCESS) {
        return close_copy_clock(4);
    }
    s_application.domain = nx_stm32u575_stop2_domain();
    example_stop2_copy_result = nx_clock_domain_acquire(
        s_application.domain, &s_application.platform_lease);
    if (example_stop2_copy_result != NX_SUCCESS) {
        return close_copy_clock(5);
    }
    nx_platform_start_result_t started = nx_platform_start();
    s_application.first_error = started.primary != NX_SUCCESS ? started.primary
                                : started.cleanup != NX_SUCCESS
                                    ? started.cleanup
                                : started.remaining_effects != 0U ? NX_ERROR_IO
                                                                  : NX_SUCCESS;
    for (unsigned attempt = 0U;
         attempt < 64U && s_application.phase != EXAMPLE_STOP2_DONE;
         ++attempt) {
        example_stop2_copy_result = example_stop2_progress(&s_application);
        example_stop2_copy_phase = s_application.phase;
        example_stop2_copy_restarted = s_application.restarted;
    }
    if (s_application.phase != EXAMPLE_STOP2_DONE) {
        if (s_application.first_error != NX_SUCCESS) {
            example_stop2_copy_result = s_application.first_error;
        }
        retain();
    }
    example_stop2_copy_result = s_application.first_error;
    if (example_stop2_copy_result == NX_SUCCESS) {
        example_stop2_copy_elapsed_us =
            s_entered.transition.after.now - s_entered.transition.before.now;
    }
    return close_clock(example_stop2_copy_result == NX_SUCCESS ? 0 : 6);
}
