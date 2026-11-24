/**
 * \file            control.h
 * \brief           Explicit calibrated MotorControl and PowerStage consumer
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-24
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#ifndef EXAMPLE_MOTOR_DRIVE_CONTROL_H
#define EXAMPLE_MOTOR_DRIVE_CONTROL_H

#include "bus.h"
#include "nexus/algorithms/motion.h"
#include "nexus/control/current.h"
#include "nexus/control/drive.h"
#include "nexus/control/modulation.h"
#include "nexus/control/motor.h"
#include "nexus/io/power_stage.h"
#include "observation.h"
#include "rotor.h"

#ifdef __cplusplus
extern "C" {
#endif

/** \brief Explicit external commissioning facts, never platform defaults.
 * \note The selected source leaf must match topology and the authored assembly.
 *       Single-shunt bootstrap voltage is an explicit finite commissioning
 *       action; its initial bus reference is a product input, not a sensor
 *       observation. Phase-shunt code does not consume these two fields.
 *       Configuration and referenced handoff objects stay immutable/stable
 *       until the session is joined; IRQ exclusively consumes each handoff.
 */
typedef struct {
    nx_motor_config_t motor;
    nx_axis_config_t axis;
    nx_axis_parameters_t axis_parameters;
    nx_modulation_topology_t topology;
    uint8_t phase_samples;
    nx_motor_ab_t bootstrap_voltage_ab;
    float bootstrap_bus_voltage_v;
    float current_zero_code[3];
    float current_a_per_code[3];
    float bus_v_per_code;
    uint32_t encoder_counts_per_turn;
    float initial_mechanical_angle_rad;
    float id_a;
    float iq_a;
    nx_axis_mode_t initial_mode;
    float initial_position_rad;
    float initial_velocity_rad_s;
    uint32_t maximum_encoder_delta;
    uint32_t maximum_scan_span_us;
    uint32_t run_duration_us;
    uint32_t startup_budget_us;
    uint32_t reference_lifetime_us;
    uint32_t feedback_division;
    uint64_t calibration_identity;
    uint64_t expected_calibration_identity;
    uint32_t calibration_revision;
    uint32_t motor_revision;
    example_rotor_queue_t* rotor;
    example_bus_queue_t* bus;
    uint32_t bus_source_id;
    bool commissioned;
    bool encoder_reversed;
} example_motor_drive_config_t;

/** \brief IRQ-exclusive state; Task never reads it during the session loan. */
typedef struct {
    const example_motor_drive_config_t* config;
    nx_power_stage_state_t stage;
    nx_drive_t fast;
    nx_current_calibration_t calibration;
    nx_drive_feedback_sampler_t feedback_sampler;
    nx_drive_feedback_queue_t* feedback;
    nx_encoder_tracker_t encoder;
    example_motor_observations_t* observations;
    nx_motor_voltage_t last_plan;
    nx_power_stage_dc_plan_t dc_plan;
    nx_time_us_t last_angle_at;
    uint64_t last_sequence;
    nx_time_us_t run_until;
    nx_time_us_t startup_until;
    bool reference_seen;
} example_motor_drive_context_t;

/** \brief Complete Task-owned additional current range with finite authority.
 * \note The limits affect subsequent Axis decisions, not already accepted
 *       IRQ references. Revision advances without wrap. A new deadline must
 *       cover the published non-bootstrap expiry high-water mark; no live
 *       queue is cleared. Only a later finite profile may relax this range.
 */
typedef struct {
    nx_axis_limits_t current;
    nx_time_us_t sampled_at;
    nx_time_us_t valid_until;
    uint64_t epoch;
    uint32_t clock_domain;
    uint32_t revision;
} example_motor_drive_limits_t;

/** \brief Application owns lifecycle flags outside the IRQ-borrowed context. */
typedef struct {
    const nx_power_stage_t* port;
    example_motor_drive_config_t config;
    nx_power_stage_session_t session;
    example_motor_observations_t observations;
    example_motor_drive_context_t context;
    nx_axis_t axis;
    nx_drive_references_t references;
    nx_drive_reference_t reference_slots[4];
    nx_drive_feedback_queue_t feedback;
    nx_drive_feedback_t feedback_slots[4];
    nx_drive_feedback_t last_feedback;
    nx_axis_output_t axis_output;
    example_motor_drive_limits_t limits;
    nx_time_us_t last_reference_until;
    uint32_t reference_generation;
    nx_time_us_t run_until;
    nx_time_us_t authorized_until;
    nx_result_t primary;
    bool initialized;
    bool borrowed;
    bool stopping;
    bool have_feedback;
    bool limits_active;
} example_motor_drive_t;

/**
 * \brief Return explicit software-reference constants without commissioning.
 * \return A complete profile with commissioned=false and identity unproven.
 * \note No measured hardware facts, allocation, device or clock access.
 */
example_motor_drive_config_t example_motor_drive_reference(void);

/**
 * \brief Admit a complete motion command through the actual Task Axis.
 * \param[in,out] drive: Live Task owner after first continuous feedback.
 * \param[in] command: Same-domain finite original receipt and newer sequence.
 * \param[in] now: Finite local Task time; queueing never refreshes authority.
 * \return SUCCESS, BUSY before feedback, or exact Axis admission error.
 * \note Task only. Admission is not IRQ adoption or physical motion.
 */
nx_result_t example_motor_drive_accept(example_motor_drive_t* drive,
                                       const nx_axis_command_t* command,
                                       nx_time_us_t now);

/** \brief Stage a complete qualified range for subsequent outer-loop steps.
 * \param[in,out] drive: Same Task owner after real startup-reference adoption.
 * \param[in] limits: New same-identity bounded range and original expiry.
 * \param[in] now: Finite local time, not before the source observation.
 * \return SUCCESS, BUSY before handoff/short authority, or validation error.
 * \note Task only. No immediate hardware or FIFO override. Every later
 *       reference is bounded by this expiry; expiry closes admission even
 *       without a new mechanical feedback record. Stop/join before reset.
 */
nx_result_t
example_motor_drive_set_limits(example_motor_drive_t* drive,
                               const example_motor_drive_limits_t* limits,
                               nx_time_us_t now);

/**
 * \brief Validate the complete profile without obtaining a hardware borrow.
 * \param[in,out] drive: Initially zeroed or joined, drained unborrowed storage,
 *                      distinct from inputs and provider.
 * \param[in] port: Stable generated PowerStage face, retained until join.
 * \param[in] config: External calibration, angle reference and budgets.
 * \return SUCCESS, INVALID or STATE; rejection preserves drive.
 * \note Single privileged Thread. Core epoch binds from the first actual
 *       provider sample, not an invented constant. Reinitialization requires
 * the sole observation consumer joined and the channel empty. Initialization
 *       does not commission hardware; commissioned=false remains a start
 * rejection.
 */
nx_result_t
example_motor_drive_initialize(example_motor_drive_t* drive,
                               const nx_power_stage_t* port,
                               const example_motor_drive_config_t* config);

/**
 * \brief Admit one finite experiment using a long-lived PowerStage session.
 * \param[in,out] drive: Initialized and completely unborrowed application.
 * \param[in] now: Finite Core time for startup and target expiry.
 * \return SUCCESS means provider ACCEPTED; errors preserve withdrawal duties.
 * \note Uncommissioned profiles reject before any provider call. Calibration
 *       and initial rotor phase must be proven externally. Neutral PWM is not
 *       a claim of isolated power or safe mechanical state.
 */
nx_result_t example_motor_drive_start(example_motor_drive_t* drive,
                                      nx_time_us_t now);

/**
 * \brief Progress startup, explicit expiry and final callback withdrawal.
 * \param[in,out] drive: Same stable single-executor owner.
 * \param[in] now: Finite Core time in the original session epoch.
 * \return BUSY until SETTLED; then exact first/final result, or STATE if idle.
 * \note Withdrawal begins one PWM period before the fixed target expires;
 *       late Task progress still causes the kernel's hard expiry. A stop
 *       error never clears borrowed or reuses IRQ state. Call again
 *       with the same storage to retry. Joined external callers and Platform
 *       cold close are separate responsibilities.
 */
nx_result_t example_motor_drive_step(example_motor_drive_t* drive,
                                     nx_time_us_t now);

/**
 * \brief Close experiment admission and request source withdrawal.
 * \param[in,out] drive: Live same-executor owner.
 * \return BUSY while borrowed, settled result, or STATE while idle.
 * \note Output inhibition and SETTLED remain distinct. No automatic restart.
 */
nx_result_t example_motor_drive_stop(example_motor_drive_t* drive);

#ifdef __cplusplus
}
#endif
#endif
