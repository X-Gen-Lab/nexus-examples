/**
 * \file            drive.h
 * \brief           Explicit paired-axis velocity and current composition
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-24
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#ifndef EXAMPLE_DIFFERENTIAL_DRIVE_H
#define EXAMPLE_DIFFERENTIAL_DRIVE_H

#include "nexus/algorithms/pid.h"
#include "nexus/control/axis.h"

#ifdef __cplusplus
extern "C" {
#endif

/** \brief Explicit teaching geometry and controller constants, no Board. */
typedef struct {
    float radius_m;
    float track_m;
    float wheel_limit_rad_s;
    uint32_t command_lifetime_us;
    nx_axis_config_t axis;
    nx_axis_parameters_t parameters;
    nx_pid_config_t current_pi;
} example_drive_config_t;

/** \brief Complete body command; receipt remains the original local time. */
typedef struct {
    uint32_t clock_domain;
    uint64_t epoch;
    uint32_t sequence;
    nx_time_us_t received_at;
    float linear_m_s;
    float angular_rad_s;
} example_drive_command_t;

/** \brief Copied decisions, not physical PWM or wheel adoption evidence. */
typedef struct {
    nx_axis_output_t axis[2];
    float raw_wheel_rad_s[2];
    float limited_wheel_rad_s[2];
    float scale;
    uint32_t generation;
} example_drive_observation_t;

/** \brief One serialized executor owns both axes, PIs and publications. */
typedef struct {
    example_drive_config_t config;
    nx_axis_t axis[2];
    nx_pid_t current_pi[2];
    example_drive_observation_t observation;
    nx_result_t fault;
    bool initialized;
    bool enabled;
} example_drive_t;

/**
 * \brief Initialize unused application storage without enabling actuation.
 * \param[out] drive: Fresh disjoint aligned state.
 * \param[in] config: Complete validated constants, copied.
 * \return SUCCESS or INVALID; rejection preserves drive.
 * \note Single Thread executor. No worker, provider, heap or hardware access.
 */
nx_result_t example_drive_initialize(example_drive_t* drive,
                                     const example_drive_config_t* config);

/**
 * \brief Establish both software references using qualified sensor feedback.
 * \param[in,out] drive: Initialized disabled owner, no fault.
 * \param[in] feedback: Two stable same-domain observations.
 * \param[in] now: Finite local monotonic time.
 * \return SUCCESS or exact axis rejection; both references commit together.
 * \note Does not start a power stage. Caller supplies calibrated wheel-side
 *       feedback, preserving source timestamps instead of plant truth.
 */
nx_result_t example_drive_enable(example_drive_t* drive,
                                 const nx_axis_feedback_t feedback[2],
                                 nx_time_us_t now);

/**
 * \brief Admit a complete body command for both velocity axes.
 * \param[in,out] drive: Enabled single-executor owner.
 * \param[in] command: New session sequence and original receipt time.
 * \param[in] now: Finite same-domain processing time.
 * \return SUCCESS or rejection, preserving both pending targets on failure.
 * \note Common wheel scaling preserves instantaneous ratio only. Heartbeat
 *       has no API here and cannot renew motion. Identical new values may be
 *       accepted; an old sequence cannot extend its original authority.
 */
nx_result_t example_drive_accept(example_drive_t* drive,
                                 const example_drive_command_t* command,
                                 nx_time_us_t now);

/**
 * \brief Run one paired slow-loop cycle, publishing only a complete decision.
 * \param[in,out] drive: Enabled stable owner.
 * \param[in] feedback: Two fresh calibrated wheel-side measurements.
 * \param[in] now: Finite same-domain cycle time.
 * \param[in] dt_s: Explicit elapsed seconds matching the axis interval.
 * \param[out] out: Separate copied observation, unchanged on failure.
 * \return SUCCESS or exact fault; hardware withdrawal remains external.
 * \note Command expiry invokes the axes' local stopping policy while valid
 *       feedback and power remain available. It does not erase faults or
 *       promise actual stopping distance. Never invokes a telemetry sink.
 */
nx_result_t example_drive_step(example_drive_t* drive,
                               const nx_axis_feedback_t feedback[2],
                               nx_time_us_t now, float dt_s,
                               example_drive_observation_t* out);

/**
 * \brief Run both scalar current PIs against copied ADC-derived amperes.
 * \param[in,out] drive: Enabled serialized owner, no latched failure.
 * \param[in] current_a: Two calibrated finite phase currents.
 * \param[in] valid: True only for valid unsaturated current samples.
 * \param[in] dt_s: Positive seconds for this current-control interval.
 * \param[out] voltage_v: Two disjoint planned model voltages.
 * \return SUCCESS or exact error; failed computation latches application fault.
 * \note This DC teaching kernel is not PMSM FOC or a hardware current owner.
 *       Output is a voltage plan; caller separately stages and observes it.
 */
nx_result_t example_drive_current_step(example_drive_t* drive,
                                       const float current_a[2], bool valid,
                                       float dt_s, float voltage_v[2]);

#ifdef __cplusplus
}
#endif
#endif
