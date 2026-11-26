/**
 * \file            cia402_motor.h
 * \brief           Real CiA402 commands over an explicitly armed PMSM consumer
 * \author          Nexus Team
 */
#ifndef EXAMPLE_CIA402_MOTOR_H
#define EXAMPLE_CIA402_MOTOR_H

#include "control.h"
#include "nexus/components/cia402_position.h"

#ifdef __cplusplus
extern "C" {
#endif

/** \brief Product authorization, independent of protocol polling/heartbeats.
 * \note authority_until is finite in the injected transport clock. Each real
 *       RPDO receives at most command_lifetime_us from its original local CAN
 *       receipt, bounded by that authority. The selected profile's ramps must
 *       match the fixed Axis ramp; no unimplemented quick-stop ramp is claimed.
 */
typedef struct {
    nx_time_us_t authority_until;
    uint32_t command_lifetime_us;
    float velocity_window_rad_s;
    uint32_t velocity_dwell_us;
} example_cia402_motor_policy_t;

/**
 * \brief One Task-owned adapter, retaining only existing owners and one
 * profile. \note PV and PP objects are separate caller storage; only one
 * pointer is bound. The receive observer delegates to the existing CAN owner
 * and records RPDO receipts in a completed operational window, without another
 * controller owner. Keep this identity, profile, motor, node, arena and CAN
 * alive until stop succeeds and
 * users join. Fields are observations, not IRQ controls.
 */
typedef struct {
    example_motor_drive_t* motor;
    nx_cia402_t* velocity;
    nx_cia402_position_t* position;
    nx_canopen_t* node;
    const nx_can_port_t* can;
    nx_can_port_t observed_port;
    nx_clock_t clock;
    example_cia402_motor_policy_t policy;
    nx_canopen_report_t protocol;
    nx_axis_command_t pending_command;
    nx_time_us_t received_at;
    nx_time_us_t dwell_since;
    uint32_t wire_generation;
    uint32_t handled_wire_generation;
    uint32_t command_sequence;
    uint32_t pending_generation;
    uint32_t accepted_generation;
    uint32_t rejected_generation;
    uint32_t pending_axis_sequence;
    uint32_t adopted_command_sequence;
    uint32_t observed_sequence;
    int32_t received_target;
    uint16_t received_controlword;
    int8_t received_mode;
    uint8_t node_id;
    nx_result_t command_result;
    nx_result_t fault;
    bool pending;
    bool operation_seen;
    bool stop_requested;
    bool reset_requested;
    bool dwelling;
    bool initialized;
    bool closing;
    bool receive_operational;
} example_cia402_motor_t;

/** \brief Bind an initialized PV profile to the actual motor/CAN application.
 * \param[in,out] adapter: Fresh stable zero storage, never moved while bound.
 * \param[in,out] motor: Initialized application, explicit commissioning/start.
 * \param[in,out] profile: Initialized unbound selected PV profile.
 * \param[out] node: Fresh CANopen identity, retained through final stop.
 * \param[in] transport: Explicit clock/CAN/queue configuration, no OD objects.
 * \param[in] policy: Fixed finite product authority and original-receipt lease.
 * \param[in,out] workspace: Caller aligned CANopen arena.
 * \param[in] bytes: Arena extent, validated by the existing CANopen query.
 * \return SUCCESS or exact validation/bind error, without starting a motor.
 * \note Same Task owns protocol, motor outer step and this adapter. SDO changes
 *       stage protocol objects; only actual seven-byte RPDO receipts authorize
 *       new application commands after a completed operational protocol step.
 *       Addressed NMT closes that receipt window. Reset/enable requests never
 *       auto-start I/O.
 */
nx_result_t example_cia402_motor_bind_velocity(
    example_cia402_motor_t* adapter, example_motor_drive_t* motor,
    nx_cia402_t* profile, nx_canopen_t* node,
    const nx_canopen_config_t* transport,
    const example_cia402_motor_policy_t* policy, void* workspace, size_t bytes);

/** \brief Bind an initialized PP profile to the actual motor/CAN application.
 * \param[in,out] adapter: Fresh stable zero storage, never moved while bound.
 * \param[in,out] motor: Initialized POSITION application with a fixed ramp.
 * \param[in,out] profile: Initialized unbound selected absolute PP profile.
 * \param[out] node: Fresh CANopen identity, retained through final stop.
 * \param[in] transport: Explicit clock/CAN/queue configuration, no OD objects.
 * \param[in] policy: Finite authority and original-receipt lease.
 * \param[in,out] workspace: Caller aligned CANopen arena.
 * \param[in] bytes: Available extent for the selected PP object count.
 * \return SUCCESS or exact error; no allocation, hardware start or homing.
 * \note A point is acknowledged only after its matching command sequence is
 *       observed in real fast-loop feedback. A held bit 4 never renews it.
 */
nx_result_t example_cia402_motor_bind_position(
    example_cia402_motor_t* adapter, example_motor_drive_t* motor,
    nx_cia402_position_t* profile, nx_canopen_t* node,
    const nx_canopen_config_t* transport,
    const example_cia402_motor_policy_t* policy, void* workspace, size_t bytes);

/** \brief Progress actual protocol, Task Axis and matching fast-loop receipts.
 * \param[in,out] adapter: Same live Task identity and retained owners.
 * \param[in] tx_deadline: Finite absolute injected-clock CAN queue budget.
 * \param[in] reset_permitted: Product recovery fact after real joined stop.
 * \return SUCCESS or BUSY before fresh PP feedback, otherwise exact error.
 * \note Reads only Task-owned motor observations and public provider state.
 *       Polling, heartbeat, NMT and repeated action snapshots never renew a
 *       command. Only a new actual RPDO receipt can admit a new PV command.
 *       Hardware/transport failures withdraw; ordinary halt/quick-stop requests
 *       use the selected Axis stop. No physical stop guarantee is inferred.
 *       Faults and authority/operation withdrawal do not wait for a new
 *       position sample; repeated observations never become fresh samples.
 *       Transport closure clears the local profile's operation and setpoint
 *       status immediately. The first adapter fault survives later steps.
 */
nx_result_t example_cia402_motor_step(example_cia402_motor_t* adapter,
                                      nx_time_us_t tx_deadline,
                                      bool reset_permitted);

/** \brief Close commands and join protocol TX plus the actual motor session.
 * \param[in,out] adapter: Same stable Task identity and all retained storage.
 * \return SUCCESS after both joins; BUSY/IO retains original identities.
 * \note Never stops/reinitializes the outer CAN controller. Protocol reset is
 *       only a request; release never re-enables or clears motor hardware.
 */
nx_result_t example_cia402_motor_stop(example_cia402_motor_t* adapter);

#ifdef __cplusplus
}
#endif
#endif /* EXAMPLE_CIA402_MOTOR_H */
