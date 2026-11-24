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

#include "nexus/algorithms/motion.h"
#include "nexus/control/motor.h"
#include "nexus/io/power_stage.h"
#include "observation.h"

#ifdef __cplusplus
extern "C" {
#endif

/** \brief Explicit external commissioning facts, never platform defaults. */
typedef struct {
    nx_motor_config_t motor;
    float current_zero_code[3];
    float current_a_per_code[3];
    float bus_v_per_code;
    uint32_t encoder_counts_per_turn;
    float initial_mechanical_angle_rad;
    float id_a;
    float iq_a;
    uint32_t maximum_encoder_delta;
    uint32_t maximum_scan_span_us;
    uint32_t run_duration_us;
    uint32_t startup_budget_us;
    bool commissioned;
    bool encoder_reversed;
} example_motor_drive_config_t;

/** \brief IRQ-exclusive state; Task never reads it during the session loan. */
typedef struct {
    example_motor_drive_config_t config;
    nx_power_stage_state_t stage;
    nx_motor_t motor;
    nx_encoder_tracker_t encoder;
    example_motor_observations_t* observations;
    nx_motor_output_t last_plan;
    nx_time_us_t last_angle_at;
    uint64_t last_sequence;
    nx_time_us_t run_until;
    bool reference_seen;
} example_motor_drive_context_t;

/** \brief Application owns lifecycle flags outside the IRQ-borrowed context. */
typedef struct {
    const nx_power_stage_t* port;
    nx_power_stage_session_t session;
    example_motor_observations_t observations;
    example_motor_drive_context_t context;
    nx_time_us_t run_until;
    nx_result_t primary;
    bool initialized;
    bool borrowed;
    bool stopping;
} example_motor_drive_t;

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
