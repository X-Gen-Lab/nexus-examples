/**
 * \file            bootstrap.h
 * \brief           Application-owned partial startup rollback
 * \author          Nexus Team
 */
#ifndef NEXUS_EXAMPLE_OWNER_BOOTSTRAP_H
#define NEXUS_EXAMPLE_OWNER_BOOTSTRAP_H
#include "nexus/components/bus_owner.h"

#ifdef __cplusplus
extern "C" {
#endif

/** \brief Worker execution facts; creation is not scheduler startup. */
typedef enum {
    EXAMPLE_WORKERS_INLINE,
    EXAMPLE_WORKERS_NATIVE_RUNNING,
    EXAMPLE_WORKERS_KERNEL_UNSTARTED
} example_owner_workers_t;

/**
 * \brief           Startup effects and attempted controls owned by a
 * coordinator \note            Notifications record successful creation-order
 * indices. Workers include each candidate before its start attempt; failure may
 * still retain its control and context. Only a successful withdrawal releases
 * that candidate. Retain this record, all objects and borrowed bytes after any
 * rollback failure. Native workers may already have submitted requests;
 * unstarted kernel tasks have never executed. scheduler_started marks the
 * scheduler-start attempt, not a proved current kernel state. Once attempted,
 * never use prestart rollback: running tasks use cooperative shutdown; an
 * unexpected scheduler return retains effects through the terminal
 * scheduler_return policy.
 */
typedef struct {
    size_t notifications;
    size_t workers;
    bool owner_initialized;
    bool publishers_live;
    bool platform_live;
    bool scheduler_started;
    example_owner_workers_t execution;
} example_owner_bootstrap_t;

/**
 * \brief           External rollback actions; no implicit task or reset
 * \note            retire_workers joins actual Native threads while the owner
 *                  still drains, or discards never-executed kernel tasks
 * before scheduler startup. It tracks partial progress and does not treat a
 * rejected producer submission as a retained borrow. quiesce_publishers
 * detaches IRQ sinks and stops selected I/O. destroy_notification accepts a
 * creation-order index. Each callback preserves storage on failure and permits
 * retry.
 */
typedef struct {
    void* context;
    nx_result_t (*retire_workers)(void*, example_owner_workers_t, size_t);
    nx_result_t (*quiesce_publishers)(void*);
    nx_result_t (*destroy_notification)(void*, size_t);
    nx_result_t (*stop_platform)(void*);
} example_owner_bootstrap_port_t;

/**
 * \brief           Roll back the recorded partial startup effects
 * \param[in,out]   effects: Single coordinator's successful effects
 * \param[in,out]   owner: Owner storage; read only if initialized
 * \param[in]       port: External task, I/O, notification and platform actions
 * \return          SUCCESS only after all recorded effects are reclaimed;
 *                  CONTEXT after scheduler startup, INVALID or callback error
 * \note            Startup/coordinator context. Close admission before retiring
 *                  Native threads, then verify drain before detaching
 * publishers. Destroy notifications in reverse creation order. Failure keeps
 *                  unfinished effects and all affected storage; retry cannot
 *                  repeat successful destruction or stop. This is an external
 *                  application policy, not a platform lifecycle abstraction.
 */
nx_result_t example_owner_bootstrap_abort(example_owner_bootstrap_t* effects,
                                          nx_bus_owner_t* owner,
                                          example_owner_bootstrap_port_t port);

#ifdef __cplusplus
}
#endif
#endif /* NEXUS_EXAMPLE_OWNER_BOOTSTRAP_H */
