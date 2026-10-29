/**
 * \file            main.c
 * \brief           Opt-in H743 V Cache-off Flash/RAM maintenance application
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-10-30
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "flash_application.h"
#include "nexus_bindings.h"
#include "nexus_factory_strong.h"

static uint8_t s_payload[32];
/** \brief Debugger sets this at main only after reviewing all boot premises.
 * \note The magic is an explicit example switch, not security authorization.
 */
volatile uint32_t example_flash_authorization;
volatile nx_result_t example_flash_result = NX_ERROR_PERMISSION;
volatile example_flash_report_t example_flash_report;

/** \brief Preserve unfinished static owners without inventing recovery. */
static _Noreturn void retain(void) {
    for (;;) {
    }
}

/** \brief Stop without consulting the old Core clock after shutdown begins.
 */
static int close_platform(int result) {
    for (unsigned attempt = 0U; attempt < 64U; ++attempt) {
        if (nx_platform_stop() == NX_SUCCESS) {
            return result;
        }
    }
    retain();
}

/** \brief Reserve and touch one application-owned sector only by opt-in. */
int main(void) {
    if (example_flash_authorization != UINT32_C(0x4e58464d)) {
        return 0;
    }
    nx_platform_start_result_t started = nx_platform_start();
    if (started.primary != NX_SUCCESS || started.cleanup != NX_SUCCESS ||
        started.remaining_effects != 0U) {
        example_flash_result =
            started.primary != NX_SUCCESS ? started.primary : NX_ERROR_IO;
        return close_platform(1);
    }
    const nx_flash_port_t* flash = nx_factory_flash_ref(NX_FLASH_REF_STORAGE);
    const nx_flash_geometry_t* geometry = nx_flash_port_geometry(flash);
    const uintptr_t payload_address = (uintptr_t)s_payload;
    if (geometry == NULL || geometry->base_address != UINT32_C(0x08000000) ||
        geometry->size != UINT32_C(0x00200000) ||
        geometry->program_unit != 32U ||
        payload_address < UINT32_C(0x24000000) ||
        payload_address > UINT32_C(0x24080000) - sizeof(s_payload)) {
        example_flash_result = NX_ERROR_UNSUPPORTED;
        return close_platform(2);
    }
    /* This recipe owns the last 128 KiB sector. It is not a platform partition.
     * Product boot, image placement and every other master must exclude it.
     * The provider additionally rejects overlap with its actual linked image.
     */
    const nx_flash_region_t region = {flash, UINT32_C(0x001e0000),
                                      UINT32_C(0x00020000), true};
    example_flash_report_t report = {0};
    example_flash_result = example_flash_maintenance(
        &region, true, nx_deadline_after(nx_time_now_us(), 2000000U), s_payload,
        &report);
    example_flash_report = report;
    return close_platform(example_flash_result == NX_SUCCESS ? 0 : 3);
}
