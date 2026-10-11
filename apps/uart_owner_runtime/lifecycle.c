/**
 * \file            lifecycle.c
 * \brief           Explicit external owner shutdown protocol
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-10-11
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "lifecycle.h"

nx_result_t example_owner_shutdown(example_owner_phase_t* phase,
                                   nx_bus_owner_t* owner,
                                   example_owner_shutdown_port_t port) {
    if (phase == NULL || owner == NULL || port.join_producers == NULL ||
        port.join_owner == NULL || port.quiesce_publishers == NULL ||
        port.reclaim == NULL || *phase > EXAMPLE_OWNER_PLATFORM_STOPPED) {
        return NX_ERROR_INVALID;
    }
    if (*phase == EXAMPLE_OWNER_RUNNING) {
        nx_bus_owner_stop(owner);
        *phase = EXAMPLE_OWNER_ADMISSION_CLOSED;
    }
    if (*phase == EXAMPLE_OWNER_ADMISSION_CLOSED) {
        nx_result_t result = port.join_producers(port.context);
        if (result != NX_SUCCESS) {
            return result;
        }
        *phase = EXAMPLE_OWNER_PRODUCERS_JOINED;
    }
    if (*phase == EXAMPLE_OWNER_PRODUCERS_JOINED) {
        nx_result_t result = port.join_owner(port.context);
        if (result != NX_SUCCESS) {
            return result;
        }
        if (!nx_bus_owner_idle(owner)) {
            return NX_ERROR_STATE;
        }
        *phase = EXAMPLE_OWNER_JOINED;
    }
    if (*phase == EXAMPLE_OWNER_JOINED) {
        nx_result_t result = port.quiesce_publishers(port.context);
        if (result != NX_SUCCESS) {
            return result;
        }
        *phase = EXAMPLE_OWNER_PUBLISHERS_QUIESCED;
    }
    if (*phase == EXAMPLE_OWNER_PUBLISHERS_QUIESCED) {
        nx_result_t result = port.reclaim(port.context);
        if (result != NX_SUCCESS) {
            return result;
        }
        *phase = EXAMPLE_OWNER_RECLAIMABLE;
    }
    return NX_SUCCESS;
}

nx_result_t example_owner_finish(example_owner_phase_t* phase,
                                 bool scheduler_running,
                                 nx_result_t (*platform_stop)(void*),
                                 void* context) {
    if (phase == NULL || platform_stop == NULL ||
        *phase > EXAMPLE_OWNER_PLATFORM_STOPPED) {
        return NX_ERROR_INVALID;
    }
    if (*phase < EXAMPLE_OWNER_RECLAIMABLE) {
        return NX_ERROR_STATE;
    }
    if (scheduler_running || *phase == EXAMPLE_OWNER_PLATFORM_STOPPED) {
        return NX_SUCCESS;
    }
    nx_result_t result = platform_stop(context);
    if (result == NX_SUCCESS) {
        *phase = EXAMPLE_OWNER_PLATFORM_STOPPED;
    }
    return result;
}
