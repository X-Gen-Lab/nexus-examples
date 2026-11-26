/**
 * \file            gateway_managed.h
 * \brief           Join managed IPv4, durable telemetry and secure sessions
 * \author          Nexus Team
 */
#ifndef NEXUS_EXAMPLE_GATEWAY_MANAGED_H
#define NEXUS_EXAMPLE_GATEWAY_MANAGED_H
#include "gateway_delivery.h"
#include "route_reference.h"

#ifdef __cplusplus
extern "C" {
#endif

/** \brief The existing task observes and applies actual PHY facts here. */
typedef nx_result_t (*example_gateway_phy_step_t)(void* context, bool* up,
                                                  nx_time_us_t deadline);

/** \brief Exact storage for one route window and one durable delivery. */
typedef struct {
    example_network_route_t route;
    example_gateway_delivery_t delivery;
    example_gateway_phy_step_t phy_step;
    void* phy_context;
    size_t hostname_bytes;
    uint32_t remaining_phy_turns;
    nx_result_t route_failure;
    bool stopping;
} example_gateway_managed_t;

/**
 * \brief Open one bounded route window in the existing serialized owner.
 * \param[in,out] managed: Zeroed or completely stopped exclusive storage.
 * \param[in] route: Disjoint explicit managed stack configuration.
 * \param[in] phy_step: Observe and apply link facts before returning them.
 * \param[in] phy_context: Stable callback context, separate from metadata.
 * \return Actual route admission status. No provider I/O runs here.
 * \note All callback and stack storage remains live through successful stop.
 *       PHY progress uses the supplied absolute deadline and has no reentry.
 */
nx_result_t
example_gateway_managed_start(example_gateway_managed_t* managed,
                              const example_network_route_config_t* route,
                              example_gateway_phy_step_t phy_step,
                              void* phy_context);

/**
 * \brief Advance the route and run one durable secure job when it is ready.
 * \param[in,out] managed: Current same-owner route and delivery metadata.
 * \param[in,out] outbox: Already loaded outbox, stable through cleanup.
 * \param[in] operation: Active, finite scope for acknowledging this outbox.
 * \param[in] gateway: Stable gateway inputs and bounded operation policy.
 * \param[out] report: Disjoint delivery observations when a job runs.
 * \return BUSY while finding a route or when a session retains cleanup;
 *         otherwise the actual route, gateway or persistence result.
 * \note The same DNS hostname supplies TLS identity and the resolved address
 *       supplies TCP. Stack, clock and operation deadline must agree. Route
 *       preparation counts BUSY PHY attempts without progressing the stack.
 *       A BUSY PHY observation during a live job closes route admission.
 *       Link or lease loss also closes admission until stop joins both
 *       levels. A new start uses a new explicit budget. Failed delivery
 *       preserves the outbox.
 */
nx_result_t
example_gateway_managed_run(example_gateway_managed_t* managed,
                            example_outbox_t* outbox,
                            const nx_flashdb_operation_t* operation,
                            const example_gateway_config_t* gateway,
                            example_gateway_delivery_report_t* report);

/**
 * \brief Close admission and join delivery before retiring DHCP and DNS.
 * \param[in,out] managed: Original storage with all synchronous users joined.
 * \param[in] operation: ACK scope for retained delivery, else may be NULL.
 * \param[in] deadline: Finite same-clock budget for local gateway cleanup.
 * \param[out] report: Disjoint observations if delivery cleanup is attempted.
 * \return SUCCESS after release or actual error. BUSY retains storage.
 * \note Repeated calls progress the original cleanup. Failed ACK persistence
 *       requires recovery before resending. Stop keeps the shared stack alive.
 */
nx_result_t example_gateway_managed_stop(
    example_gateway_managed_t* managed, const nx_flashdb_operation_t* operation,
    nx_time_us_t deadline, example_gateway_delivery_report_t* report);

#ifdef __cplusplus
}
#endif
#endif
