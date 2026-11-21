/**
 * \file            msc_handoff.c
 * \brief           Explicit single-writer filesystem and MSC owner transitions
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-21
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "msc_handoff.h"

/** \brief Copy the five exact lifecycle boundaries without activating media. */
nx_result_t example_msc_handoff_init(example_msc_handoff_t* h,
                                     const example_msc_ports_t* ports) {
    if (h == NULL || ports == NULL || ports->local_unmount == NULL ||
        ports->local_mount == NULL || ports->usb_start == NULL ||
        ports->usb_stop == NULL || ports->media_sync == NULL ||
        ((uintptr_t)ports < (uintptr_t)h + sizeof(*h) &&
         (uintptr_t)h < (uintptr_t)ports + sizeof(*ports))) {
        return NX_ERROR_INVALID;
    }
    *h = (example_msc_handoff_t){*ports, EXAMPLE_MEDIA_LOCAL, false};
    return NX_SUCCESS;
}
/** \brief Withdraw local files before permitting any MSC block callback. */
nx_result_t example_msc_request_usb(example_msc_handoff_t* h,
                                    nx_time_us_t deadline) {
    if (h == NULL || h->ports.usb_start == NULL ||
        deadline == NX_DEADLINE_NEVER) {
        return NX_ERROR_INVALID;
    }
    if (h->active) {
        return NX_ERROR_BUSY;
    }
    if (h->owner == EXAMPLE_MEDIA_USB_CLEANUP) {
        return NX_ERROR_STATE;
    }
    if (h->owner == EXAMPLE_MEDIA_USB) {
        return NX_SUCCESS;
    }
    h->active = true;
    nx_result_t result = NX_SUCCESS;
    if (h->owner == EXAMPLE_MEDIA_LOCAL ||
        h->owner == EXAMPLE_MEDIA_LOCAL_CLEANUP) {
        result = h->ports.local_unmount(h->ports.context, deadline);
        if (result == NX_SUCCESS) {
            h->owner = EXAMPLE_MEDIA_NONE;
        }
    }
    if (result == NX_SUCCESS) {
        result = h->ports.media_sync(h->ports.context, deadline);
    }
    if (result == NX_SUCCESS) {
        h->owner = EXAMPLE_MEDIA_USB_CLEANUP;
        result = h->ports.usb_start(h->ports.context, deadline);
        if (result == NX_SUCCESS) {
            h->owner = EXAMPLE_MEDIA_USB;
        }
    }
    h->active = false;
    return result;
}
/** \brief Join the former writer before sync and a new local filesystem mount.
 */
nx_result_t example_msc_request_local(example_msc_handoff_t* h,
                                      nx_time_us_t deadline) {
    if (h == NULL || h->ports.usb_stop == NULL ||
        deadline == NX_DEADLINE_NEVER) {
        return NX_ERROR_INVALID;
    }
    if (h->active) {
        return NX_ERROR_BUSY;
    }
    if (h->owner == EXAMPLE_MEDIA_LOCAL) {
        return NX_SUCCESS;
    }
    h->active = true;
    nx_result_t result = NX_SUCCESS;
    if (h->owner == EXAMPLE_MEDIA_USB ||
        h->owner == EXAMPLE_MEDIA_USB_CLEANUP) {
        result = h->ports.usb_stop(h->ports.context, deadline);
    } else if (h->owner == EXAMPLE_MEDIA_LOCAL_CLEANUP) {
        result = h->ports.local_unmount(h->ports.context, deadline);
    }
    if (result == NX_SUCCESS) {
        h->owner = EXAMPLE_MEDIA_NONE;
        result = h->ports.media_sync(h->ports.context, deadline);
    }
    if (result == NX_SUCCESS) {
        h->owner = EXAMPLE_MEDIA_LOCAL_CLEANUP;
        result = h->ports.local_mount(h->ports.context, deadline);
        if (result == NX_SUCCESS) {
            h->owner = EXAMPLE_MEDIA_LOCAL;
        }
    }
    h->active = false;
    return result;
}
