/**
 * \file            main.c
 * \brief           Opt-in U575 single SRAM4 LPTIM1 tuple across one Stop2
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-10-30
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "nexus/components/stm32u575_stop2_lptim1.h"
#include "nexus/core/memory.h"
#include "nexus_bindings.h"
#include "stop2_application.h"

#define EXAMPLE_POLL_LIMIT     10000U
#define EXAMPLE_PROGRESS_LIMIT 64U

static nx_stm32u575_retained_time_storage_t s_clock;
static nx_clock_t s_clock_face;
static nx_stm32u575_lpdma_lptim1_storage_t s_receiver;
static nx_stm32u575_lpdma_lptim1_t s_receiver_face;
static nx_stm32u575_lpdma_lptim1_request_t s_request;
static nx_stm32u575_retained_time_snapshot_t s_snapshot;
static example_stop2_application_t s_application;
static nx_stm32u575_stop2_lptim1_result_t s_entry;
static bool s_enter_attempted;
static bool s_request_joined;
static bool s_receiver_stopped;
static nx_result_t s_operation_result;

/** \brief Immutable from prepare through acquire-observed settlement. */
NX_MEMORY_SRAM4_DATA static nx_stm32u575_lptim1_tuple_t s_tuple = {37U, 0U,
                                                                   2047U};
/** \brief Separate generated LLIs; no reads before the owner initializes it. */
NX_MEMORY_SRAM4_NOINIT static nx_stm32u575_lpdma_lptim1_workspace_t s_workspace;

/** \brief Debugger opt-in after exact boot, ownership and electrical review.
 * \note Set 0x4e584c31 at a breakpoint in main; this is no security token.
 */
volatile uint32_t example_stop2_lptim1_authorization;
volatile nx_result_t example_stop2_lptim1_result = NX_ERROR_PERMISSION;
volatile example_stop2_phase_t example_stop2_lptim1_phase;
volatile bool example_stop2_lptim1_restarted;
volatile bool example_stop2_lptim1_accepted;
volatile bool example_stop2_lptim1_dma_wake;
volatile nx_result_t example_stop2_lptim1_request_result = NX_ERROR_STATE;
volatile size_t example_stop2_lptim1_transferred;
volatile nx_time_us_t example_stop2_lptim1_elapsed_us;

/** \brief Preserve owners, exact lease, closed barrier, mask and all storage.
 */
static _Noreturn void retain(void) {
    (void)nx_arch_irq_save();
    for (;;) {
    }
}

/** \brief Close the independent clock only after every user has joined. */
static int close_clock(int result) {
    if (nx_stm32u575_retained_time_stop(&s_clock) != NX_SUCCESS) {
        retain();
    }
    return result;
}

/** \brief Join even partial public start before a pre-admission return. */
static int close_receiver(int result) {
    for (unsigned attempt = 0U; attempt < EXAMPLE_PROGRESS_LIMIT; ++attempt) {
        if (nx_stm32u575_lpdma_lptim1_stop(&s_receiver_face,
                                           EXAMPLE_POLL_LIMIT) == NX_SUCCESS) {
            return close_clock(result);
        }
    }
    retain();
}

/** \brief Direct compile-time hook: admit exactly one finite register tuple. */
nx_stm32u575_stop2_result_t
example_stop2_application_enter(nx_clock_barrier_t owners) {
    s_enter_attempted = true;
    s_entry = nx_stm32u575_stop2_lptim1_enter(
        owners, &s_receiver_face, &s_request, 4096U, EXAMPLE_POLL_LIMIT);
    example_stop2_lptim1_accepted = s_entry.accepted;
    example_stop2_lptim1_dma_wake = s_entry.dma_wake;
    return s_entry.transition;
}

/** \brief Validate original wake and retained snapshots before RUN resumes. */
static bool entry_observed(void) {
    const nx_stm32u575_stop2_result_t* transition = &s_entry.transition;
    return s_entry.accepted && s_entry.dma_wake && transition->wait_executed &&
           !transition->wake_flag && transition->before.identity != NULL &&
           transition->before.identity == transition->after.identity &&
           transition->before.generation != 0U &&
           transition->before.generation == transition->after.generation &&
           transition->after.now >= transition->before.now;
}

/** \brief Join the accepted loan and explicit start effects before unmasking.
 * \note Rejection retains no loan. An unfinished join preserves the mask and
 *       static objects; terminal operation failure still requires public stop.
 */
example_stop2_join_result_t example_stop2_application_join(void) {
    example_stop2_join_result_t joined = {NX_SUCCESS, NX_SUCCESS};
    if (s_enter_attempted && !s_entry.accepted) {
        s_operation_result = NX_ERROR_STATE;
    }
    if (s_entry.accepted && !s_request_joined) {
        nx_result_t result;
        size_t transferred;
        joined.cleanup =
            nx_request_result(&s_request.base, &result, &transferred);
        if (joined.cleanup != NX_SUCCESS) {
            return joined;
        }
        s_request_joined = true;
        example_stop2_lptim1_request_result = result;
        example_stop2_lptim1_transferred = transferred;
        s_operation_result =
            result == NX_SUCCESS &&
                    (transferred != sizeof(s_tuple) || !entry_observed())
                ? NX_ERROR_IO
                : result;
    }
    if (!s_receiver_stopped) {
        joined.cleanup = nx_stm32u575_lpdma_lptim1_stop(&s_receiver_face,
                                                        EXAMPLE_POLL_LIMIT);
        s_receiver_stopped = joined.cleanup == NX_SUCCESS;
    }
    joined.operation = s_operation_result;
    return joined;
}

/** \brief Boot supplies retained RTC/SRAM4; the application owns one cycle. */
int main(void) {
    if (example_stop2_lptim1_authorization != UINT32_C(0x4e584c31)) {
        return 0;
    }
    const nx_stm32u575_retained_time_config_t clock_config = {
        NX_U575_RETAINED_TIME_RTC, 60U, nx_stm32u575_stop2_identity()};
    example_stop2_lptim1_result = nx_stm32u575_retained_time_initialize(
        &s_clock, &clock_config, &s_clock_face);
    if (example_stop2_lptim1_result != NX_SUCCESS) {
        return 1;
    }
    example_stop2_lptim1_result = nx_stm32u575_stop2_initialize(1U, &s_clock);
    if (example_stop2_lptim1_result != NX_SUCCESS) {
        return close_clock(2);
    }
    const nx_stm32u575_lpdma_lptim1_config_t receiver_config = {
        &s_clock, nx_stm32u575_stop2_identity()};
    example_stop2_lptim1_result = nx_stm32u575_lpdma_lptim1_initialize(
        &s_receiver, &receiver_config, &s_receiver_face);
    if (example_stop2_lptim1_result != NX_SUCCESS) {
        return close_clock(3);
    }
    example_stop2_lptim1_result =
        nx_stm32u575_lpdma_lptim1_start(&s_receiver_face, EXAMPLE_POLL_LIMIT);
    if (example_stop2_lptim1_result != NX_SUCCESS) {
        return close_receiver(4);
    }
    nx_request_initialize(&s_request.base);
    example_stop2_lptim1_result =
        nx_stm32u575_retained_time_sample(&s_clock, &s_snapshot);
    if (example_stop2_lptim1_result != NX_SUCCESS) {
        return close_receiver(5);
    }
    example_stop2_lptim1_result = nx_stm32u575_lpdma_lptim1_request_prepare(
        &s_request, &s_snapshot, &s_tuple, &s_workspace, NX_DEADLINE_NEVER);
    if (example_stop2_lptim1_result != NX_SUCCESS) {
        return close_receiver(6);
    }
    s_application.domain = nx_stm32u575_stop2_domain();
    example_stop2_lptim1_result = nx_clock_domain_acquire(
        s_application.domain, &s_application.platform_lease);
    if (example_stop2_lptim1_result != NX_SUCCESS) {
        return close_receiver(7);
    }
    nx_platform_start_result_t started = nx_platform_start();
    s_application.first_error = started.primary != NX_SUCCESS ? started.primary
                                : started.cleanup != NX_SUCCESS
                                    ? started.cleanup
                                : started.remaining_effects != 0U ? NX_ERROR_IO
                                                                  : NX_SUCCESS;
    for (unsigned attempt = 0U; attempt < EXAMPLE_PROGRESS_LIMIT &&
                                s_application.phase != EXAMPLE_STOP2_DONE;
         ++attempt) {
        example_stop2_lptim1_result = example_stop2_progress(&s_application);
        example_stop2_lptim1_phase = s_application.phase;
        example_stop2_lptim1_restarted = s_application.restarted;
    }
    if (s_application.phase != EXAMPLE_STOP2_DONE) {
        retain();
    }
    example_stop2_lptim1_result = s_application.first_error;
    if (example_stop2_lptim1_result == NX_SUCCESS) {
        const nx_stm32u575_stop2_result_t* transition = &s_entry.transition;
        example_stop2_lptim1_elapsed_us =
            transition->after.now - transition->before.now;
    }
    return close_clock(example_stop2_lptim1_result == NX_SUCCESS ? 0 : 8);
}
