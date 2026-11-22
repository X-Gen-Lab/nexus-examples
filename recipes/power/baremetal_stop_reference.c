/**
 * \file            baremetal_stop_reference.c
 * \brief           Explicit STOP and independent Platform restore results
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-22
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "baremetal_stop_reference.h"
#include "nexus_bindings.h"

nx_result_t example_baremetal_stop(nx_clock_barrier_t barrier,
                                   const nx_stm32_power_options_t* options,
                                   example_stop_report_t* report) {
    if (barrier.domain == NULL || barrier.epoch == 0U || options == NULL ||
        report == NULL || options->standby || options->poll_limit == 0U ||
        options->discard_wakeup || options->wake_pins != 0U)
        return NX_ERROR_INVALID;
    *report = (example_stop_report_t){.suspend = NX_ERROR_STATE,
                                      .power = NX_ERROR_STATE,
                                      .reopen = NX_ERROR_STATE,
                                      .resume = {.primary = NX_ERROR_STATE}};
    report->suspend = nx_platform_suspend();
    if (report->suspend != NX_SUCCESS)
        return report->suspend;
    report->power_attempted = true;
    report->power = nx_stm32_power_enter(barrier, options, &report->receipt);
    report->reopen = nx_clock_domain_reopen(barrier);
    if (report->reopen != NX_SUCCESS)
        return report->power != NX_SUCCESS ? report->power : report->reopen;
    report->reopened = true;
    report->resume = nx_platform_resume();
    report->resumed = report->resume.primary == NX_SUCCESS &&
                      report->resume.cleanup == NX_SUCCESS;
    if (report->power != NX_SUCCESS)
        return report->power;
    return report->resume.primary != NX_SUCCESS ? report->resume.primary
                                                : report->resume.cleanup;
}
