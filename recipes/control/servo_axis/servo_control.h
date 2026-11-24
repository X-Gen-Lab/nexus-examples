/**
 * \file            servo_control.h
 * \brief           Caller-owned axis and scalar DC current-loop composition
 * \author          Nexus Team
 */
#ifndef EXAMPLE_SERVO_CONTROL_H
#define EXAMPLE_SERVO_CONTROL_H
#include "nexus/algorithms/pid.h"
#include "nexus/control/axis.h"
#ifdef __cplusplus
extern "C" {
#endif
/** \brief Explicit controller and sensor calibration, no model truth. */
typedef struct {
    nx_axis_config_t axis;
    nx_axis_parameters_t parameters;
    nx_pid_config_t current_pi;
    uint32_t encoder_counts_per_turn;
    float current_a_per_code;
    uint16_t current_zero_code;
} example_servo_config_t;
/**
 * \brief One executor owns the public axis and independent current PI.
 * \note Use the public nx_axis_* calls on axis under this same executor.
 *       This application owns both objects; no private provider is accessed.
 *       The current path sees only calibrated current and the axis decision.
 */
typedef struct {
    nx_axis_t axis;
    nx_pid_t current_pi;
    nx_result_t current_fault;
    bool initialized;
} example_servo_control_t;
/**
 * \brief Initialize fresh application algorithms without touching devices.
 * \param[out] control: Fresh caller storage disjoint from config.
 * \param[in] config: Explicit finite control/sensor constants and PI limits.
 * \return SUCCESS or exact validation failure; rejection preserves control.
 * \note Single Thread executor. No clock, model, allocation, worker or I/O.
 */
nx_result_t
example_servo_control_initialize(example_servo_control_t* control,
                                 const example_servo_config_t* config);
/**
 * \brief Calculate one DC voltage plan from an actual axis current reference.
 * \param[in,out] control: Initialized serialized algorithms and current state.
 * \param[in] current_a: Copied calibrated current, never requested current.
 * \param[in] valid: True only for a valid unsaturated ADC-derived sample.
 * \param[in] dt_s: Explicit positive current-loop sample period, in seconds.
 * \param[out] voltage_v: Separate voltage plan, unchanged on failure.
 * \return SUCCESS or latched input/numeric fault. Fault is not physical stop.
 * \note READY/DISABLED/FAULT produces a zero model-voltage plan; HOLDING keeps
 *       the current PI active. Stage/adopt and physical power remain external.
 *       Reinitialize only after every user joins. This is scalar DC, not FOC.
 */
nx_result_t example_servo_current_step(example_servo_control_t* control,
                                       float current_a, bool valid, float dt_s,
                                       float* voltage_v);
#ifdef __cplusplus
}
#endif
#endif
