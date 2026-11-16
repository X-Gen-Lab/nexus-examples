/**
 * \file            reference_rx_policy_test.cpp
 * \brief           Public UART receive policy preserves loss boundaries
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-10-12
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "rx_policy.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace {
class Receiver {
  public:
    MOCK_METHOD(nx_result_t, events, (nx_uart_rx_event_t*, size_t, size_t*));
    MOCK_METHOD(nx_result_t, bytes, (uint8_t*, size_t, size_t*));
};
nx_result_t read_events(void* context, nx_uart_rx_event_t* events,
                        size_t capacity, size_t* count) {
    return static_cast<Receiver*>(context)->events(events, capacity, count);
}
nx_result_t read_bytes(void* context, uint8_t* bytes, size_t capacity,
                       size_t* count) {
    return static_cast<Receiver*>(context)->bytes(bytes, capacity, count);
}
const nx_uart_ops_t operations = {nullptr,     nullptr,    nullptr, nullptr,
                                  read_events, read_bytes, nullptr, nullptr,
                                  nullptr,     nullptr};
class ReferenceRx : public testing::Test {
  protected:
    testing::StrictMock<Receiver> receiver;
    const nx_uart_port_t port{&operations, &receiver};
    uint8_t bytes[8]{};
    size_t count{99U};
};
TEST_F(ReferenceRx, EventBytesUseOnlyThePublicEventOperation) {
    EXPECT_CALL(receiver, events(testing::_, 8U, testing::_))
        .WillOnce(testing::Invoke(
            [](nx_uart_rx_event_t* events, size_t, size_t* copied) {
                events[0] = {0U, 0U, 'x'};
                events[1] = {0U, 0U, 'y'};
                *copied = 2U;
                return NX_SUCCESS;
            }));
    EXPECT_EQ(example_reference_read(&port, bytes, 8U, &count), NX_SUCCESS);
    ASSERT_EQ(count, 2U);
    EXPECT_EQ(bytes[0], 'x');
    EXPECT_EQ(bytes[1], 'y');
}
TEST_F(ReferenceRx, ByteOnlyProviderUsesThePublicByteOperation) {
    testing::InSequence order;
    EXPECT_CALL(receiver, events(testing::_, 8U, testing::_))
        .WillOnce(testing::Return(NX_ERROR_UNSUPPORTED));
    EXPECT_CALL(receiver, bytes(testing::_, 8U, testing::_))
        .WillOnce(testing::Invoke([](uint8_t* output, size_t, size_t* copied) {
            output[0] = 'z';
            *copied = 1U;
            return NX_SUCCESS;
        }));
    EXPECT_EQ(example_reference_read(&port, bytes, 8U, &count), NX_SUCCESS);
    ASSERT_EQ(count, 1U);
    EXPECT_EQ(bytes[0], 'z');
}
TEST_F(ReferenceRx, AggregateByteLossNeverPublishesPartialEcho) {
    EXPECT_CALL(receiver, events(testing::_, 8U, testing::_))
        .WillOnce(testing::Return(NX_ERROR_UNSUPPORTED));
    EXPECT_CALL(receiver, bytes(testing::_, 8U, testing::_))
        .WillOnce(testing::Invoke([](uint8_t* output, size_t, size_t* copied) {
            output[0] = 'x';
            *copied = 1U;
            return NX_ERROR_OVERFLOW;
        }));
    EXPECT_EQ(example_reference_read(&port, bytes, 8U, &count),
              NX_ERROR_OVERFLOW);
    EXPECT_EQ(count, 0U);
}
TEST_F(ReferenceRx, StatusOnlyOrDamagedEventsNeverBecomeEchoBytes) {
    for (uint32_t flags :
         {NX_UART_EVENT_NO_BYTE, NX_UART_EVENT_LOSS, NX_UART_EVENT_PARITY,
          NX_UART_EVENT_FRAMING, NX_UART_EVENT_NOISE, NX_UART_EVENT_OVERRUN}) {
        EXPECT_CALL(receiver, events(testing::_, 8U, testing::_))
            .WillOnce(testing::Invoke(
                [flags](nx_uart_rx_event_t* events, size_t, size_t* copied) {
                    events[0] = {0U, 0U, 'x'};
                    events[1] = {0U, flags, 'y'};
                    *copied = 2U;
                    return NX_SUCCESS;
                }));
        EXPECT_EQ(example_reference_read(&port, bytes, 8U, &count),
                  NX_ERROR_IO);
        EXPECT_EQ(count, 0U);
    }
}
TEST_F(ReferenceRx, EventStateFailureCannotProbeADifferentProfile) {
    EXPECT_CALL(receiver, events(testing::_, 8U, testing::_))
        .WillOnce(testing::Return(NX_ERROR_STATE));
    EXPECT_EQ(example_reference_read(&port, bytes, 8U, &count), NX_ERROR_STATE);
    EXPECT_EQ(count, 0U);
}
TEST_F(ReferenceRx, EmptyEventProfilePublishesNoEcho) {
    EXPECT_CALL(receiver, events(testing::_, 8U, testing::_))
        .WillOnce(testing::Return(NX_ERROR_EMPTY));
    EXPECT_EQ(example_reference_read(&port, bytes, 8U, &count), NX_ERROR_EMPTY);
    EXPECT_EQ(count, 0U);
}
TEST_F(ReferenceRx, EmptyByteProfilePublishesNoEcho) {
    EXPECT_CALL(receiver, events(testing::_, 8U, testing::_))
        .WillOnce(testing::Return(NX_ERROR_UNSUPPORTED));
    EXPECT_CALL(receiver, bytes(testing::_, 8U, testing::_))
        .WillOnce(testing::Return(NX_ERROR_EMPTY));
    EXPECT_EQ(example_reference_read(&port, bytes, 8U, &count), NX_ERROR_EMPTY);
    EXPECT_EQ(count, 0U);
}
TEST_F(ReferenceRx, InvalidDestinationDoesNotConsumeReception) {
    EXPECT_EQ(example_reference_read(&port, bytes, 0U, &count),
              NX_ERROR_INVALID);
    EXPECT_EQ(example_reference_read(&port, bytes, 9U, &count),
              NX_ERROR_INVALID);
    EXPECT_EQ(example_reference_read(&port, nullptr, 8U, &count),
              NX_ERROR_INVALID);
    EXPECT_EQ(example_reference_read(nullptr, bytes, 8U, &count),
              NX_ERROR_INVALID);
    EXPECT_EQ(example_reference_read(&port, bytes, 8U, nullptr),
              NX_ERROR_INVALID);
    EXPECT_EQ(count, 0U);
}
} /* namespace */
