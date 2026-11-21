/**
 * \file            update_reference.c
 * \brief           Sequential staged bytes, explicit authentication and join
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-21
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "update_reference.h"
#include <string.h>

/** \brief Reject known aliases and overflowing memory extents. */
static bool overlap(const void* a, size_t an, const void* b, size_t bn) {
    uintptr_t x = (uintptr_t)a;
    uintptr_t y = (uintptr_t)b;
    return an > UINTPTR_MAX - x || bn > UINTPTR_MAX - y ||
           (an != 0 && bn != 0 && x < y + bn && y < x + an);
}
/** \brief Latch failures without asserting that persistent effects rolled back.
 */
static nx_result_t failed(example_update_t* u, nx_result_t result) {
    u->state = EXAMPLE_UPDATE_FAILED;
    u->failure = result;
    return result;
}
/** \brief Preserve the single operation clock domain and original deadline. */
static nx_result_t time_check(example_update_t* u) {
    nx_time_us_t now = u->ports.clock.read(u->ports.clock.context);
    if (now == NX_DEADLINE_NEVER || now < u->last_time) {
        return failed(u, NX_ERROR_STATE);
    }
    u->last_time = now;
    return now >= u->deadline ? failed(u, NX_ERROR_TIMEOUT) : NX_SUCCESS;
}
/** \brief Validate product-selected staging geometry without erasing anything.
 */
nx_result_t example_update_begin(example_update_t* u,
                                 const example_update_ports_t* ports,
                                 uint32_t image_bytes, nx_time_us_t deadline) {
    if (u == NULL || ports == NULL || ports->clock.read == NULL ||
        ports->authorize == NULL || ports->request_upgrade == NULL ||
        ports->quiesce == NULL || !ports->staging.writable ||
        deadline == NX_DEADLINE_NEVER || image_bytes == 0 ||
        image_bytes > ports->staging.size ||
        overlap(u, sizeof(*u), ports, sizeof(*ports)) ||
        nx_flash_region_validate(&ports->staging) != NX_SUCCESS) {
        return NX_ERROR_INVALID;
    }
    const nx_flash_geometry_t* g = nx_flash_port_geometry(ports->staging.port);
    if (g == NULL || image_bytes % g->program_unit != 0 ||
        ports->staging.offset % g->program_unit != 0) {
        return NX_ERROR_INVALID;
    }
    nx_time_us_t now = ports->clock.read(ports->clock.context);
    if (now == NX_DEADLINE_NEVER || now >= deadline) {
        return now == NX_DEADLINE_NEVER ? NX_ERROR_STATE : NX_ERROR_TIMEOUT;
    }
    memset(u, 0, sizeof(*u));
    u->ports = *ports;
    u->deadline = deadline;
    u->last_time = now;
    u->image_bytes = image_bytes;
    u->state = EXAMPLE_UPDATE_RECEIVING;
    return NX_SUCCESS;
}
/** \brief Write only the next complete admitted program units, never replay. */
nx_result_t example_update_write(example_update_t* u, const void* bytes,
                                 size_t count) {
    if (u == NULL || bytes == NULL || count == 0 ||
        overlap(u, sizeof(*u), bytes, count)) {
        return NX_ERROR_INVALID;
    }
    if (u->active) {
        return NX_ERROR_BUSY;
    }
    if (u->state != EXAMPLE_UPDATE_RECEIVING) {
        return NX_ERROR_STATE;
    }
    const nx_flash_geometry_t* g =
        nx_flash_port_geometry(u->ports.staging.port);
    if (g == NULL || count > u->image_bytes - u->written ||
        count % g->program_unit != 0) {
        return NX_ERROR_INVALID;
    }
    u->active = true;
    nx_result_t result = time_check(u);
    if (result == NX_SUCCESS) {
        result = nx_flash_region_program(&u->ports.staging, u->written, bytes,
                                         count, u->deadline);
        if (result == NX_SUCCESS) {
            u->written += (uint32_t)count;
            result = time_check(u);
        } else {
            (void)failed(u, result);
        }
    }
    u->active = false;
    return result;
}
/** \brief Authentication is a real product port, not a downloaded-byte flag. */
nx_result_t example_update_request(example_update_t* u) {
    if (u == NULL) {
        return NX_ERROR_INVALID;
    }
    if (u->active) {
        return NX_ERROR_BUSY;
    }
    if (u->state != EXAMPLE_UPDATE_RECEIVING || u->written != u->image_bytes) {
        return NX_ERROR_STATE;
    }
    u->active = true;
    nx_result_t result = time_check(u);
    if (result == NX_SUCCESS) {
        result = u->ports.authorize(u->ports.context, &u->ports.staging,
                                    u->image_bytes, u->deadline);
    }
    if (result == NX_SUCCESS) {
        u->state = EXAMPLE_UPDATE_AUTHENTICATED;
        result = time_check(u);
    }
    if (result == NX_SUCCESS) {
        result = u->ports.request_upgrade(u->ports.context, u->deadline);
    }
    if (result == NX_SUCCESS) {
        u->state = EXAMPLE_UPDATE_REQUESTED;
        result = time_check(u);
    }
    if (result != NX_SUCCESS) {
        (void)failed(u, result);
    }
    u->active = false;
    return result;
}
/** \brief Retain all resources until explicit product quiescence succeeds. */
nx_result_t example_update_prepare_reset(example_update_t* u) {
    if (u == NULL) {
        return NX_ERROR_INVALID;
    }
    if (u->active) {
        return NX_ERROR_BUSY;
    }
    if (u->state != EXAMPLE_UPDATE_REQUESTED) {
        return NX_ERROR_STATE;
    }
    u->active = true;
    nx_result_t result = time_check(u);
    if (result == NX_SUCCESS) {
        result = u->ports.quiesce(u->ports.context, u->deadline);
        if (result == NX_SUCCESS) {
            result = time_check(u);
            if (result == NX_SUCCESS) {
                u->state = EXAMPLE_UPDATE_READY_TO_RESET;
            }
        }
    }
    u->active = false;
    return result;
}
