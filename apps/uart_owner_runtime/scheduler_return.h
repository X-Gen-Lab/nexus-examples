/**
 * \file            scheduler_return.h
 * \brief           Terminal policy after a scheduler-start attempt returns
 * \author          Nexus Team
 */
#ifndef NEXUS_EXAMPLE_SCHEDULER_RETURN_H
#define NEXUS_EXAMPLE_SCHEDULER_RETURN_H

#include "bootstrap.h"

#ifdef __cplusplus
extern "C" {
#endif

/** \brief Explicit application startup stage for an unexpected return. */
#define EXAMPLE_OWNER_SCHEDULER_RETURNED 8U

/**
 * \brief           Product-owned terminal diagnostic/reset action
 * \note            fail_stop never returns and must not reclaim callbacks,
 *                  task/TCB/stack storage, notifications or platform effects.
 *                  Context remains live through the terminal action. The
 *                  example uses the public fail-stop hook, not an automatic
 *                  reset or a purported complete scheduler teardown.
 */
typedef struct {
    void* context;
    void (*fail_stop)(void*);
} example_owner_fail_stop_port_t;

/**
 * \brief           Publish failure and retain every scheduler-start effect
 * \param[in,out]   effects: Live startup record; scheduler guard stays true
 * \param[out]      status: Naturally aligned live atomic status word
 * \param[out]      stage: Naturally aligned live atomic startup-stage word
 * \param[in]       port: Terminal product diagnostic/reset sink
 * \note            All pointers/methods are valid and caller-owned. Called only
 *                  after a scheduler-start attempt unexpectedly returns.
 *                  Publish stage before the IO failure status, then call the
 *                  terminal sink. Never infer never-started tasks from the
 *                  current kernel state, call prestart discard, stop clocks,
 *                  replay creation or return to main's runtime. If the sink
 *                  violates its contract by returning, retain storage forever.
 */
#ifdef __cplusplus
[[noreturn]]
#else
_Noreturn
#endif
void example_owner_scheduler_returned(example_owner_bootstrap_t* effects,
                                      uint32_t* status, uint32_t* stage,
                                      example_owner_fail_stop_port_t port);

#ifdef __cplusplus
}
#endif
#endif /* NEXUS_EXAMPLE_SCHEDULER_RETURN_H */
