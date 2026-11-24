/**
 * \file            cia402_motor.c
 * \brief           Explicit CANopen-to-PMSM command and feedback composition
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-24
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "cia402_motor.h"
#include <float.h>
#include <math.h>

static nx_cia402_core_t* core(example_cia402_motor_t* adapter) {
    return adapter->velocity != NULL ? &adapter->velocity->core
                                     : &adapter->position->core;
}

static nx_result_t can_submit(void* argument, nx_can_tx_request_t* request) {
    return nx_can_port_submit(((example_cia402_motor_t*)argument)->can,
                              request);
}

static nx_result_t can_cancel(void* argument, nx_can_tx_request_t* request) {
    return nx_can_port_cancel(((example_cia402_motor_t*)argument)->can,
                              request);
}

static nx_result_t can_service(void* argument) {
    return nx_can_port_service(((example_cia402_motor_t*)argument)->can);
}

static nx_result_t can_state(void* argument, nx_can_state_t* state) {
    return nx_can_port_state(((example_cia402_motor_t*)argument)->can, state);
}

static int32_t target_value(const uint8_t bytes[4]) {
    uint32_t raw = (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8U |
                   (uint32_t)bytes[2] << 16U | (uint32_t)bytes[3] << 24U;
    int64_t value = (raw & UINT32_C(0x80000000)) != 0U
                        ? (int64_t)raw - INT64_C(0x100000000)
                        : (int64_t)raw;
    return (int32_t)value;
}

static nx_result_t can_read(void* argument, nx_can_event_t* events,
                            size_t capacity, size_t* count) {
    example_cia402_motor_t* adapter = argument;
    nx_result_t result =
        nx_can_port_read_events(adapter->can, events, capacity, count);
    if (result != NX_SUCCESS)
        return result;
    if (*count > capacity)
        return NX_ERROR_IO;
    for (size_t index = 0U; index < *count; ++index) {
        const nx_can_event_t* event = &events[index];
        const nx_can_frame_t* frame = &event->frame;
        if (event->kind != NX_CAN_EVENT_FRAME ||
            frame->format != NX_CAN_STANDARD || frame->kind != NX_CAN_DATA)
            continue;
        if (frame->id == 0U && frame->dlc == 2U &&
            (frame->data[1] == 0U || frame->data[1] == adapter->node_id)) {
            /* Only a completed protocol step opens a new operational window.
             * Never authorize through an NMT transition in this receive batch.
             */
            adapter->receive_operational = false;
            adapter->pending = false;
            adapter->handled_wire_generation = adapter->wire_generation;
        }
        if (!adapter->receive_operational ||
            frame->id != 0x200U + adapter->node_id || frame->dlc != 7U)
            continue;
        nx_time_us_t now = adapter->clock.read(adapter->clock.context);
        if (event->timestamp_us == NX_DEADLINE_NEVER ||
            event->timestamp_us > now || now == NX_DEADLINE_NEVER ||
            adapter->wire_generation == UINT32_MAX) {
            adapter->fault = NX_ERROR_STATE;
            return NX_ERROR_STATE;
        }
        /* CANopen's selected asynchronous RPDO adopts its latest complete
         * frame. Retain that frame's real receipt even for identical values. */
        ++adapter->wire_generation;
        adapter->received_at = event->timestamp_us;
        adapter->received_controlword =
            (uint16_t)((uint16_t)frame->data[0] | (uint16_t)frame->data[1]
                                                      << 8U);
        adapter->received_mode = (int8_t)frame->data[2];
        adapter->received_target = target_value(&frame->data[3]);
    }
    return NX_SUCCESS;
}

static const nx_can_ops_t observed_ops = {
    can_submit, can_cancel, can_service, can_read, can_state, NULL, NULL, NULL};

static bool same_ramp(float first, float second) {
    return isfinite(first) && isfinite(second) &&
           fabsf(first - second) <=
               8.0F * FLT_EPSILON * fmaxf(1.0F, fabsf(second));
}

static bool supported_ramps(const example_motor_drive_t* motor,
                            const nx_cia402_config_t* selected) {
    const float ramp = motor->config.axis_parameters.acceleration_rad_s2;
    const float scale = selected->velocity_unit_rad_s;
    return same_ramp((float)selected->acceleration * scale, ramp) &&
           same_ramp((float)selected->deceleration * scale, ramp) &&
           same_ramp((float)selected->quick_stop_deceleration * scale, ramp) &&
           (float)selected->maximum_velocity * scale <=
               motor->config.axis_parameters.velocity_limit_rad_s;
}

static nx_result_t prepare(example_cia402_motor_t* adapter,
                           example_motor_drive_t* motor, nx_canopen_t* node,
                           const nx_canopen_config_t* transport,
                           const example_cia402_motor_policy_t* policy) {
    if (adapter == NULL || motor == NULL || node == NULL || transport == NULL ||
        policy == NULL || !motor->initialized || transport->port == NULL ||
        transport->clock.read == NULL || policy->command_lifetime_us == 0U ||
        policy->authority_until == NX_DEADLINE_NEVER ||
        !isfinite(policy->velocity_window_rad_s) ||
        policy->velocity_window_rad_s < 0.0F)
        return NX_ERROR_INVALID;
    if (adapter->initialized || node->state != NULL)
        return NX_ERROR_STATE;
    nx_time_us_t now = transport->clock.read(transport->clock.context);
    if (now == NX_DEADLINE_NEVER || policy->authority_until <= now)
        return NX_ERROR_TIMEOUT;
    *adapter =
        (example_cia402_motor_t){.motor = motor,
                                 .node = node,
                                 .can = transport->port,
                                 .observed_port = {&observed_ops, adapter},
                                 .clock = transport->clock,
                                 .policy = *policy,
                                 .node_id = transport->node_id};
    return NX_SUCCESS;
}

nx_result_t
example_cia402_motor_bind_velocity(example_cia402_motor_t* adapter,
                                   example_motor_drive_t* motor,
                                   nx_cia402_t* profile, nx_canopen_t* node,
                                   const nx_canopen_config_t* transport,
                                   const example_cia402_motor_policy_t* policy,
                                   void* workspace, size_t bytes) {
    if (motor == NULL || profile == NULL || !profile->core.initialized)
        return NX_ERROR_INVALID;
    if (motor->config.initial_mode != NX_AXIS_VELOCITY ||
        !supported_ramps(motor, &profile->core.config))
        return NX_ERROR_UNSUPPORTED;
    nx_result_t result = prepare(adapter, motor, node, transport, policy);
    if (result != NX_SUCCESS)
        return result;
    adapter->velocity = profile;
    nx_canopen_config_t selected = *transport;
    selected.port = &adapter->observed_port;
    result = nx_cia402_node_init(profile, node, &selected, workspace, bytes);
    if (result == NX_SUCCESS)
        adapter->initialized = true;
    else
        *adapter = (example_cia402_motor_t){0};
    return result;
}

nx_result_t example_cia402_motor_bind_position(
    example_cia402_motor_t* adapter, example_motor_drive_t* motor,
    nx_cia402_position_t* profile, nx_canopen_t* node,
    const nx_canopen_config_t* transport,
    const example_cia402_motor_policy_t* policy, void* workspace,
    size_t bytes) {
    if (motor == NULL || profile == NULL || !profile->core.initialized)
        return NX_ERROR_INVALID;
    const nx_axis_parameters_t* axis = &motor->config.axis_parameters;
    if (motor->config.initial_mode != NX_AXIS_POSITION ||
        !supported_ramps(motor, &profile->core.config) ||
        !same_ramp((float)profile->config.profile_velocity *
                       profile->core.config.velocity_unit_rad_s,
                   axis->velocity_limit_rad_s) ||
        (double)profile->config.minimum_position *
                profile->config.position_unit_rad <
            axis->minimum_position_rad ||
        (double)profile->config.maximum_position *
                profile->config.position_unit_rad >
            axis->maximum_position_rad)
        return NX_ERROR_UNSUPPORTED;
    nx_result_t result = prepare(adapter, motor, node, transport, policy);
    if (result != NX_SUCCESS)
        return result;
    adapter->position = profile;
    nx_canopen_config_t selected = *transport;
    selected.port = &adapter->observed_port;
    result = nx_cia402_position_node_init(profile, node, &selected, workspace,
                                          bytes);
    if (result == NX_SUCCESS)
        adapter->initialized = true;
    else
        *adapter = (example_cia402_motor_t){0};
    return result;
}

static void withdraw(example_cia402_motor_t* adapter) {
    adapter->pending = false;
    adapter->stop_requested = true;
    if (adapter->motor->borrowed) {
        (void)nx_drive_inhibit(&adapter->motor->references,
                               adapter->fault == NX_SUCCESS ? NX_ERROR_CANCELLED
                                                            : adapter->fault);
        (void)example_motor_drive_stop(adapter->motor);
    }
}

static void controlled_stop(example_cia402_motor_t* adapter) {
    adapter->pending = false;
    adapter->stop_requested = true;
    if (adapter->motor->borrowed &&
        adapter->motor->axis.state != NX_AXIS_DISABLED &&
        adapter->motor->axis.state != NX_AXIS_FAULT)
        (void)nx_axis_request_stop(&adapter->motor->axis);
}

static nx_cia402_feedback_t feedback(example_cia402_motor_t* adapter,
                                     nx_time_us_t now, bool reset_permitted) {
    example_motor_drive_t* motor = adapter->motor;
    const nx_drive_feedback_t* record = &motor->last_feedback;
    nx_power_stage_state_t stage = {0};
    nx_result_t queried = nx_power_stage_state(motor->port, &stage);
    bool valid = motor->have_feedback && record->mechanical.valid &&
                 record->mechanical.sampled_at <= now &&
                 now - record->mechanical.sampled_at <=
                     motor->config.axis.maximum_feedback_age_us;
    bool running = motor->axis.state == NX_AXIS_RUNNING ||
                   motor->axis.state == NX_AXIS_STOPPING ||
                   motor->axis.state == NX_AXIS_HOLDING;
    bool faulted =
        motor->axis.state == NX_AXIS_FAULT || stage.fault != NX_SUCCESS ||
        stage.quarantined || adapter->fault != NX_SUCCESS ||
        queried != NX_SUCCESS ||
        (motor->primary != NX_SUCCESS &&
         !(adapter->stop_requested && motor->primary == NX_ERROR_CANCELLED));
    int8_t mode = 0;
    if (record->command_sequence != 0U) {
        if (motor->axis.command.mode == NX_AXIS_VELOCITY)
            mode = NX_CIA402_PROFILE_VELOCITY;
        else if (motor->axis.command.mode == NX_AXIS_POSITION)
            mode = NX_CIA402_PROFILE_POSITION;
    }
    adapter->adopted_command_sequence = record->command_sequence;
    nx_cia402_feedback_t result = {
        .velocity_rad_s = record->mechanical.velocity_rad_s,
        .mode = mode,
        .valid = valid,
        .ready = valid && motor->axis.state != NX_AXIS_DISABLED && !faulted,
        .voltage_enabled = motor->borrowed && stage.running,
        .operation_enabled = motor->borrowed && stage.running && running &&
                             record->command_sequence != 0U,
        .quick_stopping = motor->axis.state == NX_AXIS_STOPPING,
        .limited = motor->axis_output.requested_current_a !=
                   motor->axis_output.current_a,
        .fault = faulted,
        .reset_permitted =
            reset_permitted && !motor->borrowed && !stage.quarantined};
    float target = adapter->velocity != NULL
                       ? (float)adapter->velocity->core.target_velocity *
                             adapter->velocity->core.config.velocity_unit_rad_s
                       : 0.0F;
    bool matching_target =
        adapter->velocity != NULL && valid && adapter->command_sequence != 0U &&
        record->command_sequence == adapter->command_sequence &&
        motor->axis.command.sequence == adapter->command_sequence &&
        motor->axis.command.velocity_rad_s == target;
    if (!matching_target)
        adapter->dwelling = false;
    if (adapter->velocity != NULL &&
        record->mechanical.sequence != adapter->observed_sequence) {
        bool arrived = matching_target &&
                       fabsf(record->mechanical.velocity_rad_s - target) <=
                           adapter->policy.velocity_window_rad_s;
        if (!arrived)
            adapter->dwelling = false;
        else if (!adapter->dwelling) {
            adapter->dwelling = true;
            adapter->dwell_since = record->mechanical.sampled_at;
        }
    }
    result.target_reached =
        matching_target && adapter->dwelling &&
        record->mechanical.sampled_at - adapter->dwell_since >=
            adapter->policy.velocity_dwell_us;
    return result;
}

static nx_result_t stage_command(example_cia402_motor_t* adapter,
                                 nx_time_us_t now, nx_axis_mode_t mode,
                                 float target, uint32_t generation) {
    example_motor_drive_t* motor = adapter->motor;
    /* A replacement supersedes an unadmitted candidate even when its own
     * original lease has expired; never fall back to that old move. */
    adapter->pending = false;
    nx_time_us_t expiry;
    nx_result_t result = nx_deadline_finite_after(
        adapter->received_at, adapter->policy.command_lifetime_us, &expiry);
    if (result != NX_SUCCESS)
        return result;
    expiry = expiry < adapter->policy.authority_until
                 ? expiry
                 : adapter->policy.authority_until;
    if (expiry <= now)
        return NX_ERROR_TIMEOUT;
    uint32_t previous =
        adapter->command_sequence > motor->axis.accepted_sequence
            ? adapter->command_sequence
            : motor->axis.accepted_sequence;
    if (previous == UINT32_MAX)
        return NX_ERROR_EXHAUSTED;
    adapter->pending_command = (nx_axis_command_t){
        .mode = mode,
        .clock_domain = motor->axis.config.clock_domain,
        .epoch = motor->axis.config.epoch,
        .sequence = previous + 1U,
        .received_at = adapter->received_at,
        .valid_until = expiry,
        .position_rad = mode == NX_AXIS_POSITION ? target : 0.0F,
        .velocity_rad_s = mode == NX_AXIS_VELOCITY ? target : 0.0F};
    adapter->pending_generation = generation;
    adapter->pending_axis_sequence = previous + 1U;
    adapter->pending = true;
    return NX_SUCCESS;
}

static void admit(example_cia402_motor_t* adapter, nx_time_us_t now) {
    if (!adapter->pending)
        return;
    nx_result_t result = example_motor_drive_accept(
        adapter->motor, &adapter->pending_command, now);
    adapter->command_result = result;
    if (result == NX_ERROR_BUSY)
        return;
    adapter->pending = false;
    if (result == NX_SUCCESS)
        adapter->command_sequence = adapter->pending_command.sequence;
    else if (adapter->position != NULL)
        adapter->rejected_generation = adapter->pending_generation;
}

static void velocity_request(example_cia402_motor_t* adapter, nx_time_us_t now,
                             const nx_cia402_action_t* action) {
    if (adapter->wire_generation != adapter->handled_wire_generation) {
        nx_cia402_core_t* selected = &adapter->velocity->core;
        if (adapter->received_mode != NX_CIA402_PROFILE_VELOCITY ||
            adapter->received_controlword != selected->controlword ||
            adapter->received_target != selected->target_velocity) {
            adapter->command_result = NX_ERROR_STATE;
            adapter->pending = false;
        } else {
            adapter->command_result =
                stage_command(adapter, now, NX_AXIS_VELOCITY,
                              action->velocity_rad_s, adapter->wire_generation);
        }
        adapter->handled_wire_generation = adapter->wire_generation;
    }
    admit(adapter, now);
}

static void position_request(example_cia402_motor_t* adapter, nx_time_us_t now,
                             const nx_cia402_position_action_t* action) {
    if (action->new_setpoint &&
        action->generation != adapter->pending_generation) {
        adapter->pending = false;
        bool receipt =
            adapter->wire_generation != adapter->handled_wire_generation &&
            adapter->received_mode == NX_CIA402_PROFILE_POSITION &&
            adapter->received_controlword ==
                adapter->position->core.controlword &&
            adapter->received_target == adapter->position->target_position;
        adapter->command_result =
            receipt ? stage_command(adapter, now, NX_AXIS_POSITION,
                                    action->position_rad, action->generation)
                    : NX_ERROR_STATE;
        adapter->handled_wire_generation = adapter->wire_generation;
        if (adapter->command_result != NX_SUCCESS)
            adapter->rejected_generation = action->generation;
    }
    admit(adapter, now);
}

nx_result_t example_cia402_motor_step(example_cia402_motor_t* adapter,
                                      nx_time_us_t tx_deadline,
                                      bool reset_permitted) {
    if (adapter == NULL || !adapter->initialized || adapter->closing ||
        tx_deadline == NX_DEADLINE_NEVER)
        return NX_ERROR_INVALID;
    nx_time_us_t now = adapter->clock.read(adapter->clock.context);
    if (now == NX_DEADLINE_NEVER || tx_deadline <= now)
        return NX_ERROR_TIMEOUT;
    if (adapter->motor->borrowed) {
        nx_result_t progress = example_motor_drive_step(adapter->motor, now);
        if (progress != NX_SUCCESS && progress != NX_ERROR_BUSY)
            adapter->fault = progress;
    }
    nx_result_t result =
        nx_canopen_step(adapter->node, tx_deadline, &adapter->protocol);
    if (result != NX_SUCCESS ||
        adapter->protocol.reset != NX_CANOPEN_RESET_NONE ||
        adapter->protocol.closing) {
        adapter->reset_requested |=
            adapter->protocol.reset != NX_CANOPEN_RESET_NONE;
        adapter->fault = result == NX_SUCCESS ? NX_ERROR_CANCELLED : result;
        withdraw(adapter);
        return result;
    }
    adapter->receive_operational =
        adapter->protocol.nmt == NX_CANOPEN_OPERATIONAL &&
        adapter->protocol.peer != NX_CANOPEN_PEER_TIMEOUT &&
        adapter->protocol.error == NX_SUCCESS;
    if (!adapter->receive_operational) {
        adapter->pending = false;
        adapter->handled_wire_generation = adapter->wire_generation;
    }
    const nx_cia402_core_t* selected = core(adapter);
    const bool requests_operation =
        (selected->controlword & 0x018fU) == 0x000fU &&
        selected->requested_mode == selected->selected_mode;
    if (adapter->operation_seen &&
        (!adapter->receive_operational || !requests_operation))
        controlled_stop(adapter);
    now = adapter->clock.read(adapter->clock.context);
    if (now >= adapter->policy.authority_until) {
        adapter->command_result = NX_ERROR_TIMEOUT;
        controlled_stop(adapter);
    }
    const nx_drive_feedback_t* record = &adapter->motor->last_feedback;
    nx_cia402_feedback_t actual = feedback(adapter, now, reset_permitted);
    if (!adapter->motor->have_feedback && !actual.fault)
        return NX_ERROR_BUSY;
    const bool repeated =
        adapter->position != NULL &&
        adapter->observed_sequence == record->mechanical.sequence;
    if (repeated && actual.valid && !actual.fault)
        return NX_ERROR_BUSY;
    if (repeated)
        actual.valid = false;
    nx_cia402_action_t action;
    nx_cia402_position_action_t point = {0};
    if (adapter->velocity != NULL) {
        result = nx_cia402_update(adapter->velocity, &adapter->protocol,
                                  &actual, &action);
    } else {
        if (adapter->pending_axis_sequence != 0U &&
            adapter->command_sequence == adapter->pending_axis_sequence &&
            record->command_sequence == adapter->pending_axis_sequence)
            adapter->accepted_generation = adapter->pending_generation;
        nx_cia402_position_feedback_t observation = {
            .axis = actual,
            .position_rad = record->mechanical.position_rad,
            .observed_at = record->mechanical.sampled_at,
            .sequence = record->mechanical.sequence,
            .accepted_generation = adapter->accepted_generation,
            .rejected_generation = adapter->rejected_generation,
            .acknowledgement_epoch = adapter->position->config.epoch,
            .position_valid = actual.valid};
        result = nx_cia402_position_update(
            adapter->position, &adapter->protocol, &observation, &point);
        action = point.axis;
    }
    adapter->observed_sequence = record->mechanical.sequence;
    if (result != NX_SUCCESS)
        return result;
    if (action.reset_fault) {
        adapter->reset_requested = true;
        withdraw(adapter);
    } else if (core(adapter)->fault_latched) {
        withdraw(adapter);
    } else if (now >= adapter->policy.authority_until || action.halt ||
               action.quick_stop ||
               (adapter->operation_seen && !action.enable_operation)) {
        controlled_stop(adapter);
    } else if (core(adapter)->state == NX_CIA402_OPERATION_ENABLED &&
               action.enable_operation && !adapter->stop_requested) {
        adapter->operation_seen = true;
        if (adapter->velocity != NULL)
            velocity_request(adapter, now, &action);
        else
            position_request(adapter, now, &point);
    }
    return NX_SUCCESS;
}

nx_result_t example_cia402_motor_stop(example_cia402_motor_t* adapter) {
    if (adapter == NULL || !adapter->initialized)
        return NX_ERROR_INVALID;
    adapter->closing = true;
    withdraw(adapter);
    nx_result_t result = nx_canopen_stop(adapter->node);
    if (result != NX_SUCCESS)
        return result;
    if (adapter->motor->borrowed)
        return NX_ERROR_BUSY;
    result = adapter->velocity != NULL
                 ? nx_cia402_release(adapter->velocity)
                 : nx_cia402_position_release(adapter->position);
    if (result == NX_SUCCESS)
        adapter->initialized = false;
    return result;
}
