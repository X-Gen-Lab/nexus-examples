/**
 * \file            fault_policy.c
 * \brief           Caller-owned product fault latch and explicit output port
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-10-11
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "fault_policy.h"
#include "nexus/arch/atomic.h"

enum { FAULT_READY, FAULT_CLAIMED, FAULT_DIAGNOSTIC, FAULT_COMPLETE };

/** \brief A cold binding is immutable once the first fault has been claimed. */
nx_result_t
example_fault_policy_initialize(example_fault_policy_t* policy,
                                example_safe_output_port_t outputs) {
    if (policy == NULL) {
        return NX_ERROR_INVALID;
    }
    if (nx_atomic_u32_load_acquire(&policy->phase) != FAULT_READY) {
        return NX_ERROR_STATE;
    }
    policy->outputs = outputs;
    return NX_SUCCESS;
}

/** \brief One first-fault claim bounds recursion without OS locks or a worker.
 */
nx_result_t example_fault_policy_observe(example_fault_policy_t* policy,
                                         nx_fault_domain_t domain,
                                         nx_result_t reason, unsigned line) {
    if (policy == NULL) {
        return NX_ERROR_INVALID;
    }
    uint32_t expected = FAULT_READY;
    if (!nx_atomic_u32_compare_exchange_acq_rel(&policy->phase, &expected,
                                                FAULT_CLAIMED)) {
        return expected == FAULT_COMPLETE ? NX_ERROR_STATE : NX_ERROR_BUSY;
    }
    policy->domain = domain;
    policy->reason = reason;
    policy->line = line;
    policy->output_result = NX_ERROR_UNSUPPORTED;
    nx_atomic_u32_store_release(&policy->phase, FAULT_DIAGNOSTIC);
    nx_result_t result =
        policy->outputs.apply == NULL
            ? NX_ERROR_UNSUPPORTED
            : policy->outputs.apply(policy->outputs.context, domain, reason);
    policy->output_result = result;
    nx_atomic_u32_store_release(&policy->phase, FAULT_COMPLETE);
    return result;
}

/** \brief Diagnostic fields never change after their release publication. */
nx_result_t example_fault_policy_sample(const example_fault_policy_t* policy,
                                        example_fault_snapshot_t* out) {
    if (policy == NULL || out == NULL) {
        return NX_ERROR_INVALID;
    }
    uintptr_t first = (uintptr_t)policy;
    uintptr_t second = (uintptr_t)out;
    if (first > UINTPTR_MAX - sizeof(*policy) ||
        second > UINTPTR_MAX - sizeof(*out) ||
        (first < second + sizeof(*out) && second < first + sizeof(*policy))) {
        return NX_ERROR_INVALID;
    }
    uint32_t phase = nx_atomic_u32_load_acquire(&policy->phase);
    if (phase < FAULT_DIAGNOSTIC) {
        return phase == FAULT_READY ? NX_ERROR_EMPTY : NX_ERROR_BUSY;
    }
    if (phase > FAULT_COMPLETE) {
        return NX_ERROR_STATE;
    }
    /* A concurrent reader of DIAGNOSTIC never races the later result store. */
    *out = (example_fault_snapshot_t){
        policy->domain, policy->reason, policy->line,
        phase == FAULT_COMPLETE ? policy->output_result : NX_ERROR_BUSY,
        phase == FAULT_COMPLETE};
    return NX_SUCCESS;
}
