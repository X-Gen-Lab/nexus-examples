/**
 * \file            flash_application.c
 * \brief           Application-owned erase/program/readback policy
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-10-30
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "flash_application.h"
#include <string.h>

/** \brief One explicit maintenance invocation, with no automatic retry. */
nx_result_t example_flash_maintenance(const nx_flash_region_t* region,
                                      bool authorized, nx_time_us_t deadline,
                                      uint8_t payload[32],
                                      example_flash_report_t* report) {
    if (region == NULL || payload == NULL || report == NULL ||
        deadline == NX_DEADLINE_NEVER) {
        return NX_ERROR_INVALID;
    }
    *report = (example_flash_report_t){0};
    if (!authorized || !region->writable) {
        return NX_ERROR_PERMISSION;
    }
    nx_result_t result = nx_flash_region_validate(region);
    if (result != NX_SUCCESS) {
        return result;
    }
    const nx_flash_geometry_t* geometry = nx_flash_port_geometry(region->port);
    uint32_t ordinal;
    nx_flash_sector_t sector;
    if (nx_flash_geometry_sector_index(geometry, region->offset, &ordinal) !=
            NX_SUCCESS ||
        nx_flash_geometry_sector_at(geometry, ordinal, &sector) != NX_SUCCESS ||
        sector.size != region->size || 32U % geometry->program_unit != 0U ||
        region->offset % geometry->program_unit != 0U || region->size < 32U) {
        return NX_ERROR_INVALID;
    }
    /* The checked geometry supplies a positive programming unit. Fill the
     * separate caller-owned RAM after admission, before any Flash operation.
     */
    memset(payload, 0, 32U);
    payload[0] = 0x4eU;
    payload[1] = 0x58U;
    payload[2] = 0x52U;
    payload[3] = 0x41U;
    payload[4] = 0x4dU;
    uint8_t observed[32];
    result = nx_flash_region_erase(region, 0U, region->size, deadline);
    if (result != NX_SUCCESS) {
        return result;
    }
    report->erased = true;
    result = nx_flash_region_program(region, 0U, payload, 32U, deadline);
    if (result != NX_SUCCESS) {
        return result;
    }
    report->programmed = true;
    result = nx_flash_region_read(region, 0U, observed, sizeof(observed));
    if (result != NX_SUCCESS) {
        return result;
    }
    if (memcmp(payload, observed, sizeof(observed)) != 0) {
        return NX_ERROR_IO;
    }
    report->verified = true;
    return NX_SUCCESS;
}
