/**
 * \file            main.c
 * \brief           Two-device SPI application entry with explicit ownership
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-10-24
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "nexus_config.h"
#include "shared_spi_application.h"
#if defined(NEXUS_BACKEND_NATIVE)
#include <stdio.h>
#endif

/** \brief Report finite host behavior; MCU scheduler owns continued life. */
int main(void) {
    example_spi_report_t report = {0};
    int result = example_shared_spi_run(&report);
#if defined(NEXUS_BACKEND_NATIVE)
    (void)printf("SPI owner: completed=%zu cancelled=%zu rejected=%zu "
                 "joined=%u stopped=%u\n",
                 report.completed, report.cancelled, report.rejected,
                 (unsigned)report.joined, (unsigned)report.stopped);
#endif
    return result;
}
