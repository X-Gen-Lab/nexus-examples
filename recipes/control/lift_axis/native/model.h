/**
 * \file            model.h
 * \brief           Independent Native-only rotary gravity and brake plant
 * \author          Nexus Team
 */
#ifndef EXAMPLE_LIFT_MODEL_H
#define EXAMPLE_LIFT_MODEL_H
#include "lift_axis.h"
#ifdef __cplusplus
extern "C" {
#endif
/** \brief Explicit toy-plant coefficients; no real motor or brake is selected.
 */
typedef struct {
    float torque_nm_per_a;
    float inertia_kg_m2;
    float load_torque_nm;
    float damping_nm_per_rad_s;
    float current_time_constant_s;
    float encoder_rad_per_count;
    float support_tolerance_nm;
    uint32_t brake_transition_us;
    float initial_position_rad;
    float home_position_rad;
    float home_hysteresis_rad;
    float upper_limit_rad;
    float lower_limit_rad;
} example_lift_model_config_t;
/** \brief Model state belongs to the simulator, never to the controller. */
typedef struct {
    example_lift_model_config_t config;
    nx_time_us_t now;
    nx_time_us_t brake_due;
    float position_rad;
    float velocity_rad_s;
    float current_a;
    float last_encoder_rad;
    uint32_t sequence;
    uint32_t clock_domain;
    uint64_t epoch;
    example_lift_brake_state_t brake;
    example_lift_brake_state_t requested_brake;
    bool initialized;
    bool power_available;
    bool home_switch;
} example_lift_model_t;
/** \brief Initialize an engaged-brake toy plant without hardware access.
 * \param[out] model: Fresh independent simulator storage.
 * \param[in] config: Explicit finite plant and measurement coefficients.
 * \param[in] clock_domain: Nonzero observation domain.
 * \param[in] epoch: Nonzero model session.
 * \param[in] now: Finite initial microsecond time.
 * \return SUCCESS or INVALID; no controller state is read or modified.
 * \note Native software reference only; this model proves no physical holding.
 */
nx_result_t example_lift_model_initialize(
    example_lift_model_t* model, const example_lift_model_config_t* config,
    uint32_t clock_domain, uint64_t epoch, nx_time_us_t now);
/** \brief Advance current lag, gravity dynamics, brake and quantized sensors.
 * \param[in,out] model: Initialized single-executor simulator.
 * \param[in] request: Copied actuator requests, not controller internal state.
 * \param[in] now: Strictly later same-domain time.
 * \param[in] dt_s: Positive seconds matching this model interval.
 * \param[out] feedback: Independent quantized observations and model evidence.
 * \return SUCCESS, INVALID or EXHAUSTED; rejection preserves model/output.
 * \note The controller receives only feedback. Plant fields are simulator
 *       truth for separate acceptance, not a feedback shortcut. Disabling the
 *       toy drive removes motor torque; a released brake does not stop gravity.
 */
nx_result_t example_lift_model_advance(example_lift_model_t* model,
                                       const example_lift_output_t* request,
                                       nx_time_us_t now, float dt_s,
                                       example_lift_feedback_t* feedback);
#ifdef __cplusplus
}
#endif
#endif /* EXAMPLE_LIFT_MODEL_H */
