/**
 * \file            finite_dma_test.cpp
 * \brief           Finite SPI DMA application retains accepted storage
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-23
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "finite_dma.h"
#include <array>
#include <cstring>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace {
using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::StrictMock;

class Ports {
  public:
    MOCK_METHOD(nx_result_t, submit, (nx_spi_request_t*));
    MOCK_METHOD(nx_result_t, cancel, (nx_spi_request_t*));
    MOCK_METHOD(void, service, ());
    MOCK_METHOD(nx_result_t, stop, ());
};
nx_result_t submit(void* p, nx_spi_request_t* r) {
    return static_cast<Ports*>(p)->submit(r);
}
nx_result_t cancel(void* p, nx_spi_request_t* r) {
    return static_cast<Ports*>(p)->cancel(r);
}
void service(void* p) {
    static_cast<Ports*>(p)->service();
}
nx_result_t stop(void* p) {
    return static_cast<Ports*>(p)->stop();
}
const nx_spi_ops_t ops{nullptr, cancel, service, stop, nullptr};
unsigned identity_calls;
bool on_port(const void* context, const nx_spi_port_t* port) {
    ++identity_calls;
    return context == port->context;
}
const nx_spi_endpoint_ops_t endpoint_ops{nullptr, submit, nullptr, on_port,
                                         nullptr};

class FiniteSpi : public ::testing::Test {
  protected:
    void SetUp() override {
        identity_calls = 0U;
    }
    StrictMock<Ports> ports;
    nx_spi_port_t port{&ops, &ports};
    nx_spi_endpoint_t endpoint{&endpoint_ops, &ports};
    example_finite_spi_t app{};
    const uint8_t bytes[3]{1U, 2U, 3U};
    uint8_t rx[3]{};
    void accept() {
        EXPECT_CALL(ports, submit(_)).WillOnce(Invoke([](auto* r) {
            return nx_request_admit(&r->base, NX_REQUEST_ACTIVE);
        }));
        ASSERT_EQ(example_finite_spi_start(&app, &port, &endpoint, bytes, rx,
                                           sizeof(bytes), 10U, 100U),
                  NX_SUCCESS);
    }
};

TEST_F(FiniteSpi, ReceiveCannotOverwriteApplicationStopState) {
    std::array<uint8_t, sizeof(app)> before{};
    std::memcpy(before.data(), &app, sizeof(app));
    auto* receive = reinterpret_cast<uint8_t*>(&app.stopped);
    EXPECT_EQ(example_finite_spi_start(&app, &port, &endpoint, bytes, receive,
                                       1U, 10U, 100U),
              NX_ERROR_INVALID);
    EXPECT_EQ(identity_calls, 0U);
    EXPECT_EQ(std::memcmp(before.data(), &app, sizeof(app)), 0);
}

TEST_F(FiniteSpi, TransmitCannotAliasApplicationMetadata) {
    std::array<uint8_t, sizeof(app)> before{};
    std::memcpy(before.data(), &app, sizeof(app));
    const auto* transmit = reinterpret_cast<const uint8_t*>(&app.result);
    EXPECT_EQ(example_finite_spi_start(&app, &port, &endpoint, transmit, rx,
                                       sizeof(app.result), 10U, 100U),
              NX_ERROR_INVALID);
    EXPECT_EQ(identity_calls, 0U);
    EXPECT_EQ(std::memcmp(before.data(), &app, sizeof(app)), 0);
}

TEST_F(FiniteSpi, WrappingSpanRejectsWithoutPortCallsOrControlChanges) {
    std::array<uint8_t, sizeof(app)> before{};
    std::memcpy(before.data(), &app, sizeof(app));
    auto* receive = reinterpret_cast<uint8_t*>(UINTPTR_MAX - 1U);
    EXPECT_EQ(example_finite_spi_start(&app, &port, &endpoint, bytes, receive,
                                       4U, 10U, 100U),
              NX_ERROR_INVALID);
    EXPECT_EQ(identity_calls, 0U);
    EXPECT_EQ(std::memcmp(before.data(), &app, sizeof(app)), 0);
}

TEST_F(FiniteSpi, RejectsAnEndpointFromAnotherController) {
    nx_spi_port_t other{&ops, nullptr};
    EXPECT_EQ(example_finite_spi_start(&app, &other, &endpoint, bytes, rx,
                                       sizeof(bytes), 10U, 100U),
              NX_ERROR_INVALID);
    EXPECT_FALSE(app.pending);
}

TEST_F(FiniteSpi, RejectsNonfiniteBudgetBeforeProviderAdmission) {
    EXPECT_EQ(example_finite_spi_start(&app, &port, &endpoint, bytes, rx,
                                       sizeof(bytes), NX_DEADLINE_NEVER - 2U,
                                       2U),
              NX_ERROR_EXHAUSTED);
    EXPECT_FALSE(app.pending);
}
TEST_F(FiniteSpi, RejectedProviderRetainsNoLoan) {
    EXPECT_CALL(ports, submit(_)).WillOnce(Return(NX_ERROR_BUSY));
    EXPECT_EQ(example_finite_spi_start(&app, &port, &endpoint, bytes, rx,
                                       sizeof(bytes), 10U, 100U),
              NX_ERROR_BUSY);
    EXPECT_FALSE(app.pending);
    EXPECT_EQ(nx_request_state(&app.request.base), NX_REQUEST_READY);
}
TEST_F(FiniteSpi, AStepDoesNotLoopOrReuseAcceptedPayload) {
    accept();
    ASSERT_EQ(nx_request_transition(&app.request.base, NX_REQUEST_DRAINING),
              NX_SUCCESS);
    EXPECT_CALL(ports, service()).Times(1);
    EXPECT_EQ(example_finite_spi_step(&app), NX_ERROR_BUSY);
    EXPECT_TRUE(app.pending);
    EXPECT_EQ(app.request.tx, bytes);
    EXPECT_EQ(app.request.rx, rx);
    EXPECT_EQ(app.request.base.deadline, 110U);
    EXPECT_EQ(example_finite_spi_start(&app, &port, &endpoint, bytes, nullptr,
                                       3U, 20U, 20U),
              NX_ERROR_BUSY);
}
TEST_F(FiniteSpi, QuarantineAndCancelRetainBorrowUntilRealSettlement) {
    accept();
    ASSERT_EQ(nx_request_transition(&app.request.base, NX_REQUEST_QUARANTINED),
              NX_SUCCESS);
    EXPECT_CALL(ports, cancel(&app.request)).WillOnce(Return(NX_SUCCESS));
    EXPECT_EQ(example_finite_spi_request_stop(&app), NX_SUCCESS);
    EXPECT_CALL(ports, service()).Times(1);
    EXPECT_EQ(example_finite_spi_finish(&app), NX_ERROR_BUSY);
    EXPECT_TRUE(app.pending);
    EXPECT_FALSE(app.stopped);
    EXPECT_CALL(ports, service()).WillOnce(Invoke([&] {
        nx_request_settle(&app.request.base, NX_ERROR_CANCELLED, 1U);
    }));
    EXPECT_EQ(example_finite_spi_step(&app), NX_ERROR_CANCELLED);
    EXPECT_FALSE(app.pending);
    EXPECT_EQ(app.transferred, 1U);
    EXPECT_CALL(ports, stop()).WillOnce(Return(NX_ERROR_BUSY));
    EXPECT_EQ(example_finite_spi_finish(&app), NX_ERROR_BUSY);
    EXPECT_FALSE(app.stopped);
    EXPECT_CALL(ports, stop()).WillOnce(Return(NX_SUCCESS));
    EXPECT_EQ(example_finite_spi_finish(&app), NX_SUCCESS);
    EXPECT_TRUE(app.stopped);
}
TEST_F(FiniteSpi, ReportsIncompleteSuccessAndPreservesFinalCount) {
    accept();
    EXPECT_CALL(ports, service()).WillOnce(Invoke([&] {
        nx_request_settle(&app.request.base, NX_SUCCESS, 2U);
    }));
    EXPECT_EQ(example_finite_spi_step(&app), NX_ERROR_IO);
    EXPECT_FALSE(app.pending);
    EXPECT_EQ(app.transferred, 2U);
}
} /* namespace */
