/**
 * \file            sil.h
 * \brief           Finite differential-drive software experiment
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-24
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#ifndef EXAMPLE_DIFFERENTIAL_DRIVE_SIL_H
#define EXAMPLE_DIFFERENTIAL_DRIVE_SIL_H

#include "drive.h"
#include "sil_model.h"

#ifdef __cplusplus
extern "C" {
#endif

/** \brief Explicit teaching experiment; zero optional times disable injection.
 */
typedef struct {
    example_drive_config_t control;
    example_drive_plant_config_t plant;
    float linear_m_s;
    float angular_rad_s;
    float distance_m;
    uint32_t duration_us;
    uint32_t evaluation_start_us;
    uint32_t evaluation_samples;
    uint32_t command_stop_us;
    uint32_t drop_record_at_us;
    double actual_left_radius_m;
    double actual_right_radius_m;
} example_drive_experiment_t;

/** \brief Independent measured evidence and model truth, not device results. */
typedef struct {
    uint32_t samples;
    uint32_t stationary_samples;
    float peak_error_rad_s[2];
    double rms_error_rad_s[2];
    example_drive_pose_t truth;
    example_drive_pose_t odometry;
    nx_time_us_t last_valid_received_at;
    nx_time_us_t stop_reference_at[2];
    float final_velocity_rad_s[2];
    bool data_complete;
    bool wheel_tracking_passed;
    bool stopped_window_passed;
    bool ground_endpoint_passed;
} example_drive_report_t;

/**
 * \brief Run the actual C controller against an independent finite DC plant.
 * \param[in] experiment: Complete explicit software configuration.
 * \param[out] report: Distinct result; unchanged if input or execution fails.
 * \return SUCCESS, INVALID or actual controller/model error.
 * \note No sleep, global time, files, physical I/O or shared state. The plant
 *       advances at 50 us; axes and quantized encoder observations at 1 ms.
 *       Commands update at 10 ms until command_stop_us. Current ADC codes and
 *       encoder counts feed the controller; truth feeds only this evaluator.
 *       Fixed evaluation requires consecutive complete 1-ms records; loss is
 *       reported, never filled with zero error. Thresholds are teaching values
 *       0.2/0.1 rad/s per wheel, not platform defaults or physical promises.
 */
nx_result_t example_drive_run(const example_drive_experiment_t* experiment,
                              example_drive_report_t* report);

#ifdef __cplusplus
}
#endif
#endif
