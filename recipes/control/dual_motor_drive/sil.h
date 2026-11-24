/**
 * \file            sil.h
 * \brief           Dual PMSM experiment through the actual public drive bridge
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-24
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#ifndef EXAMPLE_DUAL_PMSM_SIL_H
#define EXAMPLE_DUAL_PMSM_SIL_H

#include "control.h"
#include "plant.h"

#ifdef __cplusplus
extern "C" {
#endif

/** \brief Explicit finite software experiment, never a real Board profile. */
typedef struct {
    example_motor_drive_config_t control[2];
    example_pmsm_config_t plant[2];
    float bus_voltage_v;
    float load_nm[2];
    uint32_t duration_us;
    uint32_t evaluation_start_us;
    uint32_t task_pause_at_us;
    uint32_t task_pause_for_us;
    uint32_t sensor_fault_at_us;
    uint32_t controlled_stop_at_us;
    uint8_t stop_axis_mask;
    uint32_t bus_limits_at_us;
    uint32_t bus_source_stop_at_us;
    float motoring_budget_w;
    float regeneration_budget_w;
    float power_bound_w_per_a[2];
} example_dual_motor_experiment_t;

/** \brief Observed software results; model truth never feeds a controller. */
typedef struct {
    double position_rad[2];
    double velocity_rad_s[2];
    double iq_a[2];
    double rms_velocity_error_rad_s[2];
    double rms_current_error_a[2];
    float measured_position_rad[2];
    nx_axis_state_t axis_state[2];
    nx_result_t fault[2];
    uint32_t adopted_generation[2];
    uint64_t completed_cycles[2];
    uint32_t evaluation_samples;
    uint32_t budget_updates;
    uint32_t motoring_samples;
    uint32_t regenerating_samples;
    uint32_t both_regenerating_samples;
    uint32_t opposed_flow_samples;
    float admitted_power_bound_w;
    float admitted_current_a[2];
    bool all_settled;
} example_dual_motor_report_t;

/** \brief Return complete, explicitly commissioned simulation-only inputs.
 * \return Two distinct PMSM/servo profiles, 20 kHz/1 kHz selected rates.
 * \note These are mathematical model parameters, never physical calibration.
 */
example_dual_motor_experiment_t example_dual_motor_reference(void);

/** \brief Run the real Axis/Drive/current/planning/session chain for two axes.
 * \param[in] experiment: Immutable finite model and application configuration.
 * \param[out] report: Distinct result, unchanged on input rejection.
 * \return SUCCESS for a completed experiment, or input/model failure.
 * \note Per-axis control faults are reported separately and all accepted
 *       sessions are joined before return. Truth is used only for sensors and
 *       result assessment. The shim is not the physical STM32F4 provider.
 */
nx_result_t
example_dual_motor_run(const example_dual_motor_experiment_t* experiment,
                       example_dual_motor_report_t* report);

#ifdef __cplusplus
}
#endif
#endif /* EXAMPLE_DUAL_PMSM_SIL_H */
