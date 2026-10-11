/**
 * \file            quarantine_policy.h
 * \brief           External retained-loan escalation policy
 * \author          Nexus Team
 */
#ifndef EXAMPLE_QUARANTINE_POLICY_H
#define EXAMPLE_QUARANTINE_POLICY_H
#include "nexus/components/bus_owner.h"
#ifdef __cplusplus
extern "C" {
#endif
/** \brief Application fault stages, not provider settlement or safety outputs.
 */
typedef enum {
    EXAMPLE_RECOVERY_RUNNING,
    EXAMPLE_RECOVERY_FAULT_OBSERVED,
    EXAMPLE_RECOVERY_ESCALATED_RETAINED,
    EXAMPLE_RECOVERY_LOAN_DRAINED
} example_recovery_stage_t;
/**
 * \brief           Explicit caller fault sink, invoked outside metadata guard.
 * \note            It must be bounded and never destroy/reset retained storage,
 *                  reenter this executor, stop its worker or reclaim a loan.
 *                  Product-specific safe outputs belong to the external
 * product.
 */
typedef struct {
    void* context;
    void (*publish)(void*, example_recovery_stage_t, nx_result_t);
} example_recovery_fault_port_t;
/** \brief Caller-owned application policy, distinct from SDK owner storage. */
typedef struct {
    nx_bus_owner_t* owner;
    example_recovery_fault_port_t fault;
    nx_time_us_t cleanup_budget_us;
    nx_time_us_t deadline;
    nx_time_us_t last_observed;
    uint32_t max_drain_steps;
    uint32_t drain_steps;
    nx_result_t service_result;
    nx_result_t policy_result;
    example_recovery_stage_t stage;
    bool observed;
} example_owner_recovery_t;
/**
 * \brief           Bind a real owner and explicit escalation budget.
 * \param[in,out]   recovery: Fresh zero storage retained through shutdown.
 * \param[in,out]   owner: Stable initialized owner, no concurrent lifecycle.
 * \param[in]       max_drain_steps: Positive post-fault service-step budget.
 * \param[in]       cleanup_budget_us: Positive finite escalation time budget.
 * \param[in]       fault: Mandatory bounded observable fault sink.
 * \return          SUCCESS or INVALID/STATE, without service/hardware action.
 */
nx_result_t example_owner_recovery_initialize(
    example_owner_recovery_t* recovery, nx_bus_owner_t* owner,
    uint32_t max_drain_steps, nx_time_us_t cleanup_budget_us,
    example_recovery_fault_port_t fault);
/**
 * \brief           Service once, preserving loans after escalation.
 * \param[in,out]   recovery: Unique executor, no concurrent policy mutation.
 * \param[in]       now: Monotonic observation in the owner deadline domain.
 * \return          Actual service result, with a separate policy_result.
 * \note            BUSY is normal waiting. Errors close admission and publish
 *                  a fault. Clock reversal and time/step exhaustion escalate.
 *                  Every later call still services retained work. No reset,
 *                  task cancellation, replay or implicit reclamation occurs.
 *                  LOAN_DRAINED proves only owner idle. Producer join, IRQ
 *                  detach, hardware stop and observer join still precede reuse.
 */
nx_result_t example_owner_recovery_progress(example_owner_recovery_t* recovery,
                                            nx_time_us_t now);
#ifdef __cplusplus
}
#endif
#endif
