/**
 * \file            stop2_application.h
 * \brief           Application-owned RUN closure and one Stop2 transition
 * \author          Nexus Team
 */
#ifndef EXAMPLE_STOP2_APPLICATION_H
#define EXAMPLE_STOP2_APPLICATION_H

#include "nexus/arch/arch.h"
#include "nexus/components/stm32u575_stop2.h"
#ifdef __cplusplus
extern "C" {
#endif
/** \brief Explicit application phases, independent from request state. */
typedef enum {
    EXAMPLE_STOP2_CLOSE,
    EXAMPLE_STOP2_STOP,
    EXAMPLE_STOP2_RELEASE,
    EXAMPLE_STOP2_QUIESCENT,
    EXAMPLE_STOP2_ENTER,
    EXAMPLE_STOP2_RECOVER,
    EXAMPLE_STOP2_REOPEN,
    EXAMPLE_STOP2_ACQUIRE,
    EXAMPLE_STOP2_RESTART,
    EXAMPLE_STOP2_FINAL_STOP,
    EXAMPLE_STOP2_FINAL_RELEASE,
    EXAMPLE_STOP2_DONE
} example_stop2_phase_t;
/**
 * \brief Single static application owner; no copying or concurrent progress.
 * \note Initially zero with domain and its exact acquired platform_lease.
 *       The caller has initialized retained time and the Stop2 owner, enrolled
 *       every RUN consumer and called Platform start. Keep this object, clock
 *       storage and all enrolled owners alive until DONE. Preserve first_error
 *       from an initial partial Platform start; cleanup still runs.
 */
typedef struct {
    nx_clock_domain_t* domain;
    nx_clock_lease_t platform_lease;
    nx_clock_barrier_t barrier;
    nx_stm32u575_stop2_result_t transition;
    nx_arch_irq_state_t saved_mask;
    nx_result_t first_error;
    example_stop2_phase_t phase;
    bool mask_retained;
    bool restarted;
} example_stop2_application_t;
/**
 * \brief           Advance exactly one application lifecycle operation.
 * \param[in,out]   app: Static serialized state, with an acquired RUN lease.
 * \return          BUSY while progressing, an observed error, or final result.
 * \note            Caller examines phase, not return code, before reclamation.
 *                  A non-DONE error retains the phase and responsibility for
 *                  retry. No count/timeout authorizes release. ENTER executes
 *                  once; RECOVER never sleeps again. While effects remain,
 *                  PRIMASK stays saved/disabled and storage remains borrowed.
 *                  Recovery is retried at that same masked boundary. Only a
 *                  fully drained successful transition reopens RUN admission
 *                  and starts Platform with a new lease and Core epoch.
 *                  No previous Core deadline is used after the first stop.
 *                  The application bounds attempts and parks if not DONE.
 *                  Privileged MSP Thread only; no scheduler may have started.
 *                  Boot and exact source/voltage/quiet-master premises remain
 *                  those of the selected public Stop2 contract.
 */
nx_result_t example_stop2_progress(example_stop2_application_t* app);
#ifdef __cplusplus
}
#endif
#endif /* EXAMPLE_STOP2_APPLICATION_H */
