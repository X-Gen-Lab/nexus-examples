/**
 * \file            continuous_rx_test.cpp
 * \brief           Exact consumer loans survive release and shutdown failures
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-10-28
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "receive_application.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;

namespace {
class Receiver {
  public:
    MOCK_METHOD(nx_result_t, release, (nx_stream_t*, const nx_stream_block_t*));
    MOCK_METHOD(nx_result_t, stop, ());
    MOCK_METHOD(void, service, ());
};
Receiver* receiver;

nx_result_t stop(void* context) {
    return static_cast<Receiver*>(context)->stop();
}
void service(void* context) {
    static_cast<Receiver*>(context)->service();
}
const nx_uart_ops_t operations = {nullptr, nullptr, nullptr, service, nullptr,
                                  nullptr, nullptr, nullptr, nullptr, stop};

class ContinuousReceive : public testing::Test {
  protected:
    NiceMock<Receiver> model;
    nx_uart_port_t port{&operations, &model};
    uint8_t payload[4][32]{};
    nx_stream_slot_t slots[4]{};
    nx_stream_t stream{};
    example_receive_consumer_t consumer{};

    void SetUp() override {
        receiver = &model;
        for (size_t index = 0U; index < 4U; ++index) {
            slots[index].data = payload[index];
            slots[index].capacity = sizeof(payload[index]);
        }
        ASSERT_EQ(nx_stream_initialize(&stream, slots, 4U), NX_SUCCESS);
        ON_CALL(model, release(_, _)).WillByDefault(Invoke(nx_stream_release));
        ON_CALL(model, stop()).WillByDefault(Return(NX_SUCCESS));
    }
    void publish(uint8_t value, uint32_t flags = 0U) {
        nx_stream_fill_t fill{};
        ASSERT_EQ(nx_stream_reserve(&stream, &fill), NX_SUCCESS);
        fill.data[0] = value;
        if ((flags & NX_STREAM_BOUNDARY_LOSS) != 0U) {
            nx_stream_note_loss(&stream, 1U);
        }
        ASSERT_EQ(nx_stream_publish(&stream, &fill, 1U,
                                    flags & ~NX_STREAM_BOUNDARY_LOSS),
                  NX_SUCCESS);
    }
};

TEST_F(ContinuousReceive, FailedImmediateReleaseKeepsExactBorrowForRetry) {
    publish(42U, NX_STREAM_BOUNDARY_LOSS);
    EXPECT_CALL(model, release(_, _)).WillOnce(Return(NX_ERROR_STATE));
    EXPECT_EQ(example_receive_consume(&stream, &consumer, false),
              NX_ERROR_STATE);
    ASSERT_TRUE(consumer.has_pending);
    EXPECT_EQ(consumer.pending.data, payload[0]);
    EXPECT_EQ(consumer.pending.slot, 0U);
    EXPECT_EQ(consumer.pending.epoch, slots[0].epoch);
    EXPECT_EQ(slots[0].state, NX_STREAM_SLOT_BORROWED);
    EXPECT_EQ(stream.outstanding, 1U);
    EXPECT_EQ(consumer.received, 1U);
    EXPECT_EQ(consumer.losses, 1U);
    EXPECT_CALL(model, release(&stream, &consumer.pending))
        .WillOnce(Invoke(nx_stream_release));
    EXPECT_EQ(example_receive_release(&stream, &consumer), NX_SUCCESS);
    EXPECT_FALSE(consumer.has_pending);
    EXPECT_EQ(stream.outstanding, 0U);
}

TEST_F(ContinuousReceive, PendingReleaseFailurePreventsNewAcquire) {
    publish(42U);
    publish(99U);
    EXPECT_CALL(model, release(_, _))
        .Times(2)
        .WillRepeatedly(Return(NX_ERROR_IO));
    EXPECT_EQ(example_receive_consume(&stream, &consumer, false), NX_ERROR_IO);
    nx_stream_block_t first = consumer.pending;
    EXPECT_EQ(example_receive_consume(&stream, &consumer, false), NX_ERROR_IO);
    EXPECT_EQ(consumer.received, 1U);
    EXPECT_EQ(consumer.pending.epoch, first.epoch);
    EXPECT_EQ(slots[1].state, NX_STREAM_SLOT_READY);
    EXPECT_EQ(stream.outstanding, 2U);
}

TEST_F(ContinuousReceive, DelayedAndPendingTokensSurviveIndependentFailures) {
    publish(1U);
    ASSERT_EQ(example_receive_consume(&stream, &consumer, true), NX_SUCCESS);
    publish(2U);
    EXPECT_CALL(model, release(_, _)).WillOnce(Return(NX_ERROR_IO));
    EXPECT_EQ(example_receive_consume(&stream, &consumer, false), NX_ERROR_IO);
    ASSERT_TRUE(consumer.has_held);
    ASSERT_TRUE(consumer.has_pending);
    nx_stream_block_t delayed = consumer.held;
    EXPECT_CALL(model, release(&stream, &consumer.pending))
        .WillOnce(Invoke(nx_stream_release));
    EXPECT_CALL(model, release(&stream, &consumer.held))
        .WillOnce(Return(NX_ERROR_STATE));
    EXPECT_EQ(example_receive_release(&stream, &consumer), NX_ERROR_STATE);
    EXPECT_FALSE(consumer.has_pending);
    EXPECT_TRUE(consumer.has_held);
    EXPECT_EQ(consumer.held.epoch, delayed.epoch);
    EXPECT_EQ(stream.outstanding, 1U);
    EXPECT_CALL(model, release(&stream, &consumer.held))
        .WillOnce(Invoke(nx_stream_release));
    EXPECT_EQ(example_receive_release(&stream, &consumer), NX_SUCCESS);
    EXPECT_EQ(stream.outstanding, 0U);
}

TEST_F(ContinuousReceive, StopClosesProducerReleasesThenServicesBeforeRetry) {
    publish(1U);
    ASSERT_EQ(example_receive_consume(&stream, &consumer, true), NX_SUCCESS);
    testing::InSequence order;
    EXPECT_CALL(model, stop()).WillOnce(Return(NX_ERROR_BUSY));
    EXPECT_CALL(model, release(&stream, &consumer.held))
        .WillOnce(Invoke(nx_stream_release));
    EXPECT_CALL(model, service()).Times(1);
    EXPECT_CALL(model, stop()).WillOnce(Return(NX_SUCCESS));
    EXPECT_EQ(example_receive_stop(&port, &stream, &consumer), NX_SUCCESS);
    EXPECT_FALSE(consumer.has_held);
    EXPECT_EQ(stream.outstanding, 0U);
}

TEST_F(ContinuousReceive, FailedReleaseRetainsLoanAndStopsFurtherDrain) {
    publish(1U);
    ASSERT_EQ(example_receive_consume(&stream, &consumer, true), NX_SUCCESS);
    EXPECT_CALL(model, stop()).WillOnce(Return(NX_ERROR_BUSY));
    EXPECT_CALL(model, release(&stream, &consumer.held))
        .WillOnce(Return(NX_ERROR_STATE));
    EXPECT_CALL(model, service()).Times(0);
    EXPECT_EQ(example_receive_stop(&port, &stream, &consumer), NX_ERROR_STATE);
    EXPECT_TRUE(consumer.has_held);
    EXPECT_EQ(slots[0].state, NX_STREAM_SLOT_BORROWED);
}

TEST_F(ContinuousReceive,
       StuckProducerHasFiniteServiceBudgetAndRetainsStorage) {
    EXPECT_CALL(model, stop()).Times(65).WillRepeatedly(Return(NX_ERROR_BUSY));
    EXPECT_CALL(model, service()).Times(64);
    EXPECT_EQ(example_receive_stop(&port, &stream, &consumer), NX_ERROR_BUSY);
    EXPECT_EQ(stream.slots, slots);
}

TEST_F(ContinuousReceive, ProviderStopFailureDoesNotInventSuccess) {
    EXPECT_CALL(model, stop()).WillOnce(Return(NX_ERROR_IO));
    EXPECT_CALL(model, service()).Times(0);
    EXPECT_EQ(example_receive_stop(&port, &stream, &consumer), NX_ERROR_IO);
}
} /* namespace */

extern "C" nx_result_t
example_test_stream_release(nx_stream_t* stream,
                            const nx_stream_block_t* block) {
    return receiver->release(stream, block);
}
