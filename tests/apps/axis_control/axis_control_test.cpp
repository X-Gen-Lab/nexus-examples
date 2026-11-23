/**
 * \file            axis_control_test.cpp
 * \brief           Real single-axis algorithms and explicit hardware ownership
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-22
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "axis_control.h"
#include <array>
#include <cstring>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <limits>

namespace {
class Hardware {
  public:
    uint32_t raw{1000U};
    nx_time_us_t observed{1000U};
    MOCK_METHOD(nx_result_t, EncoderStart, ());
    MOCK_METHOD(nx_result_t, EncoderStop, ());
    MOCK_METHOD(nx_result_t, Read, (nx_encoder_sample_t*));
    MOCK_METHOD(nx_result_t, PwmStart, ());
    MOCK_METHOD(nx_result_t, PwmStop, ());
    MOCK_METHOD(nx_result_t, Commit, (const nx_pwm_group_frame_t*));
    static nx_result_t encoder_start(void* p) {
        return static_cast<Hardware*>(p)->EncoderStart();
    }
    static nx_result_t encoder_stop(void* p) {
        return static_cast<Hardware*>(p)->EncoderStop();
    }
    static nx_result_t read(void* p, nx_encoder_sample_t* value) {
        return static_cast<Hardware*>(p)->Read(value);
    }
    static nx_result_t pwm_start(void* p) {
        return static_cast<Hardware*>(p)->PwmStart();
    }
    static nx_result_t pwm_stop(void* p) {
        return static_cast<Hardware*>(p)->PwmStop();
    }
    static nx_result_t commit(void* p, const nx_pwm_group_frame_t* frame) {
        return static_cast<Hardware*>(p)->Commit(frame);
    }
};
const nx_encoder_ops_t encoder_ops{
    Hardware::encoder_start, Hardware::encoder_stop, Hardware::read, nullptr};
const nx_pwm_group_ops_t pwm_ops{Hardware::commit, Hardware::pwm_start,
                                 Hardware::pwm_stop, nullptr, nullptr};

class Axis : public testing::Test {
  protected:
    testing::NiceMock<Hardware> hardware;
    nx_encoder_t encoder{&encoder_ops, &hardware};
    nx_pwm_group_t pwm{&pwm_ops, &hardware};
    example_axis_t axis{};
    example_axis_config_t config{};
    example_axis_parameters_t parameters{};
    void SetUp() override {
        ON_CALL(hardware, EncoderStart())
            .WillByDefault(testing::Return(NX_SUCCESS));
        ON_CALL(hardware, EncoderStop())
            .WillByDefault(testing::Return(NX_SUCCESS));
        ON_CALL(hardware, PwmStart())
            .WillByDefault(testing::Return(NX_SUCCESS));
        ON_CALL(hardware, PwmStop()).WillByDefault(testing::Return(NX_SUCCESS));
        ON_CALL(hardware, Commit(testing::_))
            .WillByDefault(testing::Return(NX_SUCCESS));
        ON_CALL(hardware, Read(testing::_))
            .WillByDefault([this](nx_encoder_sample_t* value) {
                *value = {hardware.observed, hardware.raw};
                return NX_SUCCESS;
            });
        config.encoder = &encoder;
        config.pwm = &pwm;
        config.calibration = {1000.0F, 0.001F};
        config.encoder_modulus = 65536U;
        config.maximum_delta_counts = 100U;
        config.pwm_period_ticks = 1000U;
        config.trigger_ticks = 500U;
        config.maximum_step_gap_us = 20000U;
        config.hardware_identity = 47U;
        config.epoch = 3U;
        config.clock_domain = 1U;
        config.calibration_revision = 7U;
        parameters.revision = 1U;
        parameters.pid = {1, 0, 0, 0, -1, 1, -10, 10};
        parameters.minimum_position_rad = -2;
        parameters.maximum_position_rad = 2;
        parameters.maximum_velocity_rad_s = 1;
        parameters.acceleration_rad_s2 = 2;
        ASSERT_EQ(example_axis_initialize(&axis, &config, &parameters),
                  NX_SUCCESS);
    }
    example_axis_command_t command(uint32_t sequence = 1U) {
        return {EXAMPLE_AXIS_POSITION_RADIANS, 3U, sequence, 2000000U, 0.2F};
    }
    nx_result_t step(nx_time_us_t now) {
        example_axis_cycle_t cycle{};
        cycle.release.ticks = now;
        cycle.release.ticks_per_second = 1000000U;
        cycle.release.domain = config.clock_domain;
        cycle.release.epoch = config.epoch;
        cycle.release.source = NX_IO_TIMESTAMP_SERVICE_OBSERVATION;
        cycle.sequence = axis.sample_sequence + 1U;
        cycle.calibration_revision = config.calibration_revision;
        cycle.valid = true;
        hardware.observed = now;
        return example_axis_step(&axis, &cycle);
    }
    void start() {
        ASSERT_EQ(example_axis_start(&axis, 1000U), NX_SUCCESS);
    }
};

TEST_F(Axis, StartStagesNeutralBeforeEnablingOutputAndDoesNotAllocate) {
    testing::InSequence sequence;
    EXPECT_CALL(hardware, EncoderStart()).WillOnce(testing::Return(NX_SUCCESS));
    EXPECT_CALL(hardware, Read(testing::_))
        .WillOnce([this](nx_encoder_sample_t* value) {
            *value = {1000U, hardware.raw};
            return NX_SUCCESS;
        });
    EXPECT_CALL(hardware, Commit(testing::_))
        .WillOnce([](const nx_pwm_group_frame_t* frame) {
            EXPECT_EQ(frame->channel_count, 2U);
            EXPECT_EQ(frame->duty_ticks[0], 0U);
            EXPECT_EQ(frame->duty_ticks[1], 0U);
            return NX_SUCCESS;
        });
    EXPECT_CALL(hardware, PwmStart()).WillOnce(testing::Return(NX_SUCCESS));
    start();
    EXPECT_EQ(axis.state, EXAMPLE_AXIS_READY);
    EXPECT_FLOAT_EQ(axis.position_rad, 0);
}

TEST_F(Axis, RejectedTargetsPreserveLastSequenceAndNeverTouchHardware) {
    start();
    EXPECT_CALL(hardware, Commit(testing::_)).Times(0);
    auto target = command();
    target.epoch = 2U;
    EXPECT_EQ(example_axis_command(&axis, &target, 1000U), NX_ERROR_STATE);
    target = command();
    target.unit = EXAMPLE_AXIS_UNIT_UNKNOWN;
    EXPECT_EQ(example_axis_command(&axis, &target, 1000U), NX_ERROR_INVALID);
    target = command();
    target.valid_until = 1000U;
    EXPECT_EQ(example_axis_command(&axis, &target, 1000U), NX_ERROR_TIMEOUT);
    target = command();
    target.position_rad = 3;
    EXPECT_EQ(example_axis_command(&axis, &target, 1000U), NX_ERROR_INVALID);
    EXPECT_EQ(axis.last_sequence, 0U);
}

TEST_F(Axis, RealTrajectoryPidAndPwmRunWithoutCallingTelemetryConsumer) {
    start();
    auto target = command();
    ASSERT_EQ(example_axis_command(&axis, &target, 1000U), NX_SUCCESS);
    uint32_t forward = 0;
    EXPECT_CALL(hardware, Commit(testing::_))
        .WillRepeatedly([&forward](const nx_pwm_group_frame_t* frame) {
            forward = frame->duty_ticks[0];
            EXPECT_EQ(frame->duty_ticks[1], 0U);
            return NX_SUCCESS;
        });
    ASSERT_EQ(step(11000U), NX_SUCCESS);
    ASSERT_EQ(step(31000U), NX_SUCCESS);
    EXPECT_GT(forward, 0U);
    example_axis_observation_t observation{};
    ASSERT_EQ(example_axis_take_observation(&axis, &observation), NX_SUCCESS);
    EXPECT_EQ(observation.sequence, 1U);
    EXPECT_EQ(observation.parameters_revision, 1U);
    EXPECT_EQ(observation.overwritten_observations, 1U);
    EXPECT_GT(observation.reference_rad, observation.position_rad);
    EXPECT_EQ(example_axis_take_observation(&axis, &observation),
              NX_ERROR_EMPTY);
}

TEST_F(Axis, PointToPointProfileRejectsReplacementWhileStillMoving) {
    start();
    auto target = command();
    ASSERT_EQ(example_axis_command(&axis, &target, 1000U), NX_SUCCESS);
    target.sequence = 2U;
    EXPECT_EQ(example_axis_command(&axis, &target, 2000U), NX_ERROR_BUSY);
    EXPECT_EQ(axis.last_sequence, 1U);
}

TEST_F(Axis, StaleCommandStopsBothSourcesAndRetainsFaultWithoutAutomaticRearm) {
    start();
    auto target = command();
    target.valid_until = 5000U;
    ASSERT_EQ(example_axis_command(&axis, &target, 1000U), NX_SUCCESS);
    EXPECT_CALL(hardware, Commit(testing::_)).Times(0);
    EXPECT_CALL(hardware, PwmStop()).WillOnce(testing::Return(NX_SUCCESS));
    EXPECT_CALL(hardware, EncoderStop()).WillOnce(testing::Return(NX_SUCCESS));
    EXPECT_EQ(step(5000U), NX_ERROR_TIMEOUT);
    EXPECT_EQ(axis.state, EXAMPLE_AXIS_FAULT);
    EXPECT_FALSE(axis.pwm_started);
    EXPECT_FALSE(axis.encoder_started);
    EXPECT_EQ(example_axis_start(&axis, 6000U), NX_ERROR_STATE);
}

TEST_F(Axis, FailureToWithdrawRetainsSourceAndCanBeRetried) {
    start();
    EXPECT_CALL(hardware, PwmStop())
        .WillOnce(testing::Return(NX_ERROR_IO))
        .WillOnce(testing::Return(NX_SUCCESS));
    EXPECT_CALL(hardware, EncoderStop()).WillOnce(testing::Return(NX_SUCCESS));
    EXPECT_EQ(example_axis_stop(&axis), NX_ERROR_IO);
    EXPECT_TRUE(axis.pwm_started);
    EXPECT_FALSE(axis.encoder_started);
    EXPECT_EQ(axis.state, EXAMPLE_AXIS_FAULT);
    EXPECT_EQ(example_axis_stop(&axis), NX_SUCCESS);
    EXPECT_FALSE(axis.pwm_started);
    EXPECT_EQ(axis.state, EXAMPLE_AXIS_FAULT);
}

TEST_F(Axis, LateOrRegressingCycleDoesNotConsumeAnotherEncoderSample) {
    start();
    EXPECT_CALL(hardware, Read(testing::_)).Times(0);
    EXPECT_CALL(hardware, PwmStop()).WillOnce(testing::Return(NX_SUCCESS));
    EXPECT_CALL(hardware, EncoderStop()).WillOnce(testing::Return(NX_SUCCESS));
    EXPECT_EQ(step(21001U), NX_ERROR_TIMEOUT);
    EXPECT_EQ(axis.state, EXAMPLE_AXIS_FAULT);
}

TEST_F(Axis, ParametersSwitchAsOneCompleteRevisionAtNextCycle) {
    start();
    ASSERT_EQ(step(11000U), NX_SUCCESS);
    auto candidate = parameters;
    candidate.revision = 2U;
    candidate.pid.kp = 2;
    ASSERT_EQ(example_axis_stage_parameters(&axis, &candidate), NX_SUCCESS);
    EXPECT_EQ(axis.parameters.revision, 1U);
    EXPECT_FLOAT_EQ(axis.pid.config.kp, 1);
    ASSERT_EQ(step(21000U), NX_SUCCESS);
    EXPECT_EQ(axis.parameters.revision, 2U);
    EXPECT_FLOAT_EQ(axis.pid.config.kp, 2);
    example_axis_observation_t observation{};
    ASSERT_EQ(example_axis_take_observation(&axis, &observation), NX_SUCCESS);
    EXPECT_EQ(observation.parameters_revision, 2U);
}

TEST_F(Axis, InvalidParameterCandidateLeavesActiveAndPendingStateUntouched) {
    start();
    auto candidate = parameters;
    candidate.revision = 2U;
    candidate.pid.kp = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(example_axis_stage_parameters(&axis, &candidate),
              NX_ERROR_INVALID);
    EXPECT_EQ(axis.parameters.revision, 1U);
    EXPECT_FALSE(axis.parameters_pending);
}

TEST_F(Axis, PwmFailureDiscardsUncommittedControlStateAndStopsSources) {
    start();
    auto target = command();
    ASSERT_EQ(example_axis_command(&axis, &target, 1000U), NX_SUCCESS);
    const auto integral = axis.pid.integral;
    EXPECT_CALL(hardware, Commit(testing::_))
        .WillOnce(testing::Return(NX_ERROR_IO));
    EXPECT_CALL(hardware, PwmStop()).WillOnce(testing::Return(NX_SUCCESS));
    EXPECT_CALL(hardware, EncoderStop()).WillOnce(testing::Return(NX_SUCCESS));
    EXPECT_EQ(step(11000U), NX_ERROR_IO);
    EXPECT_FLOAT_EQ(axis.pid.integral, integral);
    EXPECT_EQ(axis.state, EXAMPLE_AXIS_FAULT);
}
TEST_F(Axis, ZeroCalibrationScaleRejectsBeforeAnySourceStarts) {
    auto invalid = config;
    invalid.calibration.scale = 0;
    example_axis_t untouched{};
    EXPECT_EQ(example_axis_initialize(&untouched, &invalid, &parameters),
              NX_ERROR_INVALID);
    EXPECT_FALSE(untouched.initialized);
}

TEST_F(Axis, CycleBeforeAcceptedCommandTimeCannotJumpToTrajectoryEnd) {
    start();
    auto target = command();
    ASSERT_EQ(example_axis_command(&axis, &target, 10000U), NX_SUCCESS);
    EXPECT_CALL(hardware, Commit(testing::_)).Times(0);
    EXPECT_EQ(step(5000U), NX_ERROR_STATE);
    EXPECT_EQ(axis.state, EXAMPLE_AXIS_FAULT);
}

TEST_F(Axis, VelocityUsesEncoderObservationIntervalRatherThanReleaseJitter) {
    start();
    hardware.raw += 3U;
    EXPECT_CALL(hardware, Read(testing::_))
        .WillOnce([this](nx_encoder_sample_t* value) {
            *value = {16000U, hardware.raw};
            return NX_SUCCESS;
        });
    ASSERT_EQ(step(11000U), NX_SUCCESS);
    example_axis_observation_t observation{};
    ASSERT_EQ(example_axis_take_observation(&axis, &observation), NX_SUCCESS);
    EXPECT_NEAR(observation.velocity_rad_s, 0.2F, 0.00001F);
    EXPECT_EQ(observation.observed.ticks, 16000U);
    EXPECT_EQ(observation.observed.source, NX_IO_TIMESTAMP_SERVICE_OBSERVATION);
    EXPECT_EQ(observation.observed.epoch, 3U);
    EXPECT_EQ(observation.calibration_revision, 7U);
}

TEST_F(Axis, MissingCycleSequenceOrForeignEpochStopsBeforeReadingHardware) {
    start();
    example_axis_cycle_t cycle{};
    cycle.valid = true;
    cycle.sequence = 2U;
    cycle.calibration_revision = config.calibration_revision;
    cycle.release.ticks = 11000U;
    cycle.release.ticks_per_second = 1000000U;
    cycle.release.domain = config.clock_domain;
    cycle.release.epoch = 4U;
    cycle.release.source = NX_IO_TIMESTAMP_SERVICE_OBSERVATION;
    EXPECT_CALL(hardware, Read(testing::_)).Times(0);
    EXPECT_EQ(example_axis_step(&axis, &cycle), NX_ERROR_STATE);
    EXPECT_EQ(axis.state, EXAMPLE_AXIS_FAULT);
}

class ParameterMedia {
  public:
    std::array<uint8_t, 1024> bytes{};
    bool fail{false};
    unsigned programs{0U};
    ParameterMedia() {
        bytes.fill(0xffU);
    }
    static nx_storage_status_t read(void* p, size_t offset, void* out,
                                    size_t count) {
        auto& self = *static_cast<ParameterMedia*>(p);
        std::memcpy(out, self.bytes.data() + offset, count);
        return NX_STORAGE_OK;
    }
    static nx_storage_status_t program(void* p, size_t offset, const void* data,
                                       size_t count) {
        auto& self = *static_cast<ParameterMedia*>(p);
        ++self.programs;
        if (self.fail) {
            return NX_STORAGE_IO;
        }
        const auto* input = static_cast<const uint8_t*>(data);
        for (size_t index = 0; index < count; ++index) {
            if ((self.bytes[offset + index] & input[index]) != input[index]) {
                return NX_STORAGE_IO;
            }
            self.bytes[offset + index] &= input[index];
        }
        return NX_STORAGE_OK;
    }
    static nx_storage_status_t erase(void* p, size_t offset, size_t count) {
        auto& self = *static_cast<ParameterMedia*>(p);
        std::memset(self.bytes.data() + offset, 0xff, count);
        return NX_STORAGE_OK;
    }
    static nx_storage_status_t sync(void*) {
        return NX_STORAGE_OK;
    }
    nx_storage_port_t port{this, 1024U, 512U, 4U, read, program, erase, sync};
};
class Parameters : public Axis {
  protected:
    ParameterMedia media;
    nx_storage_t storage{};
    nx_settings_t settings{};
    std::array<uint8_t, 64> workspace{};
    std::array<uint8_t, 128> image{};
    void SetUp() override {
        Axis::SetUp();
        ASSERT_EQ(nx_storage_open(&storage, &media.port, 0, 512U,
                                  workspace.data(), workspace.size()),
                  NX_STORAGE_OK);
        ASSERT_EQ(nx_settings_open(&settings, &storage, 9U, image.data(),
                                   image.size(), 1U),
                  NX_STORAGE_OK);
    }
    void TearDown() override {
        nx_settings_close(&settings);
    }
};

TEST_F(Parameters,
       ExplicitSaveReloadsExactRevisionWithoutPersistingDuringControl) {
    auto candidate = parameters;
    candidate.revision = 2U;
    candidate.pid.kp = 2;
    ASSERT_EQ(example_axis_stage_parameters(&axis, &candidate), NX_SUCCESS);
    ASSERT_EQ(example_axis_apply_parameters(&axis), NX_SUCCESS);
    ASSERT_EQ(example_axis_save_parameters(&axis, &settings, 21U),
              NX_STORAGE_OK);
    EXPECT_GT(media.programs, 0U);
    EXPECT_EQ(axis.persisted_revision, 2U);
    nx_settings_close(&settings);
    ASSERT_EQ(nx_storage_open(&storage, &media.port, 0, 512U, workspace.data(),
                              workspace.size()),
              NX_STORAGE_OK);
    ASSERT_EQ(nx_settings_open(&settings, &storage, 9U, image.data(),
                               image.size(), 1U),
              NX_STORAGE_OK);
    example_axis_t restored{};
    ASSERT_EQ(example_axis_initialize(&restored, &config, &parameters),
              NX_SUCCESS);
    ASSERT_EQ(example_axis_load_parameters(&restored, &settings, 21U),
              NX_STORAGE_OK);
    EXPECT_EQ(restored.parameters.revision, 2U);
    EXPECT_FLOAT_EQ(restored.parameters.pid.kp, 2);
    start();
    const unsigned previous = media.programs;
    EXPECT_EQ(example_axis_save_parameters(&axis, &settings, 21U),
              NX_STORAGE_INVALID);
    EXPECT_EQ(media.programs, previous);
}

TEST_F(Parameters,
       HardwareIdentityOrCalibrationMismatchNeverChangesActiveSnapshot) {
    ASSERT_EQ(example_axis_save_parameters(&axis, &settings, 21U),
              NX_STORAGE_OK);
    auto other = config;
    ++other.hardware_identity;
    example_axis_t restored{};
    ASSERT_EQ(example_axis_initialize(&restored, &other, &parameters),
              NX_SUCCESS);
    EXPECT_EQ(example_axis_load_parameters(&restored, &settings, 21U),
              NX_STORAGE_UNSUPPORTED);
    EXPECT_EQ(restored.persisted_revision, 0U);
    EXPECT_EQ(restored.parameters.revision, 1U);
}

TEST_F(Parameters,
       SaveFailurePreservesLiveRevisionAndExposesRecoveryResponsibility) {
    media.fail = true;
    EXPECT_EQ(example_axis_save_parameters(&axis, &settings, 21U),
              NX_STORAGE_IO);
    EXPECT_EQ(axis.parameters.revision, 1U);
    EXPECT_EQ(axis.persisted_revision, 0U);
    EXPECT_FALSE(settings.opened);
    EXPECT_FALSE(storage.opened);
}

TEST_F(Axis, NewPositionLimitsMustContainTheStillActiveCompletedTarget) {
    start();
    const auto target = command();
    ASSERT_EQ(example_axis_command(&axis, &target, 1000U), NX_SUCCESS);
    for (nx_time_us_t now = 21000U; now <= 661000U; now += 20000U) {
        ASSERT_EQ(step(now), NX_SUCCESS);
    }
    ASSERT_EQ(axis.state, EXAMPLE_AXIS_READY);
    ASSERT_TRUE(axis.command_live);
    ASSERT_FLOAT_EQ(axis.position_rad, 0.0F);
    auto candidate = parameters;
    candidate.revision = 2U;
    candidate.minimum_position_rad = -0.1F;
    candidate.maximum_position_rad = 0.1F;
    EXPECT_EQ(example_axis_stage_parameters(&axis, &candidate),
              NX_ERROR_INVALID);
    EXPECT_FALSE(axis.parameters_pending);
    EXPECT_EQ(axis.parameters.revision, 1U);
}

} /* namespace */
