/**
 * \file            main.c
 * \brief           Finite dual PMSM software reference and measured verdict
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-24
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "nexus_bindings.h"
#include "sil.h"
#include <stdio.h>

int main(void) {
    const example_dual_motor_experiment_t experiment =
        example_dual_motor_reference();
    example_dual_motor_report_t report;
    if (nx_platform_start().primary != NX_SUCCESS) {
        return 1;
    }
    const nx_result_t result = example_dual_motor_run(&experiment, &report);
    const nx_result_t cleanup = nx_platform_stop();
    if (result != NX_SUCCESS || cleanup != NX_SUCCESS || !report.all_settled) {
        return 2;
    }
    (void)printf(
        "software_only=1 samples=%u velocity_a=%.6f velocity_b=%.6f "
        "velocity_rms_a=%.6f velocity_rms_b=%.6f fault_a=%u fault_b=%u\n",
        report.evaluation_samples, report.velocity_rad_s[0],
        report.velocity_rad_s[1], report.rms_velocity_error_rad_s[0],
        report.rms_velocity_error_rad_s[1], (unsigned)report.fault[0],
        (unsigned)report.fault[1]);
    return report.fault[0] == NX_SUCCESS && report.fault[1] == NX_SUCCESS &&
                   report.rms_velocity_error_rad_s[0] < 0.8 &&
                   report.rms_velocity_error_rad_s[1] < 0.8
               ? 0
               : 3;
}
