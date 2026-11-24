/**
 * \file            main.c
 * \brief           Finite two-metre DC software experiment and verdict
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-24
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "nexus_bindings.h"
#include "sil.h"
#include <stdio.h>

int main(void) {
    const example_drive_experiment_t experiment = {
        .control = {.radius_m = 0.1F,
                    .track_m = 0.4F,
                    .wheel_limit_rad_s = 10,
                    .command_lifetime_us = 30000,
                    .axis = {1, 1, 2000, 2000, NX_AXIS_INHIBIT_AFTER_DWELL},
                    .parameters = {1, 0.8F, 8, 20, 8, 2, 8, 10, 100, -1000,
                                   1000, 0.01F, 0.05F, 10000, 0},
                    .current_pi = {2, 2000, 0, 0, -24, 24, -24, 24}},
        .plant = {2, 0.002, 0.1, 0.1, 0.005, 0.01, 24, 1048576, 0.001, 32768},
        .linear_m_s = 0.5F,
        .distance_m = 2,
        .duration_us = 6001000,
        .evaluation_start_us = 2000000,
        .evaluation_samples = 1000,
        .actual_left_radius_m = 0.1,
        .actual_right_radius_m = 0.1};
    example_drive_report_t report;
    if (nx_platform_start().primary != NX_SUCCESS) {
        return 1;
    }
    const nx_result_t result = example_drive_run(&experiment, &report);
    const nx_result_t cleanup = nx_platform_stop();
    if (result != NX_SUCCESS || cleanup != NX_SUCCESS) {
        return 1;
    }
    (void)printf("software_only samples=%u complete=%u "
                 "left_peak=%.6f right_peak=%.6f "
                 "left_rms=%.6f right_rms=%.6f "
                 "truth_x=%.6f odom_x=%.6f stopped=%u endpoint=%u\n",
                 report.samples, (unsigned)report.data_complete,
                 (double)report.peak_error_rad_s[0],
                 (double)report.peak_error_rad_s[1], report.rms_error_rad_s[0],
                 report.rms_error_rad_s[1], report.truth.x_m,
                 report.odometry.x_m, (unsigned)report.stopped_window_passed,
                 (unsigned)report.ground_endpoint_passed);
    return report.wheel_tracking_passed && report.ground_endpoint_passed ? 0
                                                                         : 1;
}
