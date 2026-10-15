/**
 * \file            bootstrap.c
 * \brief           Explicit partial startup rollback protocol
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-10-11
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "bootstrap.h"

nx_result_t example_owner_bootstrap_abort(example_owner_bootstrap_t* effects,
                                          nx_bus_owner_t* owner,
                                          example_owner_bootstrap_port_t port) {
    if (effects == NULL || port.retire_workers == NULL ||
        port.quiesce_publishers == NULL || port.destroy_notification == NULL ||
        port.stop_platform == NULL ||
        effects->execution > EXAMPLE_WORKERS_FREERTOS_UNSTARTED ||
        (effects->owner_initialized && owner == NULL) ||
        (effects->workers != 0U &&
         (!effects->owner_initialized ||
          effects->execution == EXAMPLE_WORKERS_INLINE))) {
        return NX_ERROR_INVALID;
    }
    if (effects->scheduler_started) {
        return NX_ERROR_CONTEXT;
    }
    if (effects->owner_initialized) {
        nx_bus_owner_stop(owner);
    }
    if (effects->workers != 0U) {
        nx_result_t result = port.retire_workers(
            port.context, effects->execution, effects->workers);
        if (result != NX_SUCCESS) {
            return result;
        }
        effects->workers = 0U;
    }
    if (effects->owner_initialized && !nx_bus_owner_idle(owner)) {
        return NX_ERROR_STATE;
    }
    if (effects->publishers_live) {
        nx_result_t result = port.quiesce_publishers(port.context);
        if (result != NX_SUCCESS) {
            return result;
        }
        effects->publishers_live = false;
    }
    while (effects->notifications != 0U) {
        nx_result_t result = port.destroy_notification(
            port.context, effects->notifications - 1U);
        if (result != NX_SUCCESS) {
            return result;
        }
        --effects->notifications;
    }
    if (effects->platform_live) {
        nx_result_t result = port.stop_platform(port.context);
        if (result != NX_SUCCESS) {
            return result;
        }
        effects->platform_live = false;
    }
    return NX_SUCCESS;
}
