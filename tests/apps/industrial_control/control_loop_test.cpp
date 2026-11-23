/**
 * \file            control_loop_test.cpp
 * \brief           Real stream/filter/PID/FSM control with external I/O mocks
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-21
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "control_loop.h"
#include <cstring>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace {
class ControlHardware {
  public:
    MOCK_METHOD(nx_result_t, AdcService, ());
    MOCK_METHOD(nx_result_t, AdcStop, ());
    MOCK_METHOD(nx_result_t, PwmSet, (uint32_t, uint32_t));
    MOCK_METHOD(nx_result_t, PwmStop, ());
    static nx_result_t service(void* context) {
        return static_cast<ControlHardware*>(context)->AdcService();
    }
    static nx_result_t stop_adc(void* context) {
        return static_cast<ControlHardware*>(context)->AdcStop();
    }
    static nx_result_t set(void* context, uint32_t period, uint32_t duty) {
        return static_cast<ControlHardware*>(context)->PwmSet(period, duty);
    }
    static nx_result_t stop_pwm(void* context) {
        return static_cast<ControlHardware*>(context)->PwmStop();
    }
};

const nx_adc_ops_t adc_operations = [] {
    nx_adc_ops_t operations = {};
    operations.stream_service = ControlHardware::service;
    operations.stream_stop = ControlHardware::stop_adc;
    return operations;
}();
const nx_pwm_ops_t pwm_operations = [] {
    nx_pwm_ops_t operations = {};
    operations.set = ControlHardware::set;
    operations.stop = ControlHardware::stop_pwm;
    return operations;
}();

class ControlLoop : public testing::Test {
  protected:
    testing::StrictMock<ControlHardware> hardware;
    const nx_adc_port_t adc = {&adc_operations, &hardware};
    const nx_pwm_port_t pwm = {&pwm_operations, &hardware};
    nx_stream_t stream = {};
    nx_stream_slot_t slot = {};
    uint8_t payload[sizeof(uint16_t)] = {};
    example_control_loop_t loop = {};
    example_control_config_t config = {};
    void SetUp() override {
        slot.data = payload;
        slot.capacity = sizeof(payload);
        ASSERT_EQ(nx_stream_initialize(&stream, &slot, 1), NX_SUCCESS);
        config.adc = &adc;
        config.stream = &stream;
        config.pwm = &pwm;
        config.pid = {1, 0, 0, 0, 0, 1, -1, 1};
        config.pwm_period_ticks = 1000;
        config.sample_period_us = 10000;
        config.full_scale_counts = 4095;
        config.alpha = 0.5F;
        config.setpoint = 1;
        ASSERT_EQ(example_control_loop_initialize(&loop, &config), NX_SUCCESS);
    }
    void publish(uint16_t value, uint32_t flags = 0) {
        if ((flags & NX_STREAM_BOUNDARY_LOSS) != 0U) {
            nx_stream_note_loss(&stream, 1);
        }
        nx_stream_fill_t fill = {};
        ASSERT_EQ(nx_stream_reserve(&stream, &fill), NX_SUCCESS);
        std::memcpy(fill.data, &value, sizeof(value));
        ASSERT_EQ(nx_stream_publish(&stream, &fill, sizeof(value),
                                    flags & ~NX_STREAM_BOUNDARY_LOSS),
                  NX_SUCCESS);
    }
    void stop_and_join() {
        EXPECT_CALL(hardware, PwmStop()).WillOnce(testing::Return(NX_SUCCESS));
        EXPECT_CALL(hardware, AdcStop()).WillOnce(testing::Return(NX_SUCCESS));
        EXPECT_EQ(nx_pwm_port_stop(&pwm), NX_SUCCESS);
        EXPECT_EQ(nx_adc_port_stream_stop(&adc), NX_SUCCESS);
        EXPECT_EQ(example_control_loop_finish(&loop), NX_SUCCESS);
        EXPECT_EQ(nx_stream_stop(&stream), NX_SUCCESS);
    }
};

TEST_F(ControlLoop, RealStreamFilterPidAndPwmExecuteInTheExistingCaller) {
    example_control_observation_t observation = {};
    publish(0);
    EXPECT_CALL(hardware, AdcService()).WillOnce(testing::Return(NX_SUCCESS));
    EXPECT_CALL(hardware, PwmSet(1000, 1000))
        .WillOnce(testing::Return(NX_SUCCESS));
    ASSERT_EQ(example_control_loop_step(&loop, &observation), NX_SUCCESS);
    EXPECT_FLOAT_EQ(observation.measurement, 0);
    EXPECT_FLOAT_EQ(observation.command, 1);
    EXPECT_EQ(observation.samples, 1U);
    EXPECT_FALSE(loop.borrowed);
    publish(4095);
    EXPECT_CALL(hardware, AdcService())
        .WillOnce(testing::Return(NX_ERROR_BUSY));
    EXPECT_CALL(hardware, PwmSet(1000, 500))
        .WillOnce(testing::Return(NX_SUCCESS));
    ASSERT_EQ(example_control_loop_step(&loop, &observation), NX_SUCCESS);
    EXPECT_FLOAT_EQ(observation.measurement, 0.5F);
    EXPECT_FLOAT_EQ(observation.command, 0.5F);
    EXPECT_EQ(observation.samples, 2U);
    EXPECT_EQ(loop.machine.current, EXAMPLE_CONTROL_RUNNING);
    stop_and_join();
    EXPECT_EQ(loop.machine.current, EXAMPLE_CONTROL_STOPPED);
}

TEST_F(ControlLoop, EmptyStreamDoesNotReuseAnOldMeasurementOrAdvancePid) {
    example_control_observation_t observation = {};
    observation.samples = 91;
    EXPECT_CALL(hardware, AdcService()).WillOnce(testing::Return(NX_SUCCESS));
    EXPECT_EQ(example_control_loop_step(&loop, &observation), NX_ERROR_EMPTY);
    EXPECT_EQ(observation.samples, 91U);
    EXPECT_EQ(loop.samples, 0U);
    EXPECT_FALSE(loop.filter.initialized);
    stop_and_join();
}

TEST_F(ControlLoop, PwmFailureFaultsControlAndReleasesTheActualConsumerLoan) {
    EXPECT_CALL(hardware, PwmStop()).WillOnce(testing::Return(NX_SUCCESS));
    publish(0);
    EXPECT_CALL(hardware, AdcService()).WillOnce(testing::Return(NX_SUCCESS));
    EXPECT_CALL(hardware, PwmSet(1000, 1000))
        .WillOnce(testing::Return(NX_ERROR_IO));
    example_control_observation_t observation = {};
    observation.samples = 91;
    EXPECT_EQ(example_control_loop_step(&loop, &observation), NX_ERROR_IO);
    EXPECT_EQ(loop.machine.current, EXAMPLE_CONTROL_FAULT);
    EXPECT_EQ(observation.samples, 91U);
    EXPECT_FALSE(loop.borrowed);
    EXPECT_EQ(stream.outstanding, 0U);
    EXPECT_EQ(example_control_loop_step(&loop, &observation), NX_ERROR_STATE);
    stop_and_join();
}

TEST_F(ControlLoop, LostAcquisitionBlockNeverUpdatesTheActuator) {
    EXPECT_CALL(hardware, PwmStop()).WillOnce(testing::Return(NX_SUCCESS));
    publish(1000, NX_STREAM_BOUNDARY_LOSS);
    EXPECT_CALL(hardware, AdcService()).WillOnce(testing::Return(NX_SUCCESS));
    example_control_observation_t observation = {};
    EXPECT_EQ(example_control_loop_step(&loop, &observation), NX_ERROR_IO);
    EXPECT_EQ(loop.machine.current, EXAMPLE_CONTROL_FAULT);
    EXPECT_FALSE(loop.filter.initialized);
    EXPECT_EQ(stream.outstanding, 0U);
    stop_and_join();
}

TEST_F(ControlLoop, InvalidNormalizedProfileRejectsBeforePortsOrStorageChange) {
    example_control_loop_t unused = {};
    auto invalid = config;
    invalid.pid.output_maximum = 2;
    EXPECT_EQ(example_control_loop_initialize(&unused, &invalid),
              NX_ERROR_INVALID);
    EXPECT_EQ(unused.machine.config, nullptr);
    EXPECT_EQ(stream.outstanding, 0U);
    stop_and_join();
}

TEST_F(ControlLoop, FaultRequestsOutputWithdrawalAndFailedStopRemainsRetained) {
    publish(1000, NX_STREAM_BOUNDARY_LOSS);
    EXPECT_CALL(hardware, AdcService()).WillOnce(testing::Return(NX_SUCCESS));
    EXPECT_CALL(hardware, PwmStop())
        .WillOnce(testing::Return(NX_ERROR_BUSY))
        .WillOnce(testing::Return(NX_ERROR_IO))
        .WillOnce(testing::Return(NX_SUCCESS));
    example_control_observation_t observation{};
    EXPECT_EQ(example_control_loop_step(&loop, &observation), NX_ERROR_IO);
    EXPECT_TRUE(loop.output_stop_pending);
    EXPECT_EQ(loop.output_stop_result, NX_ERROR_BUSY);
    EXPECT_EQ(example_control_loop_finish(&loop), NX_ERROR_IO);
    EXPECT_TRUE(loop.output_stop_pending);
    EXPECT_EQ(loop.machine.current, EXAMPLE_CONTROL_FAULT);
    EXPECT_EQ(example_control_loop_finish(&loop), NX_SUCCESS);
    EXPECT_FALSE(loop.output_stop_pending);
    EXPECT_EQ(loop.machine.current, EXAMPLE_CONTROL_STOPPED);
    EXPECT_CALL(hardware, AdcStop()).WillOnce(testing::Return(NX_SUCCESS));
    EXPECT_EQ(nx_adc_port_stream_stop(&adc), NX_SUCCESS);
    EXPECT_EQ(nx_stream_stop(&stream), NX_SUCCESS);
}
} /* namespace */
