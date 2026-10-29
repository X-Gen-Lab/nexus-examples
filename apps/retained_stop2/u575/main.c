/**
 * \file            main.c
 * \brief           Opt-in U575 retained-time and one WUT Stop2 application
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-10-30
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "nexus_bindings.h"
#include "stop2_application.h"

static nx_stm32u575_retained_time_storage_t s_clock;
static nx_clock_t s_clock_face;
static example_stop2_application_t s_application;

/** \brief Explicit opt-in after prepared RTC, electrical and boot review.
 * \note Set 0x4e585332 at a breakpoint in main. This is not a security token.
 */
volatile uint32_t example_stop2_authorization;
volatile nx_result_t example_stop2_result = NX_ERROR_PERMISSION;
volatile example_stop2_phase_t example_stop2_phase;
volatile bool example_stop2_restarted;
volatile nx_time_us_t example_retained_elapsed_us;

/** \brief Preserve the same owners, exact lease, mask and storage on failure.
 */
static _Noreturn void retain(void) {
    for (;;) {
    }
}

/** \brief Join the independent clock only after all RUN/wake effects drain.
 */
static int close_clock(int result) {
    if (nx_stm32u575_retained_time_stop(&s_clock) != NX_SUCCESS) {
        retain();
    }
    return result;
}

/** \brief External boot supplies the RTC source; application owns the cycle.
 */
int main(void) {
    if (example_stop2_authorization != UINT32_C(0x4e585332)) {
        return 0;
    }
    const nx_stm32u575_retained_time_config_t config = {
        NX_U575_RETAINED_TIME_RTC, 60U, nx_stm32u575_stop2_identity()};
    example_stop2_result =
        nx_stm32u575_retained_time_initialize(&s_clock, &config, &s_clock_face);
    if (example_stop2_result != NX_SUCCESS) {
        return 1;
    }
    example_stop2_result = nx_stm32u575_stop2_initialize(1U, &s_clock);
    if (example_stop2_result != NX_SUCCESS) {
        return close_clock(2);
    }
    s_application.domain = nx_stm32u575_stop2_domain();
    example_stop2_result = nx_clock_domain_acquire(
        s_application.domain, &s_application.platform_lease);
    if (example_stop2_result != NX_SUCCESS) {
        return close_clock(3);
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
        example_stop2_result = example_stop2_progress(&s_application);
        example_stop2_phase = s_application.phase;
        example_stop2_restarted = s_application.restarted;
    }
    if (s_application.phase != EXAMPLE_STOP2_DONE) {
        retain();
    }
    example_stop2_result = s_application.first_error;
    if (example_stop2_result == NX_SUCCESS) {
        const nx_stm32u575_stop2_result_t* transition =
            &s_application.transition;
        if (!transition->wait_executed || !transition->wake_flag ||
            transition->before.identity != transition->after.identity ||
            transition->before.generation != transition->after.generation ||
            transition->after.now < transition->before.now) {
            example_stop2_result = NX_ERROR_IO;
        } else {
            example_retained_elapsed_us =
                transition->after.now - transition->before.now;
        }
    }
    return close_clock(example_stop2_result == NX_SUCCESS ? 0 : 4);
}
