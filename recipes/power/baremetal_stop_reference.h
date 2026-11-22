/**
 * \file            baremetal_stop_reference.h
 * \brief           Explicit F4 baremetal Platform STOP and resume composition
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-22
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#ifndef EXAMPLE_POWER_BAREMETAL_STOP_REFERENCE_H
#define EXAMPLE_POWER_BAREMETAL_STOP_REFERENCE_H
#include "nexus/core/platform_lifecycle.h"
#include "nexus/soc/stm32f4_power.h"
#ifdef __cplusplus
extern "C" {
#endif

/** \brief Independent primary, restore and ownership facts for one attempt.
 * \note Fields are observations, not permission to reclaim retained owners.
 *       A failed suspend/reopen/resume leaves application admission closed.
 */
typedef struct {
    nx_result_t suspend;
    nx_result_t power;
    nx_result_t reopen;
    nx_platform_start_result_t resume;
    nx_stm32_stop_receipt_t receipt;
    bool power_attempted;
    bool reopened;
    bool resumed;
} example_stop_report_t;

/**
 * \brief Suspend selected infrastructure, attempt STOP, then rebuild owners.
 * \param[in] barrier: Exact closed clock admission control from this domain.
 * \param[in] options: STOP-only reviewed wake plan with positive poll limit.
 * \param[out] report: Separate call-owned output, unchanged on invalid input.
 * \return SUCCESS only after STOP and complete resume, otherwise original
 *         power error or the first suspend/reopen/resume failure.
 * \note Privileged unmasked baremetal Thread only; use the actual selected
 *       F4 System/Platform and its public SoC header target requirements.
 *       Before calling, stop new application work, join every borrower and
 *       close clock admission. Platform suspend releases enrolled hardware;
 *       power_enter verifies the exact barrier is drained before changing
 *       clocks. A returned power rejection still allows an explicit attempt to
 *       reopen/resume, whose independent errors remain in report. Unrecoverable
 *       System restore parks inside the provider and never reaches this code.
 *       Reopen precedes resume so rebuilt consumers can acquire fresh leases.
 *       This does not restart tasks, network sessions, requests or old deadline
 *       values. Use a successful receipt and independently qualified RTC bounds
 *       with nx_stm32_deadline_bridge when required. No kernel tick bridging,
 *       Standby, event acknowledgment, hidden wait or automatic retry is added.
 *       Report, options and opaque owner storage have disjoint live extents.
 */
nx_result_t example_baremetal_stop(nx_clock_barrier_t barrier,
                                   const nx_stm32_power_options_t* options,
                                   example_stop_report_t* report);

#ifdef __cplusplus
}
#endif
#endif /* EXAMPLE_POWER_BAREMETAL_STOP_REFERENCE_H */
