/**
 * \file            lifecycle.h
 * \brief           Product-owned stop, drain, join and storage reclamation
 * \author          Nexus Team
 */
#ifndef NEXUS_EXAMPLE_OWNER_LIFECYCLE_H
#define NEXUS_EXAMPLE_OWNER_LIFECYCLE_H
#include "nexus/components/bus_owner.h"

#ifdef __cplusplus
extern "C" {
#endif

/** \brief Monotonic shutdown milestones owned by one application coordinator.
 */
typedef enum {
    EXAMPLE_OWNER_RUNNING,
    EXAMPLE_OWNER_ADMISSION_CLOSED,
    EXAMPLE_OWNER_PRODUCERS_JOINED,
    EXAMPLE_OWNER_JOINED,
    EXAMPLE_OWNER_PUBLISHERS_QUIESCED,
    EXAMPLE_OWNER_RECLAIMABLE,
    EXAMPLE_OWNER_PLATFORM_STOPPED
} example_owner_phase_t;

/**
 * \brief           Explicit product actions, performed outside owner guards
 * \note            Callbacks are task-context, bounded by product policy.
 *                  join_producers waits for admitted requests to SETTLE while
 *                  the owner still runs. join_owner stops only after drain.
 *                  quiesce_publishers detaches hardware IRQ wake and stops RX.
 *                  reclaim destroys notification/guard objects after all joins.
 *                  A failing callback retains all storage and supports retry;
 *                  each callback must track its own partial progress. No forced
 *                  cancellation, implicit task or platform reset is supplied.
 */
typedef struct {
    void* context;
    nx_result_t (*join_producers)(void* context);
    nx_result_t (*join_owner)(void* context);
    nx_result_t (*quiesce_publishers)(void* context);
    nx_result_t (*reclaim)(void* context);
} example_owner_shutdown_port_t;

/**
 * \brief           Advance the application's cooperative shutdown protocol
 * \param[in,out]   phase: Single coordinator milestone, initially RUNNING
 * \param[in,out]   owner: Live owner with a separately running executor
 * \param[in]       port: Product join/quiesce/reclaim operations
 * \return          SUCCESS when reclaimable, callback error or INVALID/STATE
 * \note            Task context. Stop closes admission and requests drain;
 *                  timeout/error never releases accepted request or bytes.
 *                  Call again only after resolving the failed action, with all
 *                  borrowed storage kept alive. Successful milestones are not
 *                  repeated. Platform stop follows this function's success.
 */
nx_result_t example_owner_shutdown(example_owner_phase_t* phase,
                                   nx_bus_owner_t* owner,
                                   example_owner_shutdown_port_t port);

/**
 * \brief           Apply explicit product policy after complete I/O quiescence
 * \param[in,out]   phase: Single coordinator milestone, at least RECLAIMABLE
 * \param[in]       scheduler_running: True retains clocks/Tick for the kernel
 * \param[in]       request_outcome: Application result of request observation
 * \param[in]       platform_stop: External platform stop action
 * \param[in]       context: Stop action context retained for this call
 * \return          Stop error in preference to request_outcome, or
 *                  STATE before quiescence or INVALID for invalid inputs
 * \note            Task context. A running FreeRTOS kernel has no maintained
 *                  scheduler-stop contract; preserve platform infrastructure.
 *                  Products need an explicit reset or complete scheduler-stop
 *                  port before disabling kernel clocks/Tick. Failure retains
 *                  phase; successful stop is never repeated. A request error
 *                  leaves the fully quiesced platform eligible for stop.
 */
nx_result_t example_owner_finish(example_owner_phase_t* phase,
                                 bool scheduler_running,
                                 nx_result_t request_outcome,
                                 nx_result_t (*platform_stop)(void*),
                                 void* context);

#ifdef __cplusplus
}
#endif
#endif /* NEXUS_EXAMPLE_OWNER_LIFECYCLE_H */
