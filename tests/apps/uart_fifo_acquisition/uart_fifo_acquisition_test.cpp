/**
 * \file            uart_fifo_acquisition_test.cpp
 * \brief           Draft acquisition regressions against external UART ports.
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-10-20
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "workload.h"
#include <array>
#include <cstring>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;

namespace {
class Uart {
  public:
    MOCK_METHOD(nx_result_t, submit, (nx_uart_tx_request_t*));
    MOCK_METHOD(nx_result_t, cancel, (nx_uart_tx_request_t*));
    MOCK_METHOD(void, service, ());
    MOCK_METHOD(nx_result_t, read, (uint8_t*, size_t, size_t*));
    MOCK_METHOD(nx_result_t, stop, ());
    MOCK_METHOD(nx_result_t, platform_stop, ());
};

Uart* platform_owner;
nx_result_t platform_stop(void) {
    return platform_owner->platform_stop();
}

nx_result_t submit(void* context, nx_uart_tx_request_t* request) {
    return static_cast<Uart*>(context)->submit(request);
}
nx_result_t cancel(void* context, nx_uart_tx_request_t* request) {
    return static_cast<Uart*>(context)->cancel(request);
}
void service(void* context) {
    static_cast<Uart*>(context)->service();
}
nx_result_t read(void* context, uint8_t* bytes, size_t capacity,
                 size_t* count) {
    return static_cast<Uart*>(context)->read(bytes, capacity, count);
}
nx_result_t stop(void* context) {
    return static_cast<Uart*>(context)->stop();
}
const nx_uart_ops_t operations = {submit, nullptr, cancel,  service, nullptr,
                                  read,   stop,    nullptr, nullptr, nullptr};

class FifoAcquisition : public testing::Test {
  protected:
    NiceMock<Uart> uart;
    nx_uart_port_t port{&operations, &uart};
    fifo_acquisition_t state{};
    fifo_acquisition_config_t config{7U, 1000U, 10U, 500U, 50U, 0U};

    void SetUp() override {
        platform_owner = &uart;
        ON_CALL(uart, submit(_)).WillByDefault(Invoke([](auto* request) {
            return nx_request_admit(&request->base, NX_REQUEST_ACTIVE);
        }));
        ON_CALL(uart, cancel(_)).WillByDefault(Invoke([](auto* request) {
            return nx_request_transition(&request->base, NX_REQUEST_DRAINING);
        }));
        ON_CALL(uart, read(_, _, _))
            .WillByDefault(Invoke([](uint8_t*, size_t, size_t* count) {
                *count = 0U;
                return NX_ERROR_EMPTY;
            }));
        ON_CALL(uart, stop()).WillByDefault(Return(NX_SUCCESS));
    }
    void initialize() {
        ASSERT_EQ(fifo_acquisition_initialize(&state, &port, &config, 100U),
                  NX_SUCCESS);
    }
    void settle(nx_result_t result = NX_SUCCESS, size_t count = 32U) {
        nx_request_settle(&state.request.base, result, count);
    }
};

TEST_F(FifoAcquisition, ConstructionIsColdAndRejectsLiveReinitialization) {
    EXPECT_CALL(uart, submit(_)).Times(0);
    EXPECT_CALL(uart, service()).Times(0);
    EXPECT_CALL(uart, read(_, _, _)).Times(0);
    EXPECT_CALL(uart, stop()).Times(0);
    initialize();
    EXPECT_EQ(fifo_acquisition_initialize(&state, &port, &config, 100U),
              NX_ERROR_STATE);
}

TEST_F(FifoAcquisition, ReadsWhileTxIsActiveWithoutRewritingBorrowedPayload) {
    initialize();
    EXPECT_CALL(uart, submit(_)).Times(1);
    unsigned received = 0U;
    EXPECT_CALL(uart, read(_, _, _))
        .Times(2)
        .WillRepeatedly(
            Invoke([&](uint8_t* out, size_t capacity, size_t* count) {
                EXPECT_GE(capacity, 1U);
                out[0] = received++ == 0U ? 7U : 80U;
                *count = 1U;
                return NX_SUCCESS;
            }));
    EXPECT_EQ(fifo_acquisition_step(&state, 100U), NX_SUCCESS);
    std::array<uint8_t, FIFO_ACQUISITION_TX_BYTES> borrowed{};
    std::memcpy(borrowed.data(), state.transmit, borrowed.size());
    EXPECT_EQ(fifo_acquisition_step(&state, 110U), NX_SUCCESS);
    EXPECT_EQ(nx_request_state(&state.request.base), NX_REQUEST_ACTIVE);
    EXPECT_EQ(state.received_count, 2U);
    EXPECT_EQ(state.mismatch_count, 0U);
    EXPECT_EQ(std::memcmp(borrowed.data(), state.transmit, borrowed.size()), 0);
}

TEST_F(FifoAcquisition, SettledResultPrecedesTheNextSequenceAndPayloadReuse) {
    initialize();
    unsigned sequence = 0U;
    EXPECT_CALL(uart, submit(&state.request))
        .Times(2)
        .WillRepeatedly(Invoke([&](nx_uart_tx_request_t* request) {
            EXPECT_EQ(request->data, state.transmit);
            EXPECT_EQ(request->length, FIFO_ACQUISITION_TX_BYTES);
            EXPECT_EQ(nx_request_state(&request->base), NX_REQUEST_READY);
            EXPECT_EQ(state.completed_requests, sequence);
            EXPECT_EQ(state.completed_tx_bytes,
                      sequence * FIFO_ACQUISITION_TX_BYTES);
            EXPECT_EQ(request->data[8], sequence);
            EXPECT_EQ(request->data[9], 0U);
            EXPECT_EQ(request->data[10], 0U);
            EXPECT_EQ(request->data[11], 0U);
            EXPECT_EQ(request->data[12], 7U ^ sequence ^ 12U);
            ++sequence;
            return nx_request_admit(&request->base, NX_REQUEST_ACTIVE);
        }));
    ASSERT_EQ(fifo_acquisition_step(&state, 100U), NX_SUCCESS);
    std::array<uint8_t, FIFO_ACQUISITION_TX_BYTES> first{};
    std::memcpy(first.data(), state.transmit, first.size());
    EXPECT_CALL(uart, service()).WillOnce(Invoke([&] {
        EXPECT_EQ(std::memcmp(first.data(), state.transmit, first.size()), 0);
        EXPECT_EQ(nx_request_state(&state.request.base), NX_REQUEST_ACTIVE);
        EXPECT_TRUE(state.borrowed);
        settle();
    }));
    EXPECT_EQ(fifo_acquisition_step(&state, 110U), NX_SUCCESS);
    EXPECT_EQ(sequence, 2U);
    EXPECT_EQ(state.accepted_requests, 2U);
    EXPECT_EQ(state.completed_requests, 1U);
    EXPECT_EQ(state.completed_tx_bytes, FIFO_ACQUISITION_TX_BYTES);
    EXPECT_TRUE(state.borrowed);
    EXPECT_EQ(nx_request_state(&state.request.base), NX_REQUEST_ACTIVE);
    EXPECT_NE(std::memcmp(first.data(), state.transmit, first.size()), 0);
}

TEST_F(FifoAcquisition, OverflowKeepsItsValidPrefixWithoutInventingLostBytes) {
    initialize();
    EXPECT_CALL(uart, read(_, _, _))
        .WillOnce(Invoke([](uint8_t* out, size_t, size_t* count) {
            out[0] = 7U;
            out[1] = 80U;
            *count = 2U;
            return NX_ERROR_OVERFLOW;
        }));
    EXPECT_EQ(fifo_acquisition_step(&state, 100U), NX_ERROR_OVERFLOW);
    EXPECT_EQ(state.received_count, 2U);
    EXPECT_EQ(state.received[0], 7U);
    EXPECT_EQ(state.received[1], 80U);
    EXPECT_EQ(state.overflow_events, 1U);
    EXPECT_EQ(state.phase, FIFO_ACQUISITION_RUNNING);
}

TEST_F(FifoAcquisition, ReadIoKeepsCopiedPrefixAndRetainsTheAcceptedTxLoan) {
    initialize();
    fifo_acquisition_step(&state, 100U);
    EXPECT_CALL(uart, read(_, _, _))
        .WillOnce(Invoke([](uint8_t* out, size_t, size_t* count) {
            out[0] = 7U;
            *count = 1U;
            return NX_ERROR_IO;
        }));
    EXPECT_CALL(uart, stop()).Times(0);
    EXPECT_EQ(fifo_acquisition_step(&state, 110U), NX_ERROR_IO);
    EXPECT_EQ(state.received_count, 1U);
    EXPECT_TRUE(state.borrowed);
    EXPECT_NE(state.phase, FIFO_ACQUISITION_CLOSED);
}

TEST_F(FifoAcquisition, CancelAndExpiredDrainDoNotRevokeTheBorrow) {
    initialize();
    fifo_acquisition_step(&state, 100U);
    EXPECT_CALL(uart, cancel(&state.request)).Times(1);
    EXPECT_CALL(uart, stop()).Times(0);
    ASSERT_EQ(fifo_acquisition_close(&state, 110U), NX_SUCCESS);
    fifo_acquisition_step(&state, 110U);
    EXPECT_EQ(fifo_acquisition_step(&state, 160U), NX_ERROR_TIMEOUT);
    fifo_acquisition_step(&state, 170U);
    EXPECT_TRUE(state.borrowed);
    EXPECT_EQ(state.phase, FIFO_ACQUISITION_RETAINED);
    EXPECT_EQ(nx_request_state(&state.request.base), NX_REQUEST_DRAINING);
}

TEST_F(FifoAcquisition, LateWireSettlementAllowsCloseButKeepsTheFirstFailure) {
    initialize();
    fifo_acquisition_step(&state, 100U);
    fifo_acquisition_close(&state, 110U);
    fifo_acquisition_step(&state, 110U);
    fifo_acquisition_step(&state, 160U);
    EXPECT_CALL(uart, service()).WillOnce(Invoke([this] {
        settle(NX_ERROR_CANCELLED, 3U);
    }));
    EXPECT_CALL(uart, stop()).WillOnce(Return(NX_SUCCESS));
    EXPECT_EQ(fifo_acquisition_step(&state, 170U), NX_ERROR_TIMEOUT);
    EXPECT_FALSE(state.borrowed);
    EXPECT_EQ(state.completed_tx_bytes, 3U);
    EXPECT_EQ(state.phase, FIFO_ACQUISITION_CLOSED);
}

TEST_F(FifoAcquisition, RefusedCancelDoesNotBecomeAcknowledgementOrRelease) {
    initialize();
    fifo_acquisition_step(&state, 100U);
    fifo_acquisition_close(&state, 110U);
    EXPECT_CALL(uart, cancel(&state.request))
        .WillOnce(Return(NX_ERROR_IO))
        .WillOnce(Invoke([](auto* request) {
            return nx_request_transition(&request->base, NX_REQUEST_DRAINING);
        }));
    EXPECT_CALL(uart, stop()).Times(0);
    EXPECT_EQ(fifo_acquisition_step(&state, 110U), NX_ERROR_IO);
    EXPECT_FALSE(state.cancel_sent);
    EXPECT_TRUE(state.borrowed);
    EXPECT_EQ(fifo_acquisition_step(&state, 120U), NX_ERROR_IO);
    EXPECT_TRUE(state.cancel_sent);
    EXPECT_TRUE(state.borrowed);
}

TEST_F(FifoAcquisition, FiniteWindowClosesAdmissionAndRequiresRealSettlement) {
    initialize();
    EXPECT_CALL(uart, submit(_)).Times(1);
    fifo_acquisition_step(&state, 100U);
    EXPECT_CALL(uart, cancel(&state.request)).Times(1);
    EXPECT_CALL(uart, service()).WillOnce(Invoke([this] {
        settle(NX_ERROR_CANCELLED, 3U);
    }));
    EXPECT_CALL(uart, stop()).WillOnce(Return(NX_SUCCESS));
    EXPECT_EQ(fifo_acquisition_step(&state, 1100U), NX_SUCCESS);
    EXPECT_EQ(state.phase, FIFO_ACQUISITION_CLOSED);
    EXPECT_EQ(state.acquisition_end, 1100U);
    EXPECT_EQ(state.completed_tx_bytes, 3U);
    EXPECT_FALSE(state.borrowed);
}

TEST_F(FifoAcquisition, ExhaustedRxCaptureStopsAdmissionWithoutReclaimingTx) {
    config.duration_us = 2000U;
    initialize();
    EXPECT_CALL(uart, submit(_)).Times(1);
    ON_CALL(uart, read(_, _, _))
        .WillByDefault(Invoke([](uint8_t* out, size_t capacity, size_t* count) {
            std::memset(out, 0, capacity);
            *count = capacity;
            return NX_SUCCESS;
        }));
    for (unsigned index = 0U; index < 129U; ++index) {
        fifo_acquisition_step(&state, 100U + 10U * index);
    }
    EXPECT_EQ(state.received_count, FIFO_ACQUISITION_RX_BYTES);
    EXPECT_TRUE(state.trace_exhausted);
    EXPECT_EQ(state.first_error, NX_ERROR_EXHAUSTED);
    EXPECT_NE(state.phase, FIFO_ACQUISITION_RUNNING);
    EXPECT_NE(state.phase, FIFO_ACQUISITION_CLOSED);
    EXPECT_TRUE(state.borrowed);
}

TEST_F(FifoAcquisition, RejectedSubmissionHasNoCancellationOrBorrow) {
    initialize();
    EXPECT_CALL(uart, submit(_)).WillOnce(Return(NX_ERROR_BUSY));
    fifo_acquisition_step(&state, 100U);
    EXPECT_FALSE(state.borrowed);
    EXPECT_EQ(nx_request_state(&state.request.base), NX_REQUEST_READY);
    EXPECT_CALL(uart, cancel(_)).Times(0);
    fifo_acquisition_close(&state, 110U);
    fifo_acquisition_step(&state, 110U);
    EXPECT_EQ(state.phase, FIFO_ACQUISITION_CLOSED);
}

TEST_F(FifoAcquisition, CloseWaitsForTheBufferedRxTailToBecomeEmpty) {
    initialize();
    fifo_acquisition_step(&state, 100U);
    settle();
    fifo_acquisition_close(&state, 110U);
    EXPECT_CALL(uart, read(_, _, _))
        .WillOnce(Invoke([](uint8_t* out, size_t, size_t* count) {
            out[0] = 7U;
            *count = 1U;
            return NX_SUCCESS;
        }));
    EXPECT_CALL(uart, stop()).Times(0);
    fifo_acquisition_step(&state, 110U);
    testing::Mock::VerifyAndClearExpectations(&uart);
    EXPECT_CALL(uart, stop()).WillOnce(Return(NX_SUCCESS));
    fifo_acquisition_step(&state, 120U);
    EXPECT_EQ(state.phase, FIFO_ACQUISITION_CLOSED);
}

TEST_F(FifoAcquisition, RefusedStopRetainsStateForARealRetry) {
    initialize();
    EXPECT_CALL(uart, submit(_)).WillOnce(Return(NX_ERROR_BUSY));
    fifo_acquisition_step(&state, 100U);
    fifo_acquisition_close(&state, 110U);
    EXPECT_CALL(uart, stop())
        .WillOnce(Return(NX_ERROR_IO))
        .WillOnce(Return(NX_SUCCESS));
    EXPECT_EQ(fifo_acquisition_step(&state, 110U), NX_ERROR_IO);
    EXPECT_EQ(state.phase, FIFO_ACQUISITION_RETAINED);
    EXPECT_EQ(fifo_acquisition_step(&state, 120U), NX_ERROR_IO);
    EXPECT_EQ(state.phase, FIFO_ACQUISITION_CLOSED);
}

TEST_F(FifoAcquisition, BrokenClockRetainsWithoutMoreProviderAccess) {
    initialize();
    fifo_acquisition_step(&state, 100U);
    EXPECT_CALL(uart, service()).Times(0);
    EXPECT_CALL(uart, read(_, _, _)).Times(0);
    EXPECT_CALL(uart, cancel(_)).Times(0);
    EXPECT_CALL(uart, stop()).Times(0);
    EXPECT_EQ(fifo_acquisition_step(&state, 99U), NX_ERROR_STATE);
    EXPECT_EQ(fifo_acquisition_step(&state, 120U), NX_ERROR_STATE);
    EXPECT_TRUE(state.borrowed);
    EXPECT_TRUE(state.clock_fault);
    EXPECT_EQ(state.phase, FIFO_ACQUISITION_RETAINED);
}

TEST_F(FifoAcquisition, InvalidSessionOrUnboundedSamplePlanIsCold) {
    EXPECT_CALL(uart, service()).Times(0);
    EXPECT_CALL(uart, submit(_)).Times(0);
    config.session = 0U;
    EXPECT_EQ(fifo_acquisition_initialize(&state, &port, &config, 100U),
              NX_ERROR_INVALID);
    config.session = 7U;
    config.duration_us = UINT32_MAX;
    EXPECT_EQ(fifo_acquisition_initialize(&state, &port, &config, 100U),
              NX_ERROR_INVALID);
    config.duration_us = 1000U;
    EXPECT_EQ(
        fifo_acquisition_initialize(&state, &port, &config, UINT64_MAX - 500U),
        NX_ERROR_INVALID);
}
TEST_F(FifoAcquisition, PlatformCleanupRejectsRunningAndRetainedLoans) {
    initialize();
    fifo_acquisition_step(&state, 100U);
    uint32_t attempts = 99U;
    EXPECT_CALL(uart, platform_stop()).Times(0);
    EXPECT_EQ(
        fifo_acquisition_join_platform(&state, platform_stop, 3U, &attempts),
        NX_ERROR_STATE);
    EXPECT_EQ(attempts, 99U);
    fifo_acquisition_close(&state, 110U);
    fifo_acquisition_step(&state, 110U);
    fifo_acquisition_step(&state, 160U);
    ASSERT_EQ(state.phase, FIFO_ACQUISITION_RETAINED);
    EXPECT_EQ(
        fifo_acquisition_join_platform(&state, platform_stop, 3U, &attempts),
        NX_ERROR_STATE);
    EXPECT_TRUE(state.borrowed);
    EXPECT_EQ(attempts, 99U);
}

TEST_F(FifoAcquisition, PlatformCleanupRetriesBusyAndIoThenRecordsJoin) {
    uint32_t attempts = 99U;
    EXPECT_CALL(uart, platform_stop())
        .WillOnce(Return(NX_ERROR_BUSY))
        .WillOnce(Return(NX_ERROR_IO))
        .WillOnce(Return(NX_SUCCESS));
    EXPECT_EQ(
        fifo_acquisition_join_platform(&state, platform_stop, 3U, &attempts),
        NX_SUCCESS);
    EXPECT_EQ(attempts, 3U);
    EXPECT_EQ(state.phase, FIFO_ACQUISITION_COLD);
}

TEST_F(FifoAcquisition, PlatformCleanupBudgetDoesNotInventReleaseOrTimeout) {
    uint32_t attempts = 99U;
    EXPECT_CALL(uart, platform_stop())
        .Times(2)
        .WillRepeatedly(Return(NX_ERROR_BUSY));
    EXPECT_EQ(
        fifo_acquisition_join_platform(&state, platform_stop, 2U, &attempts),
        NX_ERROR_BUSY);
    EXPECT_EQ(attempts, 2U);
    EXPECT_EQ(state.phase, FIFO_ACQUISITION_COLD);
}

TEST_F(FifoAcquisition, PlatformCleanupFollowsActualUartStopAndStopsOnSuccess) {
    initialize();
    EXPECT_CALL(uart, submit(_)).WillOnce(Return(NX_ERROR_BUSY));
    fifo_acquisition_step(&state, 100U);
    fifo_acquisition_close(&state, 110U);
    EXPECT_CALL(uart, stop()).WillOnce(Return(NX_SUCCESS));
    fifo_acquisition_step(&state, 110U);
    ASSERT_EQ(state.phase, FIFO_ACQUISITION_CLOSED);
    uint32_t attempts = 99U;
    EXPECT_CALL(uart, platform_stop()).Times(1).WillOnce(Return(NX_SUCCESS));
    EXPECT_EQ(fifo_acquisition_join_platform(&state, platform_stop,
                                             FIFO_ACQUISITION_CLEANUP_ATTEMPTS,
                                             &attempts),
              NX_SUCCESS);
    EXPECT_EQ(attempts, 1U);
}

TEST_F(FifoAcquisition, InvalidPlatformCleanupBudgetOrClockStaysCold) {
    uint32_t attempts = 99U;
    EXPECT_CALL(uart, platform_stop()).Times(0);
    EXPECT_EQ(
        fifo_acquisition_join_platform(&state, platform_stop, 0U, &attempts),
        NX_ERROR_INVALID);
    EXPECT_EQ(fifo_acquisition_join_platform(
                  &state, platform_stop, FIFO_ACQUISITION_CLEANUP_ATTEMPTS + 1U,
                  &attempts),
              NX_ERROR_INVALID);
    EXPECT_EQ(fifo_acquisition_join_platform(&state, nullptr, 1U, &attempts),
              NX_ERROR_INVALID);
    state.clock_fault = true;
    EXPECT_EQ(
        fifo_acquisition_join_platform(&state, platform_stop, 1U, &attempts),
        NX_ERROR_STATE);
    EXPECT_EQ(attempts, 99U);
}
} /* namespace */
