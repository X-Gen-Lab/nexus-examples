/**
 * \file            lift_axis.h
 * \brief           Explicit vertical-axis support handover and bounded homing
 * \author          Nexus Team
 */
#ifndef EXAMPLE_LIFT_AXIS_H
#define EXAMPLE_LIFT_AXIS_H
#include "nexus/control/axis.h"
#ifdef __cplusplus
extern "C" {
#endif
/** \brief Qualified mechanical state, not a brake coil command or timer. */
typedef enum {
    EXAMPLE_LIFT_BRAKE_UNKNOWN,
    EXAMPLE_LIFT_BRAKE_ENGAGED,
    EXAMPLE_LIFT_BRAKE_RELEASED
} example_lift_brake_state_t;
/** \brief Explicit actuator request; KEEP makes no new mechanical command. */
typedef enum {
    EXAMPLE_LIFT_BRAKE_INVALID,
    EXAMPLE_LIFT_BRAKE_KEEP,
    EXAMPLE_LIFT_BRAKE_CLOSE,
    EXAMPLE_LIFT_BRAKE_RELEASE
} example_lift_brake_request_t;
/** \brief Application sequence; no state certifies a physical safety function.
 */
typedef enum {
    EXAMPLE_LIFT_SECURED,
    EXAMPLE_LIFT_BUILD_SUPPORT,
    EXAMPLE_LIFT_RELEASING,
    EXAMPLE_LIFT_ACTIVE,
    EXAMPLE_LIFT_STOPPING,
    EXAMPLE_LIFT_ENGAGING,
    EXAMPLE_LIFT_FAULT
} example_lift_state_t;
/** \brief Finite reference search, distinct from rotor electrical alignment. */
typedef enum {
    EXAMPLE_LIFT_HOME_IDLE,
    EXAMPLE_LIFT_HOME_SEARCH,
    EXAMPLE_LIFT_HOME_BACKOFF,
    EXAMPLE_LIFT_HOME_APPROACH,
    EXAMPLE_LIFT_HOME_SETTLING
} example_lift_homing_t;
/** \brief Caller-selected mechanical profile; no Board or motor defaults.
 * \note All positions and velocities are on the same rotary mechanical side
 *       as Axis feedback. The application owns transmission-to-height mapping.
 *       support_feedforward_a must match Axis stop_feedforward_a. Fault brake
 *       policies must explicitly select KEEP or CLOSE; neither is a certified
 *       emergency response. No automatic dynamic braking is selected.
 */
typedef struct {
    uint32_t maximum_feedback_age_us;
    uint32_t handover_timeout_us;
    uint32_t homing_timeout_us;
    uint32_t homing_phase_timeout_us;
    float homing_maximum_phase_distance_rad;
    float search_velocity_rad_s;
    float approach_velocity_rad_s;
    float home_coordinate_rad;
    float support_feedforward_a;
    example_lift_brake_request_t on_feedback_loss;
    example_lift_brake_request_t on_power_loss;
} example_lift_config_t;
/** \brief Complete physical observations supplied by one explicit executor.
 * \note brake_valid and ENGAGED mean qualified mechanical holding capability,
 *       not merely a coil level. support_confirmed independently establishes
 *       the configured motor support, never inferred from a current command.
 *       All times share motion.clock_domain/epoch; source ages are checked.
 *       drive_ready=false confirms withdrawn drive only under the caller's
 *       own hardware contract. Ordinary booleans are not concurrent mailboxes.
 */
typedef struct {
    nx_axis_feedback_t motion;
    nx_time_us_t brake_sampled_at;
    nx_time_us_t support_sampled_at;
    nx_time_us_t switches_sampled_at;
    example_lift_brake_state_t brake;
    bool brake_valid;
    bool support_confirmed;
    bool switches_valid;
    bool home_switch;
    bool upper_limit;
    bool lower_limit;
    bool power_available;
    bool drive_ready;
} example_lift_feedback_t;
/** \brief Copied requests/facts; external owners execute and confirm actions.
 */
typedef struct {
    nx_axis_output_t axis;
    example_lift_state_t state;
    example_lift_homing_t homing;
    example_lift_brake_request_t brake_request;
    nx_result_t fault;
    float reference_offset_rad;
    bool motor_permit;
    bool support_unconfirmed;
    bool reference_valid;
} example_lift_output_t;
/** \brief Single-executor state, retained until all users and I/O have joined.
 */
typedef struct {
    nx_axis_t* axis;
    example_lift_config_t config;
    example_lift_output_t output;
    example_lift_state_t state;
    example_lift_homing_t homing;
    nx_time_us_t deadline;
    nx_time_us_t homing_deadline;
    nx_time_us_t last_step;
    float phase_origin_rad;
    float candidate_offset_rad;
    float commanded_velocity_rad_s;
    float reference_offset_rad;
    uint32_t sequence;
    nx_result_t fault;
    bool initialized;
    bool reference_valid;
    bool candidate_reference;
} example_lift_t;
/** \brief Bind a disabled HOLD-policy Axis without hardware access.
 * \param[out] lift: Fresh independent caller storage.
 * \param[in,out] axis: Initialized DISABLED Axis; retained through joined
 * close. \param[in] config: Complete explicit profile, copied on success.
 * \return SUCCESS or INVALID/STATE; rejection preserves lift.
 * \note Task-only. Caller serializes every call and all Axis use; no worker.
 */
nx_result_t example_lift_initialize(example_lift_t* lift, nx_axis_t* axis,
                                    const example_lift_config_t* config);
/** \brief Start support building while the qualified brake remains engaged.
 * \param[in,out] lift: SECURED owner without a latched fault.
 * \param[in] feedback: Fresh complete initial observation.
 * \param[in] now: Finite same-domain monotonic microseconds.
 * \return SUCCESS or exact error; no release is inferred or executed.
 * \note Task-only. Output requests become observable on the next step.
 */
nx_result_t example_lift_start(example_lift_t* lift,
                               const example_lift_feedback_t* feedback,
                               nx_time_us_t now);
/** \brief Accept one finite-authority velocity command while mechanically free.
 * \param[in,out] lift: ACTIVE same-executor owner.
 * \param[in] velocity_rad_s: Signed finite requested mechanical velocity.
 * \param[in] received_at: Original local command receipt, never retimestamped.
 * \param[in] valid_until: Original finite command expiry.
 * \param[in] now: Same-domain executor time.
 * \return SUCCESS or exact admission error; no hidden command renewal.
 * \note Task-only. Protocol authentication and wire identity remain external.
 */
nx_result_t example_lift_move(example_lift_t* lift, float velocity_rad_s,
                              nx_time_us_t received_at,
                              nx_time_us_t valid_until, nx_time_us_t now);
/** \brief Begin finite negative search, positive backoff and slow reapproach.
 * \param[in,out] lift: ACTIVE owner; current mechanical reference invalidated.
 * \param[in] feedback: Fresh switch and motion facts.
 * \param[in] now: Finite same-domain time.
 * \return SUCCESS or exact error; search distance/time are always bounded.
 * \note Task-only. Home edge defines an offset; no counter reset or electrical
 *       alignment occurs. Reference becomes valid only after stop and handover.
 */
nx_result_t example_lift_home(example_lift_t* lift,
                              const example_lift_feedback_t* feedback,
                              nx_time_us_t now);
/** \brief Request controlled stop while preserving powered support.
 * \param[in,out] lift: ACTIVE or already stopping owner.
 * \param[in] now: Finite same-domain time for one handover budget.
 * \return SUCCESS or exact error; no brake command is blindly confirmed.
 * \note Task-only. Continue step with valid feedback and usable power.
 */
nx_result_t example_lift_request_stop(example_lift_t* lift, nx_time_us_t now);
/** \brief Execute one bounded sequence and Axis cycle, then copy requests.
 * \param[in,out] lift: Initialized single-executor state.
 * \param[in] feedback: Full independent observations, borrowed for this call.
 * \param[in] now: Strictly increasing finite same-domain time.
 * \param[in] dt_s: Positive seconds matching the actual Axis interval.
 * \param[out] out: Separate copied requests and confirmation facts.
 * \return SUCCESS, or STATE with latched fault requests; INVALID leaves out.
 * \note Task-only. No GPIO, hardware stop, allocation or blocking occurs.
 *       Feedback/power failure selects the explicit profile response and marks
 *       unconfirmed support; it never fabricates controlled stopping. External
 *       hardware and users must still be stopped/joined before reclamation.
 */
nx_result_t example_lift_step(example_lift_t* lift,
                              const example_lift_feedback_t* feedback,
                              nx_time_us_t now, float dt_s,
                              example_lift_output_t* out);
/** \brief Clear software fault after confirmed brake and drive withdrawal.
 * \param[in,out] lift: FAULT or SECURED owner with joined external users.
 * \param[in] feedback: Fresh engaged-brake and drive-withdrawn confirmation.
 * \param[in] now: Finite same-domain time.
 * \return SUCCESS or STATE; success stays SECURED and never restarts motion.
 * \note Task-only. A new explicit start is required. This is no safety reset.
 */
nx_result_t example_lift_reset(example_lift_t* lift,
                               const example_lift_feedback_t* feedback,
                               nx_time_us_t now);
#ifdef __cplusplus
}
#endif
#endif /* EXAMPLE_LIFT_AXIS_H */
