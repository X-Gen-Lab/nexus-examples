/**
 * \file            cia402_motor_test.cpp
 * \brief           Real CANopen-to-Axis-to-PMSM consumer and receipt contracts
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-24
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "cia402_motor.h"
#include <algorithm>
#include <array>
#include <deque>
#include <gtest/gtest.h>
#include <vector>

namespace {
struct CanWire {
    nx_time_us_t now = 1000;
    std::deque<nx_can_event_t> received;
    std::vector<nx_can_frame_t> transmitted;
    nx_can_tx_request_t* pending = nullptr;
    bool drain = true;
    bool bus_off = false;
    static nx_time_us_t clock(void* context) {
        return static_cast<CanWire*>(context)->now;
    }
    static nx_result_t submit(void* context, nx_can_tx_request_t* request) {
        auto& wire = *static_cast<CanWire*>(context);
        if (wire.pending != nullptr)
            return NX_ERROR_BUSY;
        const auto result = nx_request_admit(&request->base, NX_REQUEST_ACTIVE);
        if (result == NX_SUCCESS) {
            wire.pending = request;
            wire.transmitted.push_back(request->frame);
        }
        return result;
    }
    static nx_result_t cancel(void* context, nx_can_tx_request_t* request) {
        auto& wire = *static_cast<CanWire*>(context);
        if (wire.pending != request)
            return NX_ERROR_STATE;
        if (!wire.drain)
            return NX_ERROR_BUSY;
        wire.pending = nullptr;
        nx_request_settle(&request->base, NX_ERROR_CANCELLED, 0);
        return NX_SUCCESS;
    }
    static nx_result_t service(void* context) {
        auto& wire = *static_cast<CanWire*>(context);
        if (wire.pending != nullptr && wire.drain) {
            auto* request = wire.pending;
            wire.pending = nullptr;
            nx_request_settle(&request->base, NX_SUCCESS, request->frame.dlc);
        }
        return NX_SUCCESS;
    }
    static nx_result_t read(void* context, nx_can_event_t* events,
                            size_t capacity, size_t* count) {
        auto& wire = *static_cast<CanWire*>(context);
        *count = 0;
        if (capacity > 0 && !wire.received.empty()) {
            events[0] = wire.received.front();
            wire.received.pop_front();
            *count = 1;
        }
        return NX_SUCCESS;
    }
    static nx_result_t state(void* context, nx_can_state_t* out) {
        *out = {};
        out->started = true;
        out->bus_off = static_cast<const CanWire*>(context)->bus_off;
        return NX_SUCCESS;
    }
    const nx_can_ops_t operations = {submit, cancel,  service, read,
                                     state,  nullptr, nullptr, nullptr};
    nx_can_port_t port = {&operations, this};
    void push(uint32_t id, std::initializer_list<uint8_t> bytes,
              nx_time_us_t timestamp = 0) {
        nx_can_event_t event{};
        event.kind = NX_CAN_EVENT_FRAME;
        event.timestamp_us = timestamp != 0 ? timestamp : now;
        event.frame = {id,
                       NX_CAN_STANDARD,
                       NX_CAN_DATA,
                       static_cast<uint8_t>(bytes.size()),
                       {}};
        std::copy(bytes.begin(), bytes.end(), event.frame.data);
        received.push_back(event);
    }
    const nx_can_frame_t* last(uint32_t id) const {
        for (auto at = transmitted.rbegin(); at != transmitted.rend(); ++at)
            if (at->id == id)
                return &*at;
        return nullptr;
    }
};
struct StageWire {
    nx_power_stage_session_t* active = nullptr;
    nx_power_stage_plan_t applied{};
    nx_power_stage_state_t facts{};
    nx_result_t fault = NX_SUCCESS;
    uint64_t cycles = 0;
    unsigned starts = 0;
    bool drain = true;
    static nx_result_t start(void* context, nx_power_stage_session_t* session) {
        auto& stage = *static_cast<StageWire*>(context);
        if (stage.active != nullptr)
            return NX_ERROR_BUSY;
        const auto result = nx_request_admit(&session->base, NX_REQUEST_ACTIVE);
        if (result == NX_SUCCESS) {
            stage.active = session;
            stage.applied = session->initial;
            stage.facts.running = true;
            ++stage.starts;
        }
        return result;
    }
    static nx_result_t service(void*) {
        return NX_ERROR_BUSY;
    }
    static nx_result_t stop(void* context) {
        auto& stage = *static_cast<StageWire*>(context);
        if (stage.active == nullptr)
            return NX_ERROR_STATE;
        stage.facts.running = false;
        if (!stage.drain) {
            (void)nx_request_transition(&stage.active->base,
                                        NX_REQUEST_QUARANTINED);
            stage.facts.quarantined = true;
            return NX_ERROR_IO;
        }
        auto* session = stage.active;
        stage.active = nullptr;
        stage.facts.quarantined = false;
        nx_request_settle(&session->base, stage.fault, 0);
        return NX_SUCCESS;
    }
    static nx_result_t state(const void* context, nx_power_stage_state_t* out) {
        const auto& stage = *static_cast<const StageWire*>(context);
        *out = stage.facts;
        out->fault = stage.fault;
        out->completed_cycles = stage.cycles;
        return NX_SUCCESS;
    }
    const nx_power_stage_ops_t operations = {start, service, stop, state};
    nx_power_stage_t port = {&operations, this};
    StageWire() {
        facts.clock_epoch = 9;
        facts.tick_hz = 1000000;
        facts.period_ticks = 1000;
        facts.trigger_ticks = 800;
        facts.minimum_duty_ticks = 50;
        facts.maximum_duty_ticks = 700;
        facts.scan_span_us = 3;
        facts.capture_begin_ticks = 730;
        facts.capture_end_ticks = 1000;
        facts.deadtime_ticks = 1;
        facts.settling_ticks = 25;
        facts.aperture_ticks = 3;
        facts.conversion_ticks = 3;
        facts.irq_ticks = 20;
        facts.compute_ticks = 100;
        facts.commit_before_ticks = 990;
        facts.current_count = 3;
        facts.current_phase_mask = 7;
        facts.bus_available = true;
        facts.encoder_available = true;
    }
};
class CiaMotor : public testing::Test {
  protected:
    CanWire wire;
    StageWire stage;
    example_motor_drive_t motor{};
    example_cia402_motor_t adapter{};
    nx_cia402_t velocity{};
    nx_cia402_position_t position{};
    nx_canopen_t node{};
    std::array<nx_canopen_tx_slot_t, 8> tx{};
    std::vector<max_align_t> workspace;
    nx_time_us_t cycle_start = 1000;
    uint32_t encoder = 100;
    bool pp = false;
    void initialize(bool position_mode = false, bool warm = true) {
        pp = position_mode;
        auto config = example_motor_drive_reference();
        config.commissioned = true;
        config.run_duration_us = 1000000;
        config.initial_mode = pp ? NX_AXIS_POSITION : NX_AXIS_VELOCITY;
        config.axis_parameters.velocity_limit_rad_s = 10;
        config.axis_parameters.acceleration_rad_s2 = 20;
        ASSERT_EQ(example_motor_drive_initialize(&motor, &stage.port, &config),
                  NX_SUCCESS);
        ASSERT_EQ(example_motor_drive_start(&motor, wire.now), NX_SUCCESS);
        if (warm) {
            for (unsigned cycle = 0; cycle != 6; ++cycle) {
                ASSERT_EQ(fast_cycle(), NX_SUCCESS);
            }
        }
    }
    nx_result_t bind(float acceleration = 20, uint32_t lifetime_us = 50000,
                     uint32_t authority_us = 500000) {
        const nx_cia402_config_t pv{0.01F, 1000U,
                                    static_cast<uint32_t>(acceleration * 100),
                                    2000U, 2000U};
        nx_canopen_config_t transport{};
        transport.port = &wire.port;
        transport.clock = {CanWire::clock, &wire};
        transport.tx_slots = tx.data();
        transport.tx_capacity = tx.size();
        transport.receive_budget = 4;
        transport.maximum_step_us = 100000;
        transport.producer_heartbeat_ms = 100;
        transport.consumer_heartbeat_ms = 100;
        transport.sdo_timeout_ms = 200;
        transport.node_id = 18;
        transport.peer_node_id = 19;
        const size_t count = pp ? (size_t)NX_CIA402_POSITION_OBJECT_COUNT
                                : (size_t)NX_CIA402_OBJECT_COUNT;
        const auto bytes = nx_canopen_workspace_bytes(count);
        workspace.resize((bytes + sizeof(max_align_t) - 1) /
                         sizeof(max_align_t));
        const example_cia402_motor_policy_t policy{wire.now + authority_us,
                                                   lifetime_us, 0.05F, 5000U};
        if (!pp) {
            if (nx_cia402_initialize(&velocity, &pv) != NX_SUCCESS)
                return NX_ERROR_INVALID;
            return example_cia402_motor_bind_velocity(
                &adapter, &motor, &velocity, &node, &transport, &policy,
                workspace.data(), workspace.size() * sizeof(max_align_t));
        }
        nx_cia402_position_config_t selected{};
        selected.axis = pv;
        selected.position_unit_rad = 0.001F;
        selected.minimum_position = -90000;
        selected.maximum_position = 90000;
        selected.profile_velocity = 1000;
        selected.position_window = 5;
        selected.position_window_ms = 5;
        selected.velocity_window_rad_s = 0.05F;
        selected.epoch = 42;
        if (nx_cia402_position_initialize(&position, &selected) != NX_SUCCESS)
            return NX_ERROR_INVALID;
        return example_cia402_motor_bind_position(
            &adapter, &motor, &position, &node, &transport, &policy,
            workspace.data(), workspace.size() * sizeof(max_align_t));
    }
    nx_result_t fast_cycle() {
        if (stage.active == nullptr)
            return NX_ERROR_STATE;
        nx_power_stage_sample_t sample{};
        for (auto& code : sample.current_raw)
            code = 2048;
        sample.current_count = 3;
        sample.current_phase_mask = stage.applied.current_phase_mask;
        sample.bus_valid = true;
        sample.encoder_valid = true;
        sample.bus_raw = 2400;
        sample.encoder_count = encoder;
        sample.plan_sequence = stage.applied.sequence;
        sample.sequence = stage.cycles;
        sample.clock_epoch = 9;
        sample.trigger_ticks = stage.applied.trigger_ticks;
        sample.sample_at = cycle_start + sample.trigger_ticks;
        sample.observed_at = sample.sample_at + 3;
        sample.angle_at = sample.observed_at;
        sample.planned_apply_at = cycle_start + 1000;
        sample.scan_span_us = 3;
        nx_power_stage_plan_t next{};
        const auto status =
            stage.active->compute(stage.active->context, &sample, &next);
        wire.now = sample.observed_at;
        cycle_start += 1000;
        if (status != NX_SUCCESS) {
            stage.fault = status;
            return status;
        }
        stage.applied = next;
        ++stage.cycles;
        const auto task = example_motor_drive_step(&motor, wire.now);
        return task == NX_ERROR_BUSY ? NX_SUCCESS : task;
    }
    nx_result_t step(bool advance = true) {
        if (advance && motor.borrowed) {
            const auto fast = fast_cycle();
            if (fast != NX_SUCCESS)
                return fast;
        }
        return example_cia402_motor_step(&adapter, wire.now + 50000, false);
    }
    void target(uint16_t control, int32_t value, nx_time_us_t timestamp = 0) {
        const auto raw = static_cast<uint32_t>(value);
        wire.push(
            0x212U,
            {static_cast<uint8_t>(control), static_cast<uint8_t>(control >> 8U),
             static_cast<uint8_t>(pp ? 1 : 3), static_cast<uint8_t>(raw),
             static_cast<uint8_t>(raw >> 8U), static_cast<uint8_t>(raw >> 16U),
             static_cast<uint8_t>(raw >> 24U)},
            timestamp);
    }
    void enable() {
        ASSERT_EQ(step(), NX_SUCCESS);
        wire.push(0, {1, 18});
        ASSERT_EQ(step(), NX_SUCCESS);
        target(6, 0);
        ASSERT_EQ(step(), NX_SUCCESS);
        target(7, 0);
        ASSERT_EQ(step(), NX_SUCCESS);
        target(15, 0);
        ASSERT_EQ(step(), NX_SUCCESS);
        EXPECT_EQ(pp ? position.core.state : velocity.core.state,
                  NX_CIA402_OPERATION_ENABLED);
    }
    void TearDown() override {
        if (adapter.initialized) {
            EXPECT_EQ(example_cia402_motor_stop(&adapter), NX_SUCCESS);
        }
        if (motor.borrowed) {
            EXPECT_EQ(example_motor_drive_stop(&motor), NX_SUCCESS);
        }
    }
};
TEST_F(CiaMotor, RealVelocityPdoReachesAxisAndActualFastFeedback) {
    initialize();
    ASSERT_EQ(bind(), NX_SUCCESS);
    enable();
    target(15, 200);
    const auto receipt = wire.now;
    ASSERT_EQ(step(), NX_SUCCESS);
    EXPECT_EQ(adapter.command_result, NX_SUCCESS);
    EXPECT_EQ(motor.axis.pending_command.received_at, receipt);
    EXPECT_FLOAT_EQ(motor.axis.pending_command.velocity_rad_s, 2);
    const auto sequence = motor.axis.accepted_sequence;
    for (unsigned cycle = 0; cycle != 4; ++cycle)
        ASSERT_EQ(step(), NX_SUCCESS);
    EXPECT_EQ(motor.last_feedback.command_sequence, sequence);
    EXPECT_EQ(adapter.adopted_command_sequence, sequence);
    EXPECT_NE(stage.applied.duty_ticks[0], stage.applied.duty_ticks[1]);
}
TEST_F(CiaMotor, PollingDoesNotRefreshAuthorityButANewIdenticalPdoIsAReceipt) {
    initialize();
    ASSERT_EQ(bind(), NX_SUCCESS);
    enable();
    target(15, 100);
    ASSERT_EQ(step(), NX_SUCCESS);
    const auto sequence = motor.axis.accepted_sequence;
    const auto expiry = motor.axis.pending_command.valid_until;
    const auto receipt = motor.axis.pending_command.received_at;
    for (unsigned cycle = 0; cycle != 5; ++cycle)
        ASSERT_EQ(step(), NX_SUCCESS);
    EXPECT_EQ(motor.axis.accepted_sequence, sequence);
    EXPECT_EQ(motor.axis.command.valid_until, expiry);
    EXPECT_EQ(motor.axis.command.received_at, receipt);
    target(15, 100);
    ASSERT_EQ(step(), NX_SUCCESS);
    EXPECT_GT(motor.axis.accepted_sequence, sequence);
    EXPECT_GT(motor.axis.pending_command.received_at, receipt);
}
TEST_F(CiaMotor, HeartbeatAndSdoStagingDoNotCreateMotionReceipts) {
    initialize();
    ASSERT_EQ(bind(), NX_SUCCESS);
    enable();
    const auto sequence = motor.axis.accepted_sequence;
    wire.push(0x713U, {5});
    wire.push(0x612U, {0x23, 0xff, 0x60, 0, 0xc8, 0, 0, 0});
    ASSERT_EQ(step(), NX_SUCCESS);
    EXPECT_EQ(velocity.core.target_velocity, 200);
    EXPECT_EQ(motor.axis.accepted_sequence, sequence);
}
TEST_F(CiaMotor, ExpiredQueuedPdoCannotAcquireAFreshLocalDeadline) {
    initialize();
    ASSERT_EQ(bind(20, 5000), NX_SUCCESS);
    enable();
    const auto sequence = motor.axis.accepted_sequence;
    target(15, 100, wire.now - 6000);
    (void)step();
    EXPECT_EQ(adapter.command_result, NX_ERROR_TIMEOUT);
    EXPECT_EQ(motor.axis.accepted_sequence, sequence);
}
TEST_F(CiaMotor, ActualTpdoUsesSensorFeedbackInsteadOfTheVelocityTarget) {
    initialize();
    ASSERT_EQ(bind(), NX_SUCCESS);
    enable();
    target(15, 200);
    ASSERT_EQ(step(), NX_SUCCESS);
    ASSERT_EQ(nx_canopen_request_tpdo(&node), NX_SUCCESS);
    ASSERT_EQ(step(), NX_SUCCESS);
    const auto* frame = wire.last(0x192U);
    ASSERT_NE(frame, nullptr);
    EXPECT_EQ(frame->dlc, 7);
    EXPECT_EQ(frame->data[2], 3);
    for (unsigned i = 3; i != 7; ++i)
        EXPECT_EQ(frame->data[i], 0);
    EXPECT_EQ(velocity.core.actual_velocity, 0);
    EXPECT_EQ(velocity.core.target_velocity, 200);
}
TEST_F(CiaMotor, PositionAckWaitsForTheMatchingRealFastCommandReceipt) {
    initialize(true);
    ASSERT_EQ(bind(), NX_SUCCESS);
    enable();
    target(0x003f, 1000);
    ASSERT_EQ(step(), NX_SUCCESS);
    EXPECT_EQ(position.core.statusword & 0x1000U, 0U);
    const auto sequence = motor.axis.accepted_sequence;
    EXPECT_EQ(adapter.pending_axis_sequence, sequence);
    EXPECT_EQ(step(false), NX_ERROR_BUSY);
    EXPECT_EQ(position.core.statusword & 0x1000U, 0U);
    for (unsigned cycle = 0; cycle != 4; ++cycle)
        ASSERT_EQ(step(), NX_SUCCESS);
    EXPECT_EQ(motor.last_feedback.command_sequence, sequence);
    EXPECT_NE(position.core.statusword & 0x1000U, 0U);
    EXPECT_EQ(position.actual_position, 0);
    EXPECT_EQ(position.core.statusword & 0x0400U, 0U);
    const auto expiry = motor.axis.command.valid_until;
    target(0x003f, 1000);
    ASSERT_EQ(step(), NX_SUCCESS);
    EXPECT_EQ(motor.axis.accepted_sequence, sequence);
    EXPECT_EQ(motor.axis.command.valid_until, expiry);
}
TEST_F(CiaMotor, ProtocolResetWithdrawsWithoutAutomaticRestart) {
    initialize();
    ASSERT_EQ(bind(), NX_SUCCESS);
    enable();
    const auto starts = stage.starts;
    wire.push(0, {0x81, 18});
    (void)step();
    EXPECT_TRUE(adapter.reset_requested);
    EXPECT_FALSE(motor.borrowed);
    EXPECT_EQ(stage.starts, starts);
}
TEST_F(CiaMotor, FixedRampMismatchRejectsBeforeBindingOrTransmission) {
    initialize();
    EXPECT_EQ(bind(30), NX_ERROR_UNSUPPORTED);
    EXPECT_EQ(node.state, nullptr);
    EXPECT_TRUE(wire.transmitted.empty());
}
TEST_F(CiaMotor, MissingRealFeedbackIsBusyAndDoesNotAdvertiseActualMode) {
    initialize(false, false);
    ASSERT_EQ(bind(), NX_SUCCESS);
    EXPECT_EQ(step(false), NX_ERROR_BUSY);
    EXPECT_EQ(velocity.core.actual_mode, 0);
    EXPECT_EQ(velocity.core.state, NX_CIA402_SWITCH_ON_DISABLED);
    EXPECT_EQ(stage.starts, 1U);
}
TEST_F(CiaMotor, ExpiredReplacementDoesNotAdmitAnOlderBusyCandidate) {
    initialize();
    ASSERT_EQ(bind(20, 10000), NX_SUCCESS);
    enable();
    ASSERT_EQ(fast_cycle(), NX_SUCCESS);
    auto parameters = motor.axis.parameters;
    ++parameters.revision;
    ASSERT_EQ(nx_axis_stage_parameters(&motor.axis, &parameters), NX_SUCCESS);
    target(15, 100);
    ASSERT_EQ(step(false), NX_SUCCESS);
    EXPECT_EQ(adapter.command_result, NX_ERROR_BUSY);
    EXPECT_TRUE(adapter.pending);
    const auto sequence = motor.axis.accepted_sequence;
    ASSERT_EQ(fast_cycle(), NX_SUCCESS);
    target(15, 200, wire.now - 11000);
    ASSERT_EQ(step(false), NX_SUCCESS);
    EXPECT_EQ(adapter.command_result, NX_ERROR_TIMEOUT);
    EXPECT_FALSE(adapter.pending);
    EXPECT_EQ(motor.axis.accepted_sequence, sequence);
}
TEST_F(CiaMotor, ChangedStagingTargetCannotReuseAnOldReachedQualification) {
    initialize();
    ASSERT_EQ(bind(), NX_SUCCESS);
    enable();
    for (unsigned cycle = 0; cycle != 8; ++cycle) {
        ASSERT_EQ(step(), NX_SUCCESS);
    }
    ASSERT_NE(velocity.core.statusword & 0x0400U, 0U);
    wire.push(0x612U, {0x23, 0xff, 0x60, 0, 0xc8, 0, 0, 0});
    ASSERT_EQ(step(false), NX_SUCCESS);
    EXPECT_EQ(velocity.core.target_velocity, 200);
    EXPECT_EQ(velocity.core.statusword & 0x0400U, 0U);
}
TEST_F(CiaMotor, FailedDrainRetainsBothLoansUntilExplicitRetryJoins) {
    initialize();
    ASSERT_EQ(bind(), NX_SUCCESS);
    enable();
    wire.drain = false;
    ASSERT_EQ(nx_canopen_request_tpdo(&node), NX_SUCCESS);
    ASSERT_EQ(step(), NX_SUCCESS);
    ASSERT_NE(wire.pending, nullptr);
    stage.drain = false;
    EXPECT_NE(example_cia402_motor_stop(&adapter), NX_SUCCESS);
    EXPECT_TRUE(adapter.initialized);
    EXPECT_TRUE(motor.borrowed);
    EXPECT_NE(node.state, nullptr);
    EXPECT_EQ(nx_request_state(&motor.session.base), NX_REQUEST_QUARANTINED);
    wire.drain = true;
    stage.drain = true;
    EXPECT_EQ(example_cia402_motor_stop(&adapter), NX_SUCCESS);
    EXPECT_FALSE(adapter.initialized);
    EXPECT_FALSE(motor.borrowed);
    EXPECT_EQ(node.state, nullptr);
    EXPECT_EQ(wire.pending, nullptr);
}
TEST_F(CiaMotor, PositionHardwareFaultDoesNotWaitForAnotherMotionSample) {
    initialize(true);
    ASSERT_EQ(bind(), NX_SUCCESS);
    enable();
    stage.fault = NX_ERROR_IO;
    stage.facts.running = false;
    EXPECT_EQ(step(false), NX_SUCCESS);
    EXPECT_EQ(position.core.state, NX_CIA402_FAULT);
    EXPECT_FALSE(motor.borrowed);
}
TEST_F(CiaMotor, BusOffWithdrawsVelocityAndKeepsTheOriginalFault) {
    initialize();
    ASSERT_EQ(bind(), NX_SUCCESS);
    enable();
    const auto starts = stage.starts;
    const auto samples = stage.cycles;
    wire.bus_off = true;
    EXPECT_EQ(step(false), NX_ERROR_IO);
    EXPECT_EQ(velocity.core.state, NX_CIA402_FAULT);
    EXPECT_FALSE(adapter.receive_operational);
    EXPECT_FALSE(motor.borrowed);
    wire.bus_off = false;
    EXPECT_EQ(step(false), NX_ERROR_STATE);
    EXPECT_EQ(adapter.fault, NX_ERROR_IO);
    EXPECT_EQ(stage.starts, starts);
    EXPECT_EQ(stage.cycles, samples);
}
TEST_F(CiaMotor, BusOffClearsPositionAcknowledgementWithoutAnotherSample) {
    initialize(true);
    ASSERT_EQ(bind(), NX_SUCCESS);
    enable();
    target(0x003f, 1000);
    ASSERT_EQ(step(), NX_SUCCESS);
    for (unsigned cycle = 0; cycle != 4; ++cycle)
        ASSERT_EQ(step(), NX_SUCCESS);
    ASSERT_NE(position.core.statusword & 0x1000U, 0U);
    wire.bus_off = true;
    EXPECT_EQ(step(false), NX_ERROR_IO);
    EXPECT_EQ(position.core.state, NX_CIA402_FAULT);
    EXPECT_EQ(position.core.statusword & 0x1400U, 0U);
    EXPECT_FALSE(motor.borrowed);
}
TEST_F(CiaMotor, PreoperationalIgnoredPdoCannotBeRevivedBySdoStaging) {
    initialize();
    ASSERT_EQ(bind(), NX_SUCCESS);
    ASSERT_EQ(step(), NX_SUCCESS);
    target(15, 200);
    ASSERT_EQ(step(), NX_SUCCESS);
    EXPECT_EQ(velocity.core.target_velocity, 0);
    const auto sequence = motor.axis.accepted_sequence;
    wire.push(0, {1, 18});
    ASSERT_EQ(step(), NX_SUCCESS);
    wire.push(0x612U, {0x2f, 0x60, 0x60, 0, 3, 0, 0, 0});
    ASSERT_EQ(step(), NX_SUCCESS);
    wire.push(0x612U, {0x2b, 0x40, 0x60, 0, 6, 0, 0, 0});
    ASSERT_EQ(step(), NX_SUCCESS);
    wire.push(0x612U, {0x2b, 0x40, 0x60, 0, 7, 0, 0, 0});
    ASSERT_EQ(step(), NX_SUCCESS);
    wire.push(0x612U, {0x23, 0xff, 0x60, 0, 0xc8, 0, 0, 0});
    ASSERT_EQ(step(), NX_SUCCESS);
    wire.push(0x612U, {0x2b, 0x40, 0x60, 0, 15, 0, 0, 0});
    ASSERT_EQ(step(), NX_SUCCESS);
    EXPECT_EQ(velocity.core.state, NX_CIA402_OPERATION_ENABLED);
    EXPECT_EQ(motor.axis.accepted_sequence, sequence);
    EXPECT_EQ(adapter.command_sequence, 0U);
}
TEST_F(CiaMotor, PositionAuthorityExpiresWithoutWaitingForANewSample) {
    initialize(true);
    ASSERT_EQ(bind(20, 50000, 6500), NX_SUCCESS);
    enable();
    wire.now = adapter.policy.authority_until;
    EXPECT_EQ(step(false), NX_ERROR_BUSY);
    EXPECT_TRUE(adapter.stop_requested);
    EXPECT_EQ(motor.axis.state, NX_AXIS_STOPPING);
}
TEST_F(CiaMotor, PositionNmtStopDoesNotWaitForAnotherMotionSample) {
    initialize(true);
    ASSERT_EQ(bind(), NX_SUCCESS);
    enable();
    wire.push(0, {2, 18});
    EXPECT_EQ(step(false), NX_ERROR_BUSY);
    EXPECT_TRUE(adapter.stop_requested);
    EXPECT_EQ(motor.axis.state, NX_AXIS_STOPPING);
}
TEST_F(CiaMotor, PositionHaltDoesNotWaitForAnotherMotionSample) {
    initialize(true);
    ASSERT_EQ(bind(), NX_SUCCESS);
    enable();
    target(0x10f, 0);
    EXPECT_EQ(step(false), NX_ERROR_BUSY);
    EXPECT_TRUE(adapter.stop_requested);
    EXPECT_EQ(motor.axis.state, NX_AXIS_STOPPING);
}
TEST_F(CiaMotor, NmtInTheSameBatchInvalidatesTheEarlierMotionReceipt) {
    initialize();
    ASSERT_EQ(bind(), NX_SUCCESS);
    enable();
    const auto sequence = motor.axis.accepted_sequence;
    target(15, 200);
    wire.push(0, {1, 18});
    ASSERT_EQ(step(), NX_SUCCESS);
    EXPECT_EQ(adapter.protocol.nmt, NX_CANOPEN_OPERATIONAL);
    EXPECT_EQ(motor.axis.accepted_sequence, sequence);
}
TEST_F(CiaMotor, HardwareFaultBeforeFirstFeedbackStillWithdraws) {
    initialize(false, false);
    ASSERT_EQ(bind(), NX_SUCCESS);
    stage.fault = NX_ERROR_IO;
    stage.facts.running = false;
    EXPECT_EQ(step(false), NX_SUCCESS);
    EXPECT_EQ(velocity.core.state, NX_CIA402_FAULT);
    EXPECT_FALSE(motor.borrowed);
}
} /* namespace */
