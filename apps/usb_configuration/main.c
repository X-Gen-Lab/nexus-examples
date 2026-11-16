/**
 * \file            main.c
 * \brief           Baremetal cooperative USB recipe; no worker or scheduler
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-05
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "nexus_bindings.h"
#include "nexus_factory_strong.h"
#include "usb_configuration.h"

static example_usb_t application;
static const uint8_t sample[8] = {0x4EU, 0x65U, 0x78U, 0x75U,
                                  0x73U, 0U,    0U,    1U};
int main(void) {
    nx_platform_start_result_t started = nx_platform_start();
    int result = 1;
    if (started.primary == NX_SUCCESS &&
        example_usb_initialize(
            &application, nx_factory_usb_profile(NX_USB_DEVICE_REF_FS.value),
            nx_factory_usb_activation(NX_USB_DEVICE_REF_FS.value)) ==
            NX_SUCCESS) {
        bool sent = false;
        while (!application.closed) {
            (void)example_usb_step(&application);
            if (application.configuration == 0U)
                sent = false;
            if (application.ready && !sent &&
                example_usb_send(&application, sample) == NX_SUCCESS)
                sent = true;
        }
        result = application.failure == NX_SUCCESS ? 0 : 1;
    }
    /* BUSY or failed cleanup retains the static application and all outer
     * leases. It never becomes permission to reclaim buffers or reset a peer.
     */
    while (nx_platform_stop() != NX_SUCCESS) {
    }
    return result;
}
