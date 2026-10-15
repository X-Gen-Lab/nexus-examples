/**
 * \file            quarantine_policy.c
 * \brief           Application-owned quarantine retry and retained fault stages
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-10-11
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "quarantine_policy.h"

/** \brief Reject control alias without ordering unrelated C pointers. */
static bool overlaps(const void* first, size_t first_bytes, const void* second,
                     size_t second_bytes) {
    uintptr_t a = (uintptr_t)first;
    uintptr_t b = (uintptr_t)second;
    return first_bytes > UINTPTR_MAX - a || second_bytes > UINTPTR_MAX - b ||
           (a < b + second_bytes && b < a + first_bytes);
}
/** \brief Bind only established owner facts, never initialize live owner again.
 */
nx_result_t example_owner_recovery_initialize(
    example_owner_recovery_t* recovery, nx_bus_owner_t* owner,
    uint32_t max_drain_steps, nx_time_us_t cleanup_budget_us,
    example_recovery_fault_port_t fault) {
    if (recovery == NULL || owner == NULL || owner->slots == NULL ||
        owner->capacity == 0U ||
        owner->capacity > SIZE_MAX / sizeof(*owner->slots) ||
        owner->guard.enter == NULL || owner->guard.leave == NULL ||
        owner->executor.start == NULL || owner->executor.service == NULL ||
        owner->executor.cancel == NULL || owner->clock.read == NULL ||
        fault.publish == NULL || max_drain_steps == 0U ||
        cleanup_budget_us == 0U || cleanup_budget_us == NX_DEADLINE_NEVER ||
        overlaps(recovery, sizeof(*recovery), owner, sizeof(*owner)) ||
        overlaps(recovery, sizeof(*recovery), owner->slots,
                 owner->capacity * sizeof(*owner->slots))) {
        return NX_ERROR_INVALID;
    }
    if (recovery->owner != NULL) {
        return NX_ERROR_STATE;
    }
    *recovery =
        (example_owner_recovery_t){.owner = owner,
                                   .fault = fault,
                                   .cleanup_budget_us = cleanup_budget_us,
                                   .max_drain_steps = max_drain_steps};
    return NX_SUCCESS;
}
/** \brief The fault sink observes retained ownership, never reclaims it. */
static void publish(example_owner_recovery_t* recovery,
                    example_recovery_stage_t stage, nx_result_t reason) {
    recovery->stage = stage;
    recovery->fault.publish(recovery->fault.context, stage, reason);
}
/** \brief Exactly one service step continues even after escalation. */
nx_result_t example_owner_recovery_progress(example_owner_recovery_t* recovery,
                                            nx_time_us_t now) {
    if (recovery == NULL || recovery->owner == NULL ||
        recovery->fault.publish == NULL || recovery->max_drain_steps == 0U ||
        recovery->cleanup_budget_us == 0U) {
        return NX_ERROR_INVALID;
    }
    nx_result_t result = nx_bus_owner_service(recovery->owner);
    recovery->service_result = result;
    bool reversed = recovery->observed && now < recovery->last_observed;
    bool new_fault =
        recovery->stage == EXAMPLE_RECOVERY_RUNNING &&
        ((result != NX_SUCCESS && result != NX_ERROR_BUSY) || reversed);
    bool exhausted = false;
    if (new_fault) {
        nx_bus_owner_stop(recovery->owner);
        publish(recovery, EXAMPLE_RECOVERY_FAULT_OBSERVED,
                reversed ? NX_ERROR_IO : result);
        if (recovery->cleanup_budget_us >= UINT64_MAX - now) {
            exhausted = true;
        } else {
            recovery->deadline = now + recovery->cleanup_budget_us;
        }
    }
    if (recovery->stage == EXAMPLE_RECOVERY_FAULT_OBSERVED ||
        recovery->stage == EXAMPLE_RECOVERY_ESCALATED_RETAINED) {
        if (recovery->drain_steps != UINT32_MAX) {
            ++recovery->drain_steps;
        }
        if (nx_bus_owner_idle(recovery->owner)) {
            publish(recovery, EXAMPLE_RECOVERY_LOAN_DRAINED, NX_SUCCESS);
        } else if (recovery->stage == EXAMPLE_RECOVERY_FAULT_OBSERVED) {
            nx_result_t reason = NX_SUCCESS;
            if (reversed) {
                reason = NX_ERROR_IO;
            } else if (exhausted) {
                reason = NX_ERROR_EXHAUSTED;
            } else if (now >= recovery->deadline ||
                       recovery->drain_steps >= recovery->max_drain_steps) {
                reason = NX_ERROR_TIMEOUT;
            }
            if (reason != NX_SUCCESS) {
                recovery->policy_result = reason;
                publish(recovery, EXAMPLE_RECOVERY_ESCALATED_RETAINED, reason);
            }
        }
    }
    if (!reversed) {
        recovery->last_observed = now;
        recovery->observed = true;
    }
    return result;
}
