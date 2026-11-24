/**
 * \file            main.c
 * \brief           Finite loaded position cascade and measured software verdict
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-24
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "nexus_bindings.h"
#include "servo_sil.h"
#include <stdio.h>

int main(void) {
    const example_servo_experiment_t experiment = example_servo_reference();
    example_servo_report_t report;
    if (nx_platform_start().primary != NX_SUCCESS) {
        return 1;
    }
    const nx_result_t result = example_servo_run(&experiment, &report);
    const nx_result_t cleanup = nx_platform_stop();
    if (result != NX_SUCCESS || cleanup != NX_SUCCESS) {
        return 1;
    }
    (void)printf(
        "software_only samples=%u complete=%u tracking=%u reached=%u "
        "revision=%u adopted_us=%llu trajectory_us=%llu reached_us=%llu "
        "position_peak=%.6f velocity_peak=%.6f current_peak=%.6f "
        "position_rms=%.6f velocity_rms=%.6f current_rms=%.6f "
        "measured_position=%.6f truth_position=%.6f "
        "truth_velocity=%.6f truth_current=%.6f\n",
        report.samples, (unsigned)report.data_complete,
        (unsigned)report.tracking_passed, (unsigned)report.target_reached,
        report.parameter_revision, (unsigned long long)report.adopted_at,
        (unsigned long long)report.trajectory_complete_at,
        (unsigned long long)report.measured_reached_at,
        (double)report.peak_position_error_rad,
        (double)report.peak_velocity_error_rad_s,
        (double)report.peak_current_error_a, report.rms_position_error_rad,
        report.rms_velocity_error_rad_s, report.rms_current_error_a,
        (double)report.measured_position_rad, report.truth_position_rad,
        report.truth_velocity_rad_s, report.truth_current_a);
    return report.data_complete && report.tracking_passed &&
                   report.target_reached
               ? 0
               : 1;
}
