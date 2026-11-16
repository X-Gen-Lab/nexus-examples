/**
 * \file            network_gateway_frames_test.cpp
 * \brief           Actual datagram copies and exact frame-loan withdrawal
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-02
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "native/native_frames.h"
#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <gtest/gtest.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {
class GatewayNativeFrames : public testing::Test {
  protected:
    int pair[2]{-1, -1};
    nx_time_us_t now = 100U;
    example_gateway_native_frames_t port{};
    std::array<uint8_t, 64> frame{};

    static nx_time_us_t read(void* context) {
        return *static_cast<nx_time_us_t*>(context);
    }
    void SetUp() override {
        ASSERT_EQ(socketpair(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK, 0, pair), 0);
        frame[0] = 2U;
        frame[6] = 4U;
        frame[12] = 8U;
        frame[13] = 0U;
        frame[63] = 0xA5U;
    }
    void TearDown() override {
        if (pair[0] >= 0) {
            close(pair[0]);
        }
        if (pair[1] >= 0) {
            close(pair[1]);
        }
    }
    void open() {
        ASSERT_EQ(example_gateway_native_frames_init(&port, pair[0],
                                                     nx_clock_t{read, &now}),
                  NX_SUCCESS);
    }
    void ingress(const uint8_t* bytes, size_t count) {
        ASSERT_EQ(send(pair[1], bytes, count, MSG_NOSIGNAL),
                  static_cast<ssize_t>(count));
    }
};

TEST_F(GatewayNativeFrames, InvalidBindingHasNoDescriptorOrPortEffects) {
    const auto original = port;
    EXPECT_EQ(
        example_gateway_native_frames_init(&port, -1, nx_clock_t{read, &now}),
        NX_ERROR_INVALID);
    EXPECT_EQ(std::memcmp(&port, &original, sizeof(port)), 0);
    ASSERT_EQ(fcntl(pair[0], F_SETFL, 0), 0);
    EXPECT_EQ(example_gateway_native_frames_init(&port, pair[0],
                                                 nx_clock_t{read, &now}),
              NX_ERROR_INVALID);
    EXPECT_EQ(fcntl(pair[0], F_GETFL) & O_NONBLOCK, 0);
    EXPECT_EQ(std::memcmp(&port, &original, sizeof(port)), 0);
}

TEST_F(GatewayNativeFrames, ScatterTransmitSettlesOnlyAfterTheKernelCopy) {
    open();
    nx_eth_frames_tx_request_t request{};
    nx_request_initialize(&request.base);
    const nx_eth_fragment_t fragments[2]{{frame.data(), 13U},
                                         {frame.data() + 13U, 51U}};
    ASSERT_EQ(nx_eth_frames_tx_prepare(&request, fragments, 2U, true, 200U),
              NX_SUCCESS);
    ASSERT_EQ(nx_eth_frames_port_submit(&port.face, &request), NX_SUCCESS);
    frame.fill(0U);
    nx_result_t result = NX_ERROR_STATE;
    size_t copied = 0U;
    ASSERT_EQ(nx_request_result(&request.base, &result, &copied), NX_SUCCESS);
    EXPECT_EQ(result, NX_SUCCESS);
    EXPECT_EQ(copied, 64U);
    EXPECT_EQ(request.stamp.source, NX_IO_TIMESTAMP_UNKNOWN);
    std::array<uint8_t, 64> received{};
    ASSERT_EQ(recv(pair[1], received.data(), received.size(), MSG_DONTWAIT),
              64);
    EXPECT_EQ(received[12], 8U);
    EXPECT_EQ(received[63], 0xA5U);
}

TEST_F(GatewayNativeFrames, ExpiredTransmitRejectsWithoutBorrowOrDatagram) {
    open();
    nx_eth_frames_tx_request_t request{};
    nx_request_initialize(&request.base);
    const nx_eth_fragment_t fragment{frame.data(), frame.size()};
    ASSERT_EQ(nx_eth_frames_tx_prepare(&request, &fragment, 1U, false, now),
              NX_SUCCESS);
    EXPECT_EQ(nx_eth_frames_port_submit(&port.face, &request),
              NX_ERROR_TIMEOUT);
    EXPECT_EQ(nx_request_state(&request.base), NX_REQUEST_READY);
    std::array<uint8_t, 64> received{};
    EXPECT_EQ(recv(pair[1], received.data(), received.size(), MSG_DONTWAIT),
              -1);
    EXPECT_TRUE(errno == EAGAIN || errno == EWOULDBLOCK);
}

TEST_F(GatewayNativeFrames, ExactReleaseRetainsImmutableRxAndQueuedNextFrame) {
    open();
    ingress(frame.data(), frame.size());
    nx_eth_frames_rx_loan_t loan{};
    nx_eth_fragment_t view{};
    ASSERT_EQ(nx_eth_frames_port_acquire(&port.face, &loan, &view, 1U),
              NX_SUCCESS);
    const auto original = loan;
    EXPECT_EQ(loan.length, frame.size());
    EXPECT_EQ(loan.stamp.source, NX_IO_TIMESTAMP_UNKNOWN);
    EXPECT_EQ(loan.acquired_source, NX_IO_TIMESTAMP_ACQUIRE_OBSERVATION);
    frame[63] = 0x5AU;
    ingress(frame.data(), frame.size());
    nx_eth_frames_rx_loan_t next{};
    nx_eth_fragment_t next_view{};
    EXPECT_EQ(nx_eth_frames_port_acquire(&port.face, &next, &next_view, 1U),
              NX_ERROR_BUSY);
    EXPECT_EQ(view.data[63], 0xA5U);
    auto altered = loan;
    ++altered.length;
    EXPECT_EQ(nx_eth_frames_port_release(&port.face, &altered), NX_ERROR_STATE);
    ASSERT_EQ(nx_eth_frames_port_release(&port.face, &loan), NX_SUCCESS);
    EXPECT_EQ(nx_eth_frames_port_release(&port.face, &loan), NX_ERROR_STATE);
    ASSERT_EQ(nx_eth_frames_port_acquire(&port.face, &next, &next_view, 1U),
              NX_SUCCESS);
    EXPECT_EQ(next_view.data[63], 0x5AU);
    EXPECT_GT(next.epoch, original.epoch);
    EXPECT_EQ(nx_eth_frames_port_release(&port.face, &original),
              NX_ERROR_STATE);
    EXPECT_EQ(nx_eth_frames_port_release(&port.face, &next), NX_SUCCESS);
}

TEST_F(GatewayNativeFrames, StopRetainsHeldRxAndNeverClosesTheBorrowedFd) {
    open();
    ingress(frame.data(), frame.size());
    nx_eth_frames_rx_loan_t loan{};
    nx_eth_fragment_t view{};
    ASSERT_EQ(nx_eth_frames_port_acquire(&port.face, &loan, &view, 1U),
              NX_SUCCESS);
    EXPECT_EQ(nx_eth_frames_port_stop(&port.face), NX_ERROR_BUSY);
    EXPECT_EQ(view.data[63], 0xA5U);
    nx_eth_frames_rx_loan_t rejected{};
    nx_eth_fragment_t rejected_view{};
    EXPECT_EQ(
        nx_eth_frames_port_acquire(&port.face, &rejected, &rejected_view, 1U),
        NX_ERROR_STATE);
    ASSERT_EQ(nx_eth_frames_port_release(&port.face, &loan), NX_SUCCESS);
    ASSERT_EQ(nx_eth_frames_port_stop(&port.face), NX_SUCCESS);
    EXPECT_GE(fcntl(pair[0], F_GETFD), 0);
    EXPECT_EQ(example_gateway_native_frames_init(&port, pair[0],
                                                 nx_clock_t{read, &now}),
              NX_SUCCESS);
    ingress(frame.data(), frame.size());
    ASSERT_EQ(
        nx_eth_frames_port_acquire(&port.face, &rejected, &rejected_view, 1U),
        NX_SUCCESS);
    EXPECT_GT(rejected.epoch, loan.epoch);
    EXPECT_EQ(nx_eth_frames_port_release(&port.face, &loan), NX_ERROR_STATE);
    EXPECT_EQ(nx_eth_frames_port_release(&port.face, &rejected), NX_SUCCESS);
}

TEST_F(GatewayNativeFrames, KernelFailureAfterAdmissionSettlesAnActualError) {
    open();
    ASSERT_EQ(close(pair[1]), 0);
    pair[1] = -1;
    nx_eth_frames_tx_request_t request{};
    nx_request_initialize(&request.base);
    const nx_eth_fragment_t fragment{frame.data(), frame.size()};
    ASSERT_EQ(nx_eth_frames_tx_prepare(&request, &fragment, 1U, false, 200U),
              NX_SUCCESS);
    ASSERT_EQ(nx_eth_frames_port_submit(&port.face, &request), NX_SUCCESS);
    nx_result_t result = NX_SUCCESS;
    size_t copied = 1U;
    ASSERT_EQ(nx_request_result(&request.base, &result, &copied), NX_SUCCESS);
    EXPECT_EQ(result, NX_ERROR_IO);
    EXPECT_EQ(copied, 0U);
    EXPECT_EQ(nx_eth_frames_port_stop(&port.face), NX_SUCCESS);
}

TEST_F(GatewayNativeFrames, OversizeAndAliasedOutputsNeverCreateAnRxLoan) {
    open();
    const std::array<uint8_t, NX_ETH_FRAME_MAX + 1U> oversized{};
    ingress(oversized.data(), oversized.size());
    nx_eth_frames_rx_loan_t loan{};
    nx_eth_fragment_t view{};
    EXPECT_EQ(nx_eth_frames_port_acquire(&port.face, &loan, &view, 1U),
              NX_ERROR_OVERFLOW);
    EXPECT_EQ(loan.identity, nullptr);
    nx_eth_frames_state_t state{};
    ASSERT_EQ(nx_eth_frames_port_state(&port.face, &state), NX_SUCCESS);
    EXPECT_EQ(state.mac.held_rx, 0U);
    EXPECT_EQ(state.mac.discarded, 1U);
    auto* alias = reinterpret_cast<nx_eth_frames_rx_loan_t*>(port.receive);
    EXPECT_EQ(nx_eth_frames_port_acquire(&port.face, alias, &view, 1U),
              NX_ERROR_INVALID);
}

TEST_F(GatewayNativeFrames, ClockLossClosesNewAdmissionWithoutRevokingHeldRx) {
    open();
    ingress(frame.data(), frame.size());
    nx_eth_frames_rx_loan_t loan{};
    nx_eth_fragment_t view{};
    ASSERT_EQ(nx_eth_frames_port_acquire(&port.face, &loan, &view, 1U),
              NX_SUCCESS);
    now = 99U;
    EXPECT_EQ(nx_eth_frames_port_service(&port.face), NX_ERROR_STATE);
    EXPECT_EQ(view.data[63], 0xA5U);
    EXPECT_EQ(nx_eth_frames_port_release(&port.face, &loan), NX_SUCCESS);
    EXPECT_EQ(nx_eth_frames_port_stop(&port.face), NX_SUCCESS);
}

TEST_F(GatewayNativeFrames, ExplicitLinkDownRejectsTxUntilAQuietUpObservation) {
    open();
    const nx_eth_link_t down{false, true, 100U};
    ASSERT_EQ(nx_eth_frames_port_apply_link(&port.face, &down), NX_SUCCESS);
    nx_eth_frames_tx_request_t request{};
    nx_request_initialize(&request.base);
    const nx_eth_fragment_t fragment{frame.data(), frame.size()};
    ASSERT_EQ(nx_eth_frames_tx_prepare(&request, &fragment, 1U, false, 200U),
              NX_SUCCESS);
    EXPECT_EQ(nx_eth_frames_port_submit(&port.face, &request), NX_ERROR_STATE);
    EXPECT_EQ(nx_request_state(&request.base), NX_REQUEST_READY);
    const nx_eth_link_t up{true, true, 100U};
    ASSERT_EQ(nx_eth_frames_port_apply_link(&port.face, &up), NX_SUCCESS);
    EXPECT_EQ(nx_eth_frames_port_submit(&port.face, &request), NX_SUCCESS);
}

TEST_F(GatewayNativeFrames,
       ReferenceMtuRejectsAnOversizedTaggedTxBeforeAdmission) {
    open();
    std::array<uint8_t, NX_ETH_FRAME_MAX + 4U> tagged{};
    tagged[0] = 2U;
    tagged[6] = 4U;
    tagged[12] = 0x81U;
    tagged[13] = 0U;
    tagged[16] = 8U;
    tagged[17] = 0U;
    nx_eth_frames_tx_request_t request{};
    nx_request_initialize(&request.base);
    const nx_eth_fragment_t fragment{tagged.data(), tagged.size()};
    ASSERT_EQ(nx_eth_frames_tx_prepare(&request, &fragment, 1U, false, 200U),
              NX_SUCCESS);
    EXPECT_EQ(nx_eth_frames_port_submit(&port.face, &request),
              NX_ERROR_INVALID);
    EXPECT_EQ(nx_request_state(&request.base), NX_REQUEST_READY);
    std::array<uint8_t, 1518> received{};
    EXPECT_EQ(recv(pair[1], received.data(), received.size(), MSG_DONTWAIT),
              -1);
    EXPECT_TRUE(errno == EAGAIN || errno == EWOULDBLOCK);
}

TEST_F(GatewayNativeFrames, TamperedRequestCannotAliasItsOwnFragmentMetadata) {
    open();
    nx_eth_frames_tx_request_t request{};
    nx_request_initialize(&request.base);
    const nx_eth_fragment_t fragment{frame.data(), frame.size()};
    ASSERT_EQ(nx_eth_frames_tx_prepare(&request, &fragment, 1U, false, 200U),
              NX_SUCCESS);
    auto* alias = reinterpret_cast<nx_eth_fragment_t*>(&request.stamp);
    std::memcpy(&request.stamp, &fragment, sizeof(fragment));
    request.fragments = alias;
    EXPECT_EQ(nx_eth_frames_port_submit(&port.face, &request),
              NX_ERROR_INVALID);
    EXPECT_EQ(nx_request_state(&request.base), NX_REQUEST_READY);
    std::array<uint8_t, 64> received{};
    EXPECT_EQ(recv(pair[1], received.data(), received.size(), MSG_DONTWAIT),
              -1);
    EXPECT_TRUE(errno == EAGAIN || errno == EWOULDBLOCK);
}
} /* namespace */
