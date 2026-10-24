/**
 * \file            main.c
 * \brief           Native slow-consumer recovery entry point
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-10-24
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "stream_application.h"
#include <stdio.h>
int main(void) {
    example_stream_report_t report;
    int result = example_stream_run(&report);
    if (result == 0) {
        printf("Stream: lost=%u preserved=%u stop_waited=%u restarted=%u "
               "stopped=%u diagnostics=%u\n",
               report.lost_blocks, (unsigned)report.borrow_preserved,
               (unsigned)report.stop_waited, (unsigned)report.restarted,
               (unsigned)report.stopped, report.diagnostic_findings);
    }
    return result;
}
