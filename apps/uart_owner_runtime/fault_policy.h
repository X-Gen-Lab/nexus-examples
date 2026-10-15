/**
 * \file            fault_policy.h
 * \brief           Explicit external product safe-output responsibility
 * \author          Nexus Team
 */
#ifndef EXAMPLE_FAULT_POLICY_H
#define EXAMPLE_FAULT_POLICY_H
#include "nexus/core/fault.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/** \brief An explicit product port; never interpreted as reset or cleanup. */
typedef struct {
    void* context;
    nx_result_t (*apply)(void*, nx_fault_domain_t, nx_result_t);
} example_safe_output_port_t;
/** \brief Application-owned first-fault storage; absent from SDK instances. */
typedef struct {
    example_safe_output_port_t outputs;
    uint32_t phase;
    nx_fault_domain_t domain;
    nx_result_t reason;
    unsigned line;
    nx_result_t output_result;
} example_fault_policy_t;
/** \brief A software observation, never physical output qualification. */
typedef struct {
    nx_fault_domain_t domain;
    nx_result_t reason;
    unsigned line;
    nx_result_t output_result;
    bool action_completed;
} example_fault_snapshot_t;
/**
 * \brief           Bind once in serialized cold application startup.
 * \param[in,out]   policy: Zeroed static application storage, never reclaimed
 *                  or reinitialized after a fault. Port/context remain stable.
 * \param[in]       outputs: Explicit bounded product action, or NULL apply for
 *                  UNPROVEN output responsibility. No GPIO route is invented.
 * \return          SUCCESS or INVALID/STATE without invoking hardware.
 */
nx_result_t example_fault_policy_initialize(example_fault_policy_t* policy,
                                            example_safe_output_port_t outputs);
/**
 * \brief           Record the first fault before the explicit product action.
 * \param[in,out]   policy: Stable product-owned record, including before main.
 * \param[in]       domain: Actual reporting path, not severity.
 * \param[in]       reason: Failure; never permission to reclaim a loan.
 * \param[in]       line: Static location; no task name/string is read.
 * \return          Product result, UNSUPPORTED without a port, BUSY on nested
 *                  reporting, STATE after the first completed action.
 * \note            Port runs in the incoming task/ISR/masked fault context.
 *                  It preserves masks, is bounded and allocation-free, and
 *                  assumes no functioning OS, clock, stack or transport.
 *                  Only separately reviewed product output actions are allowed.
 *                  No release, reset, retry or hardware safety proof follows.
 *                  A returning hook still parks in the terminal SDK caller.
 *                  Corrupted-stack handling is best effort.
 */
nx_result_t example_fault_policy_observe(example_fault_policy_t* policy,
                                         nx_fault_domain_t domain,
                                         nx_result_t reason, unsigned line);
/**
 * \brief           Read release-published diagnostic fields without OS locks.
 * \param[in]       policy: Stable record; no concurrent initialization/reclaim.
 * \param[out]      out: Separate writable storage; unchanged on failure.
 * \return          SUCCESS, EMPTY before a fault, BUSY before diagnostic
 *                  publication, or INVALID for NULL/overlap. During the product
 *                  action, output_result is BUSY and action_completed is false.
 */
nx_result_t example_fault_policy_sample(const example_fault_policy_t* policy,
                                        example_fault_snapshot_t* out);
/** \brief Main owns this stable record; the observer contains no registry. */
example_fault_policy_t* example_owner_fault_policy(void);
#ifdef __cplusplus
}
#endif
#endif
