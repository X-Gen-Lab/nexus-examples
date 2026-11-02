/**
 * \file            main.c
 * \brief           Product-owned file model and finite industrial entry
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-02
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "industrial_application.h"
#include "nexus/components/file_flash.h"
#include <inttypes.h>
#include <stdio.h>

int main(int argc, char** argv) {
    if (argc != 2) {
        fputs("Usage: industrial_control <dedicated-model-file>\n", stderr);
        return 1;
    }
    nx_file_flash_t file;
    nx_storage_t storage;
    uint8_t workspace[NX_STORAGE_HEADER_SIZE];
    if (nx_file_flash_open(&file, argv[1], 1024U, 256U, 4U) != NX_STORAGE_OK) {
        return 2;
    }
    if (nx_storage_open(&storage, &file.port, 0U, 512U, workspace,
                        sizeof(workspace)) != NX_STORAGE_OK) {
        nx_file_flash_close(&file);
        return 3;
    }
    const example_industrial_config_t config = {100000U, 3U, 30000U, 500000U,
                                                1000000U};
    example_industrial_report_t report;
    nx_result_t result = example_industrial_run(&storage, &config, &report);
    nx_file_flash_close(&file);
    if (result != NX_SUCCESS) {
        fprintf(
            stderr,
            "Industrial failure: result=%u storage=%u "
            "retained=%u startup=%u/%u/%" PRIu32 "\n",
            (unsigned)result, (unsigned)report.storage_status,
            (unsigned)report.storage_retained, (unsigned)report.startup_primary,
            (unsigned)report.startup_cleanup, report.startup_remaining_effects);
        return 4;
    }
    printf("Native industrial: samples=%" PRIu32 " temperature=%" PRId32
           " pressure=%" PRIu32 " durable_period_us=%" PRIu32
           " joined=%u stopped=%u; physical HIL not executed\n",
           report.samples, report.sample.centi_celsius,
           report.sample.pressure_pa, report.durable_period_us,
           (unsigned)report.joined, (unsigned)report.platform_stopped);
    return report.joined && report.platform_stopped && !report.watchdog_expired
               ? 0
               : 5;
}
