/**
 * \file            servo_sil.h
 * \brief           Finite loaded single-axis experiment with isolated truth
 * \author          Nexus Team
 */
#ifndef EXAMPLE_SERVO_SIL_H
#define EXAMPLE_SERVO_SIL_H
#include "servo_control.h"
#include "sil_model.h"
#ifdef __cplusplus
extern "C" {
#endif
/** \brief Complete teaching experiment; zero optional times disable actions. */
typedef struct {
    example_servo_config_t control;
    example_drive_plant_config_t plant;
    nx_axis_mode_t mode;
    float position_rad;
    float velocity_rad_s;
    float current_a;
    uint32_t duration_us;
    uint32_t valid_until_us;
    uint32_t evaluation_start_us;
    uint32_t evaluation_samples;
    uint32_t load_at_us;
    double load_nm;
    uint32_t parameters_at_us;
    nx_axis_parameters_t candidate_parameters;
    uint32_t derating_at_us;
    float derated_current_limit_a;
    uint32_t stop_at_us;
    uint32_t drop_record_at_us;
    uint32_t sensor_fault_at_us;
} example_servo_experiment_t;
/** \brief Model evaluation only; no truth enters the controller interface. */
typedef struct {
    uint32_t samples;
    uint32_t limited_samples;
    uint32_t parameter_revision;
    nx_time_us_t adopted_at;
    nx_time_us_t trajectory_complete_at;
    nx_time_us_t measured_reached_at;
    nx_time_us_t stop_adopted_at;
    nx_time_us_t model_stopped_at;
    float peak_position_error_rad;
    float peak_velocity_error_rad_s;
    float peak_current_error_a;
    double rms_position_error_rad;
    double rms_velocity_error_rad_s;
    double rms_current_error_a;
    float measured_position_rad;
    float measured_velocity_rad_s;
    double truth_position_rad;
    double truth_velocity_rad_s;
    double truth_current_a;
    nx_axis_state_t final_state;
    nx_result_t axis_fault;
    bool command_expired;
    bool target_reached;
    bool data_complete;
    bool tracking_passed;
} example_servo_report_t;
/**
 * \brief Return the one explicit teaching reference, independent of any Board.
 * \return Value containing SI constants and fixed evaluation requirements.
 * \note The reference is not a product default, tuning recipe or physical test.
 */
example_servo_experiment_t example_servo_reference(void);
/**
 * \brief Execute a finite real-C cascade against the shared independent plant.
 * \param[in] experiment: Explicit finite model and control configuration.
 * \param[out] report: Distinct complete result, unchanged on any error.
 * \return SUCCESS or exact controller/model error; no physical actuation.
 * \note Plant/current run at 50 us, quantized encoder/axis at 1 ms. One finite
 *       command is admitted once. Position dwell comes from real nx_axis_step,
 *       not trajectory completion. Statistics use the fixed 1-ms window;
 *       missing records remain missing. Model truth is evaluated separately.
 *       Velocity compares the observed interval with its previous-cycle
 *       reference. Instant ADC current compares with the target active before
 *       that boundary; a newly calculated target only applies afterward.
 */
nx_result_t example_servo_run(const example_servo_experiment_t* experiment,
                              example_servo_report_t* report);
#ifdef __cplusplus
}
#endif
#endif
