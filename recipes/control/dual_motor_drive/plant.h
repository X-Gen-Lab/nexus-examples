/**
 * \file            plant.h
 * \brief           Independent averaged PMSM and quantized sensor model
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-24
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#ifndef EXAMPLE_PMSM_PLANT_H
#define EXAMPLE_PMSM_PLANT_H

#include "nexus/core/status.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** \brief Host-only physical and sensor constants; SI, amplitude invariant. */
typedef struct {
    double resistance_ohm;
    double ld_h;
    double lq_h;
    double flux_wb;
    double inertia_kg_m2;
    double viscous_nm_per_rad_s;
    double current_a_per_code;
    uint32_t pole_pairs;
    uint32_t encoder_counts_per_turn;
    uint16_t current_zero_code;
} example_pmsm_config_t;

/** \brief Independent plant truth; production controllers never read it. */
typedef struct {
    example_pmsm_config_t config;
    double id_a;
    double iq_a;
    double velocity_rad_s;
    double position_rad;
    double dc_current_a;
    uint64_t cycle;
    bool initialized;
} example_pmsm_t;

/** \brief Quantized sensor outputs, with no exact current or speed truth. */
typedef struct {
    uint16_t current_code[3];
    uint32_t encoder_count;
    bool saturated;
} example_pmsm_sensor_t;

/**
 * \brief Initialize distinct, fresh averaged plant storage.
 * \param[out] plant: Unused caller model, distinct from config.
 * \param[in] config: Positive electrical, inertia and sensor constants.
 * \return SUCCESS or INVALID; rejection preserves plant.
 * \note Host SIL only, no physical device or scheduler is inferred.
 */
nx_result_t example_pmsm_initialize(example_pmsm_t* plant,
                                    const example_pmsm_config_t* config);

/**
 * \brief Advance from actual average leg duty, without controller internals.
 * \param[in,out] plant: Initialized, exclusively owned model.
 * \param[in] duty: Three finite [0,1] active leg fractions.
 * \param[in] bus_voltage_v: Finite positive current DC bus voltage.
 * \param[in] dt_s: Finite positive interval, at most min(Ld,Lq)/(8R).
 * \param[in] load_nm: Signed opposing mechanical torque.
 * \return SUCCESS, INVALID or EXHAUSTED; rejection preserves plant.
 * \note Explicit midpoint integration. Positive dc_current_a draws from bus;
 *       negative returns energy. Switching ripple, losses and EMI are absent.
 */
nx_result_t example_pmsm_advance(example_pmsm_t* plant, const float duty[3],
                                 double bus_voltage_v, double dt_s,
                                 double load_nm);

/**
 * \brief Convert model currents and position to bounded sensor codes.
 * \param[in] plant: Stable initialized model, never controller state.
 * \param[out] sensor: Distinct output, unchanged on failure.
 * \return SUCCESS, INVALID or EXHAUSTED.
 * \note ADC range is [0,4095], encoder modulus 65536. Clipping is explicit.
 */
nx_result_t example_pmsm_sense(const example_pmsm_t* plant,
                               example_pmsm_sensor_t* sensor);

#ifdef __cplusplus
}
#endif
#endif
