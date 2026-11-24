/**
 * \file            sil_model.h
 * \brief           Independent DC plant and quantized sensor model
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-24
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#ifndef EXAMPLE_DRIVE_SIL_MODEL_H
#define EXAMPLE_DRIVE_SIL_MODEL_H

#include "nexus/core/status.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** \brief Explicit ideal DC electrical and mechanical model, SI units. */
typedef struct {
    double resistance_ohm;
    double inductance_h;
    double back_emf_v_per_rad_s;
    double torque_nm_per_a;
    double inertia_kg_m2;
    double viscous_nm_per_rad_s;
    double bus_voltage_v;
    uint32_t counts_per_turn;
    double current_a_per_code;
    uint16_t current_zero_code;
} example_drive_plant_config_t;

/** \brief Model truth stays outside every production controller input. */
typedef struct {
    example_drive_plant_config_t config;
    double current_a;
    double velocity_rad_s;
    double position_rad;
    double staged_voltage_v;
    double active_voltage_v;
    double load_nm;
    uint64_t cycle;
} example_drive_plant_t;

/** \brief Copied sensor output; codes deliberately contain no plant truth. */
typedef struct {
    uint32_t encoder_count;
    uint16_t current_code;
    uint64_t sequence;
    bool valid;
    bool saturated;
} example_drive_sensor_t;

/** \brief Independent ideal planar motion; never read by wheel control. */
typedef struct {
    double x_m;
    double y_m;
    double heading_rad;
} example_drive_pose_t;

/**
 * \brief Initialize unused model state without advancing time.
 * \param[out] plant: Fresh distinct caller storage.
 * \param[in] config: Positive electrical, sensor and inertia constants.
 * \return SUCCESS or INVALID; rejection preserves plant.
 * \note Host-only explicit model. No physical Board or control qualification.
 */
nx_result_t
example_drive_plant_initialize(example_drive_plant_t* plant,
                               const example_drive_plant_config_t* config);

/**
 * \brief Stage one finite voltage; the next advance models an output delay.
 * \param[in,out] plant: Initialized single-executor model.
 * \param[in] voltage_v: Finite requested voltage, clamped to the model bus.
 * \return SUCCESS or INVALID; rejection preserves plant.
 * \note Does not change active voltage or imply a hardware apply event.
 */
nx_result_t example_drive_plant_stage(example_drive_plant_t* plant,
                                      double voltage_v);

/**
 * \brief Advance the DC model, then adopt the previously staged voltage.
 * \param[in,out] plant: Independent initialized state.
 * \param[in] dt_s: Finite positive interval, no larger than L/(4R).
 * \param[in] load_nm: Finite resisting torque, sign retained.
 * \return SUCCESS, INVALID or EXHAUSTED; rejection preserves plant.
 * \note Explicit Euler model with a deliberate one-step output delay. The
 *       interval bound is a model restriction, not a control stability proof.
 */
nx_result_t example_drive_plant_advance(example_drive_plant_t* plant,
                                        double dt_s, double load_nm);

/**
 * \brief Produce quantized modulo encoder and unsigned ADC sensor codes.
 * \param[in] plant: Stable model truth, inaccessible to production control.
 * \param[in] valid: External sensor health, copied without inventing samples.
 * \param[out] sample: Distinct result, unchanged on failure.
 * \return SUCCESS, INVALID or EXHAUSTED.
 * \note Encoder modulus is 2^32. ADC clipping sets saturated; zero code does
 *       not imply zero physical current. No clock or publication side effects.
 */
nx_result_t example_drive_plant_sense(const example_drive_plant_t* plant,
                                      bool valid,
                                      example_drive_sensor_t* sample);

/**
 * \brief Integrate independent planar truth or a separately owned odometry.
 * \param[in,out] pose: Finite state owned by one caller.
 * \param[in] left_rad_s: Finite wheel speed.
 * \param[in] right_rad_s: Finite wheel speed.
 * \param[in] left_radius_m: Positive effective radius.
 * \param[in] right_radius_m: Positive effective radius.
 * \param[in] track_m: Positive wheel separation.
 * \param[in] dt_s: Positive finite interval.
 * \return SUCCESS, INVALID or EXHAUSTED; rejection preserves pose.
 * \note Exact constant-twist integration. Wheel slip and compliance are not
 *       included. Using encoder odometry does not establish ground truth.
 */
nx_result_t example_drive_pose_advance(example_drive_pose_t* pose,
                                       double left_rad_s, double right_rad_s,
                                       double left_radius_m,
                                       double right_radius_m, double track_m,
                                       double dt_s);

#ifdef __cplusplus
}
#endif
#endif
