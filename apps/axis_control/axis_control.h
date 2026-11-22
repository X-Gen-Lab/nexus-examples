/**
 * \file            axis_control.h
 * \brief           Caller-owned point-to-point rotary axis software reference
 * \author          Nexus Team
 */
#ifndef EXAMPLE_AXIS_CONTROL_H
#define EXAMPLE_AXIS_CONTROL_H
#include "nexus/algorithms/calibration.h"
#include "nexus/algorithms/motion.h"
#include "nexus/algorithms/pid.h"
#include "nexus/components/settings.h"
#include "nexus/io/encoder.h"
#include "nexus/io/observation.h"
#include "nexus/io/pwm_group.h"
#ifdef __cplusplus
extern "C" {
#endif
/** \brief Only rotary position commands are selected by this reference. */
typedef enum {
    EXAMPLE_AXIS_UNIT_UNKNOWN = 0,
    EXAMPLE_AXIS_POSITION_RADIANS
} example_axis_unit_t;
/** \brief READY/MOVING describe the command profile, not mechanical rest. */
typedef enum {
    EXAMPLE_AXIS_DISABLED,
    EXAMPLE_AXIS_READY,
    EXAMPLE_AXIS_MOVING,
    EXAMPLE_AXIS_FAULT
} example_axis_state_t;
/** \brief Complete product parameter snapshot; gains use radian error. */
typedef struct {
    uint32_t revision;
    nx_pid_config_t pid;
    float minimum_position_rad;
    float maximum_position_rad;
    float maximum_velocity_rad_s;
    float acceleration_rad_s2;
} example_axis_parameters_t;
/**
 * \brief Selected software profile; no Board, motor or electrical defaults.
 * \note One privileged Thread executor owns every call and both live ports.
 *       The encoder uses a separate timer from the two-channel PWM group.
 *       Calibration maps expanded counts to radians, never a mechanical-zero
 *       discovery. Positive output selects channel 0, negative channel 1;
 *       hardware routing, interlocks and the meaning of neutral are external.
 *       clock_domain/epoch identify the shared Core monotonic domain. Keep
 *       configuration, ports and private providers alive through joined close.
 */
typedef struct {
    const nx_encoder_t* encoder;
    const nx_pwm_group_t* pwm;
    nx_calibration_f32_t calibration;
    uint64_t encoder_modulus;
    uint32_t maximum_delta_counts;
    uint32_t pwm_period_ticks;
    uint32_t trigger_ticks;
    uint32_t maximum_step_gap_us;
    uint32_t hardware_identity;
    uint32_t clock_domain;
    uint32_t epoch;
    uint32_t calibration_revision;
} example_axis_config_t;
/** \brief Copied finite command; acceptance never proves physical execution. */
typedef struct {
    example_axis_unit_t unit;
    uint32_t epoch;
    uint32_t sequence;
    nx_time_us_t valid_until;
    float position_rad;
} example_axis_command_t;
/**
 * \brief Explicit application-cycle facts, causally paired with this call.
 * \note release is a software release observation, not an encoder edge or ADC
 *       aperture. Its rate is exactly 1000000 ticks/second in the configured
 *       domain/epoch. The executor provides consecutive nonzero sequence and
 *       the selected calibration revision; invalid/missing input stops control.
 */
typedef struct {
    nx_io_timestamp_t release;
    uint32_t sequence;
    uint32_t calibration_revision;
    bool valid;
} example_axis_cycle_t;
/** \brief One copied latest observation; it retains no I/O or sample loan. */
typedef struct {
    nx_io_timestamp_t observed;
    uint32_t sequence;
    uint32_t sample_sequence;
    uint32_t parameters_revision;
    uint32_t calibration_revision;
    uint32_t overwritten_observations;
    float position_rad;
    float velocity_rad_s;
    float reference_rad;
    float normalized_output;
    bool valid;
} example_axis_observation_t;
/**
 * \brief Explicit independent application state; fields are not controls.
 * \note All publication is serialized by the same executor, including take,
 *       stage and save. Cross-task callers copy through their existing queue
 *       or guard; ordinary field reads do not implement a lock-free snapshot.
 *       Source flags retain withdrawal responsibility even in FAULT. The
 *       single observation slot may overwrite an unconsumed copy and counts
 *       that event; control never calls a logger, network or storage sink.
 */
typedef struct {
    example_axis_config_t config;
    example_axis_parameters_t parameters;
    example_axis_parameters_t pending_parameters;
    nx_pid_t pid;
    nx_pid_t pending_pid;
    nx_encoder_tracker_t tracker;
    nx_trajectory_plan_t trajectory;
    example_axis_observation_t observation;
    nx_time_us_t last_cycle;
    nx_time_us_t last_observed;
    nx_time_us_t command_start;
    nx_time_us_t command_deadline;
    uint32_t last_sequence;
    uint32_t sample_sequence;
    uint32_t persisted_revision;
    float position_rad;
    example_axis_state_t state;
    nx_result_t fault;
    bool initialized;
    bool encoder_started;
    bool pwm_started;
    bool command_live;
    bool parameters_pending;
    bool observation_pending;
} example_axis_t;
/**
 * \brief Initialize unused single-owner state without touching hardware.
 * \param[out] axis: Fresh aligned state, distinct from every input.
 * \param[in] config: Explicit selected profile, copied during this call.
 * \param[in] parameters: Complete validated initial snapshot, copied.
 * \return SUCCESS or INVALID; no I/O, allocation or retained input pointers.
 * \note Reinitialize only after both sources and every caller have joined.
 */
nx_result_t
example_axis_initialize(example_axis_t* axis,
                        const example_axis_config_t* config,
                        const example_axis_parameters_t* parameters);
/**
 * \brief Start encoder, establish reference and stage neutral before PWM.
 * \param[in,out] axis: Initialized DISABLED state and exclusive live ports.
 * \param[in] now: Finite release in the configured monotonic epoch.
 * \return SUCCESS or actual error; partial effects are withdrawn or retained.
 * \note Task-only. Reads software encoder time, not an edge aperture. A fault
 *       remains latched even after successful source withdrawal; explicit
 *       joined reinitialization is required before restarting.
 */
nx_result_t example_axis_start(example_axis_t* axis, nx_time_us_t now);
/**
 * \brief Admit one rest-to-rest target using copied command fields.
 * \param[in,out] axis: READY started axis with no pending parameter snapshot.
 * \param[in] command: Matching unit/epoch, increasing sequence, bounded target.
 * \param[in] now: Finite same-domain time, not earlier than the last cycle.
 * \return SUCCESS, INVALID/STATE/TIMEOUT, BUSY while a move is in progress.
 * \note No hardware call or retained command. This profile rejects moving
 *       replanning; READY means the profile completed, not verified settling.
 */
nx_result_t example_axis_command(example_axis_t* axis,
                                 const example_axis_command_t* command,
                                 nx_time_us_t now);
/**
 * \brief Execute one bounded Thread-mode feedback cycle and copy observation.
 * \param[in,out] axis: Started same-executor state.
 * \param[in] cycle: Stable consecutive release facts, borrowed only this call.
 * \return SUCCESS or fault; invalid time/quality/gaps close output admission.
 * \note Uses actual encoder read, calibration, tracker, trajectory, PID and
 *       coherent PWM-group commit. Current F4 task-only APIs are not called
 *       from IRQ. Commit stages shadows, never proves an electrical update.
 *       Errors request both source stops; failed withdrawal retains flags and
 *       ports for explicit stop retry. No physical braking or FOC is selected.
 */
nx_result_t example_axis_step(example_axis_t* axis,
                              const example_axis_cycle_t* cycle);
/**
 * \brief Close admission and retry both source withdrawals.
 * \param[in,out] axis: Initialized same-executor state and retained ports.
 * \return SUCCESS if both stops succeeded, or first error retaining flags.
 * \note No borrowed asynchronous request exists in this selected profile.
 *       Success releases runtime sources only, not cold leases or providers.
 *       FAULT remains latched; external users and Platform still need join.
 */
nx_result_t example_axis_stop(example_axis_t* axis);
/**
 * \brief Stage one complete newer parameter revision without persistence.
 * \param[in,out] axis: Initialized serialized state; at most one candidate.
 * \param[in] parameters: Valid newer snapshot, copied; no retained pointer.
 * \return SUCCESS, INVALID/STATE, BUSY for pending or active motion limits.
 * \note A running axis applies the entire candidate at its next cycle start;
 *       PID bumpless reconfiguration is prevalidated. Calibration is immutable.
 *       Disabled state applies on start, or explicitly before save via apply.
 */
nx_result_t
example_axis_stage_parameters(example_axis_t* axis,
                              const example_axis_parameters_t* parameters);
/**
 * \brief Apply staged parameters while sources are disabled.
 * \param[in,out] axis: Initialized state with both sources withdrawn.
 * \return SUCCESS or STATE; no storage or hardware access.
 * \note Running cycles apply internally at the same serialized boundary.
 */
nx_result_t example_axis_apply_parameters(example_axis_t* axis);
/**
 * \brief Copy and consume the latest optional telemetry snapshot.
 * \param[in,out] axis: Initialized same-executor state.
 * \param[out] out: Distinct aligned copy, unchanged when no observation exists.
 * \return SUCCESS, EMPTY or INVALID; no network I/O or retained pointer.
 */
nx_result_t example_axis_take_observation(example_axis_t* axis,
                                          example_axis_observation_t* out);
/**
 * \brief Explicitly persist active parameters in an already open Settings.
 * \param[in,out] axis: Quiesced state without pending parameters.
 * \param[in,out] settings: Product-owned, exclusively open exact schema.
 * \param[in] key: Product-assigned record key.
 * \return Storage status; failure may have changed media and needs recovery.
 * \note Same task only, sources disabled: F407 Flash writes can stall IRQs.
 *       Record uses fixed little-endian IEEE binary32, version and hardware ID.
 *       No partition/default formatting/migration is selected. Success alone
 *       updates persisted_revision; failure does not roll back active settings.
 */
nx_storage_status_t example_axis_save_parameters(example_axis_t* axis,
                                                 nx_settings_t* settings,
                                                 uint32_t key);
/**
 * \brief Load exact record into a disabled axis without migration or defaults.
 * \param[in,out] axis: Quiesced initialized state; no pending parameters.
 * \param[in] settings: Product-owned open exact-schema settings, serialized.
 * \param[in] key: Exact product record key.
 * \return Storage status; rejection preserves active axis parameters.
 * \note Checks size, format, hardware identity and every decoded bound before
 *       applying. Missing/corrupt/old schema never becomes an empty writable
 *       default. No I/O other than Settings RAM retrieval occurs here.
 */
nx_storage_status_t example_axis_load_parameters(example_axis_t* axis,
                                                 const nx_settings_t* settings,
                                                 uint32_t key);
#ifdef __cplusplus
}
#endif
#endif
