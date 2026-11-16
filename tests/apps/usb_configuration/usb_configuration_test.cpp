/**
 * \file            usb_configuration_test.cpp
 * \brief           Real C application and EP0 helper against external USB ports
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-05
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "usb_configuration.h"
#include <array>
#include <cstring>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <string>
#include <vector>
using testing::_;
using testing::NiceMock;
namespace {
nx_time_us_t clock_us = 100U;
class Ports {
  public:
    MOCK_METHOD(nx_result_t, Service, ());
    MOCK_METHOD(nx_result_t, State, (nx_usb_device_state_t*));
    MOCK_METHOD(nx_result_t, Events, (nx_usb_event_t*, size_t, size_t*));
    MOCK_METHOD(nx_result_t, Address, (uint64_t, uint8_t));
    MOCK_METHOD(nx_result_t, Stop, ());
    MOCK_METHOD(nx_result_t, Reject, (uint64_t));
    MOCK_METHOD(nx_result_t, Activate, (uint64_t, uint8_t, bool));
    MOCK_METHOD(nx_result_t, Submit, (unsigned, nx_usb_request_t*));
    MOCK_METHOD(nx_result_t, Cancel, (unsigned, nx_usb_request_t*));
    MOCK_METHOD(nx_result_t, Stall, (unsigned, bool));
};
struct Endpoint {
    Ports* ports;
    unsigned slot;
};
nx_result_t Service(void* c) {
    return static_cast<Ports*>(c)->Service();
}
nx_result_t State(void* c, nx_usb_device_state_t* s) {
    return static_cast<Ports*>(c)->State(s);
}
nx_result_t Events(void* c, nx_usb_event_t* e, size_t n, size_t* count) {
    return static_cast<Ports*>(c)->Events(e, n, count);
}
nx_result_t Address(void* c, uint64_t e, uint8_t value) {
    return static_cast<Ports*>(c)->Address(e, value);
}
nx_result_t Stop(void* c) {
    return static_cast<Ports*>(c)->Stop();
}
nx_result_t Reject(void* c, uint64_t e) {
    return static_cast<Ports*>(c)->Reject(e);
}
nx_result_t Activate(void* c, uint64_t e, uint8_t address, bool active) {
    return static_cast<Ports*>(c)->Activate(e, address, active);
}
nx_result_t Submit(void* c, nx_usb_request_t* r) {
    auto* ep = static_cast<Endpoint*>(c);
    return ep->ports->Submit(ep->slot, r);
}
nx_result_t Cancel(void* c, nx_usb_request_t* r) {
    auto* ep = static_cast<Endpoint*>(c);
    return ep->ports->Cancel(ep->slot, r);
}
nx_result_t Stall(void* c, bool stalled) {
    auto* ep = static_cast<Endpoint*>(c);
    return ep->ports->Stall(ep->slot, stalled);
}
const nx_usb_endpoint_ops_t endpoint_ops{Submit, Cancel, Stall};
const nx_usb_device_ops_t device_ops{Service, Events, State,   Address,
                                     nullptr, Stop,   nullptr, nullptr};
const nx_usb_activation_ops_t activation_ops{Activate};
const nx_usb_setup_decision_ops_t decision_ops{Reject};
class UsbConfiguration : public testing::Test {
  protected:
    NiceMock<Ports> ports;
    Endpoint contexts[3]{{&ports, 0U}, {&ports, 1U}, {&ports, 2U}};
    nx_usb_endpoint_port_t faces[3]{{&endpoint_ops, &contexts[0]},
                                    {&endpoint_ops, &contexts[1]},
                                    {&endpoint_ops, &contexts[2]}};
    nx_usb_device_port_t device{&device_ops, &ports};
    nx_usb_activation_port_t activation{&activation_ops, &ports};
    nx_usb_endpoint_profile_t endpoints[3]{
        {&faces[0], 64U, 0U, true, NX_USB_PROFILE_CONTROL},
        {&faces[1], 64U, 0U, false, NX_USB_PROFILE_CONTROL},
        {&faces[2], 64U, 1U, true, NX_USB_PROFILE_BULK}};
    nx_usb_device_profile_t profile{&device,
                                    endpoints,
                                    3U,
                                    48000000U,
                                    320U,
                                    96U,
                                    4U,
                                    NX_USB_PROFILE_DEVICE,
                                    NX_USB_PROFILE_FULL_SPEED,
                                    NX_USB_PROFILE_INTERNAL_FS,
                                    NX_USB_PROFILE_PIO,
                                    {&decision_ops, &ports}};
    example_usb_t app{};
    nx_usb_device_state_t observed{};
    std::array<nx_usb_request_t*, 3> pending{};
    std::vector<std::string> actions;
    bool endpoint_active = false;
    bool hold_cancel = false;
    bool hold_stop = false;
    bool reset_event = false;
    uint8_t staged_address = 0U;
    bool address_staged = false;
    uint64_t rejected_epoch = 0U;
    nx_result_t initialized = NX_ERROR_STATE;
    void SetUp() override {
        clock_us = 100U;
        observed.started = true;
        observed.address = 7U;
        ON_CALL(ports, Service()).WillByDefault([] { return NX_SUCCESS; });
        ON_CALL(ports, State(_)).WillByDefault([&](auto* out) {
            *out = observed;
            return NX_SUCCESS;
        });
        ON_CALL(ports, Events(_, _, _))
            .WillByDefault([&](auto* events, size_t, size_t* count) {
                *count = reset_event ? 1U : 0U;
                if (reset_event) {
                    events[0] = {};
                    events[0].kind = NX_USB_EVENT_RESET;
                    reset_event = false;
                }
                return *count == 0U ? NX_ERROR_EMPTY : NX_SUCCESS;
            });
        ON_CALL(ports, Activate(_, _, _))
            .WillByDefault([&](uint64_t epoch, uint8_t address, bool active) {
                if (epoch != observed.epoch || address != 0x81U)
                    return NX_ERROR_STATE;
                if (pending[2] != nullptr)
                    return NX_ERROR_BUSY;
                endpoint_active = active;
                actions.emplace_back(active ? "activate" : "deactivate");
                return NX_SUCCESS;
            });
        ON_CALL(ports, Submit(_, _))
            .WillByDefault([&](unsigned slot, nx_usb_request_t* request) {
                if (pending[slot] != nullptr)
                    return NX_ERROR_BUSY;
                if (slot == 2U && !endpoint_active)
                    return NX_ERROR_STATE;
                const auto result =
                    nx_request_admit(&request->base, NX_REQUEST_ACTIVE);
                if (result == NX_SUCCESS) {
                    pending[slot] = request;
                    actions.emplace_back(slot == 2U ? "data" : "control");
                }
                return result;
            });
        ON_CALL(ports, Cancel(_, _))
            .WillByDefault([&](unsigned slot, nx_usb_request_t* request) {
                if (pending[slot] != request)
                    return NX_ERROR_STATE;
                actions.emplace_back("cancel");
                if (hold_cancel) {
                    (void)nx_request_transition(&request->base,
                                                NX_REQUEST_DRAINING);
                } else {
                    pending[slot] = nullptr;
                    nx_request_settle(&request->base, NX_ERROR_CANCELLED, 0U);
                }
                return NX_SUCCESS;
            });
        ON_CALL(ports, Stop()).WillByDefault([&] {
            observed.closing = true;
            if (hold_stop)
                return NX_ERROR_BUSY;
            for (auto*& request : pending) {
                if (request != nullptr) {
                    auto* ended = request;
                    request = nullptr;
                    nx_request_settle(&ended->base, NX_ERROR_CANCELLED, 0U);
                }
            }
            endpoint_active = false;
            address_staged = false;
            observed.started = false;
            return NX_SUCCESS;
        });
        ON_CALL(ports, Address(_, _))
            .WillByDefault([&](uint64_t epoch, uint8_t value) {
                if (epoch != observed.epoch)
                    return NX_ERROR_STATE;
                staged_address = value;
                address_staged = true;
                actions.emplace_back("address");
                return NX_SUCCESS;
            });
        ON_CALL(ports, Reject(_)).WillByDefault([&](uint64_t epoch) {
            rejected_epoch = epoch;
            observed.control_pending = false;
            return NX_SUCCESS;
        });
        initialized = example_usb_initialize(&app, &profile, &activation);
    }
    void TearDown() override {
        hold_cancel = hold_stop = false;
        if (app.profile != nullptr) {
            for (unsigned i = 0U; i < 32U && !app.closed; ++i)
                (void)example_usb_close(&app);
            EXPECT_TRUE(app.closed);
        }
    }
    void Setup(uint8_t type, uint8_t request, uint16_t value,
               uint16_t length = 0U) {
        ++observed.epoch;
        observed.setup = {type, request, value, 0U, length, observed.epoch};
        observed.control_pending = true;
    }
    bool Packet(unsigned slot) {
        for (unsigned i = 0U; i < 20U && pending[slot] == nullptr; ++i)
            (void)example_usb_step(&app);
        return pending[slot] != nullptr;
    }
    void Complete(unsigned slot, nx_result_t result = NX_SUCCESS) {
        ASSERT_NE(pending[slot], nullptr);
        auto* request = pending[slot];
        pending[slot] = nullptr;
        if (request->stage == NX_USB_STATUS && result == NX_SUCCESS) {
            observed.control_pending = false;
            if (address_staged) {
                observed.address = staged_address;
                address_staged = false;
            }
        }
        nx_request_settle(&request->base, result,
                          result == NX_SUCCESS ? request->length : 0U);
    }
    void Configured() {
        Setup(0U, 9U, 1U);
        ASSERT_TRUE(Packet(0U));
        ASSERT_TRUE(endpoint_active);
        ASSERT_FALSE(app.ready);
        Complete(0U);
        ASSERT_EQ(example_usb_step(&app), NX_SUCCESS);
        ASSERT_TRUE(app.ready);
    }
};
TEST_F(UsbConfiguration, InitializationIsPureAndDoesNotPublishReadiness) {
    ASSERT_EQ(initialized, NX_SUCCESS);
    EXPECT_TRUE(actions.empty());
    EXPECT_FALSE(app.ready);
    EXPECT_FALSE(endpoint_active);
}
TEST_F(UsbConfiguration, EmptyEventRingIsNormalAndDoesNotCloseTheDevice) {
    ASSERT_EQ(initialized, NX_SUCCESS);
    EXPECT_EQ(example_usb_step(&app), NX_SUCCESS);
    EXPECT_FALSE(app.stopping);
    EXPECT_FALSE(app.closed);
    EXPECT_EQ(app.failure, NX_SUCCESS);
    EXPECT_FALSE(app.ready);
}
TEST_F(UsbConfiguration, ConfigurationStagesBeforeStatusAndCommitsAfterAck) {
    ASSERT_EQ(initialized, NX_SUCCESS);
    const uint8_t sample[8]{};
    Setup(0U, 9U, 1U);
    ASSERT_TRUE(Packet(0U));
    ASSERT_EQ(actions.size(), 2U);
    EXPECT_EQ(actions[0], "activate");
    EXPECT_EQ(actions[1], "control");
    EXPECT_FALSE(app.ready);
    EXPECT_EQ(example_usb_send(&app, sample), NX_ERROR_STATE);
    Complete(0U);
    ASSERT_EQ(example_usb_step(&app), NX_SUCCESS);
    EXPECT_TRUE(app.ready);
    EXPECT_EQ(app.configuration, 1U);
    ASSERT_EQ(example_usb_send(&app, sample), NX_SUCCESS);
    EXPECT_NE(pending[2], nullptr);
}
TEST_F(UsbConfiguration, DeconfigurationWaitsForActualDataLoanDrain) {
    ASSERT_EQ(initialized, NX_SUCCESS);
    ASSERT_NO_FATAL_FAILURE(Configured());
    const uint8_t sample[8]{};
    ASSERT_EQ(example_usb_send(&app, sample), NX_SUCCESS);
    hold_cancel = true;
    Setup(0U, 9U, 0U);
    for (unsigned i = 0U; i < 4U; ++i)
        (void)example_usb_step(&app);
    EXPECT_FALSE(app.ready);
    EXPECT_TRUE(endpoint_active);
    EXPECT_NE(pending[2], nullptr);
    EXPECT_EQ(pending[0], nullptr);
    Complete(2U, NX_ERROR_CANCELLED);
    ASSERT_TRUE(Packet(0U));
    EXPECT_FALSE(endpoint_active);
    Complete(0U);
    ASSERT_EQ(example_usb_step(&app), NX_SUCCESS);
    EXPECT_EQ(app.configuration, 0U);
    EXPECT_FALSE(app.ready);
}
TEST_F(UsbConfiguration, SupersededStagingClosesRatherThanPublishingReady) {
    ASSERT_EQ(initialized, NX_SUCCESS);
    Setup(0U, 9U, 1U);
    ASSERT_TRUE(Packet(0U));
    Setup(0x80U, 8U, 0U, 1U);
    for (unsigned i = 0U; i < 12U && !app.closed; ++i)
        (void)example_usb_step(&app);
    EXPECT_FALSE(app.ready);
    EXPECT_TRUE(app.closed);
    EXPECT_FALSE(endpoint_active);
}
TEST_F(UsbConfiguration, OldStatusAckCannotCommitANewerSetupEpoch) {
    ASSERT_EQ(initialized, NX_SUCCESS);
    Setup(0U, 9U, 1U);
    ASSERT_TRUE(Packet(0U));
    Complete(0U);
    Setup(0x80U, 8U, 0U, 1U);
    for (unsigned i = 0U; i < 12U && !app.closed; ++i)
        (void)example_usb_step(&app);
    EXPECT_FALSE(app.ready);
    EXPECT_TRUE(app.closed);
}
TEST_F(UsbConfiguration, ResetAndHeldStopRetainStorageUntilJoinAndRestart) {
    ASSERT_EQ(initialized, NX_SUCCESS);
    ASSERT_NO_FATAL_FAILURE(Configured());
    const uint8_t sample[8]{1U, 2U, 3U};
    ASSERT_EQ(example_usb_send(&app, sample), NX_SUCCESS);
    hold_cancel = hold_stop = true;
    ++observed.epoch;
    observed.address = 0U;
    reset_event = true;
    (void)example_usb_step(&app);
    EXPECT_FALSE(app.ready);
    EXPECT_FALSE(app.closed);
    EXPECT_EQ(example_usb_close(&app), NX_ERROR_BUSY);
    EXPECT_TRUE(app.tx_borrowed);
    EXPECT_NE(nx_request_state(&app.tx.base), NX_REQUEST_SETTLED);
    hold_cancel = hold_stop = false;
    for (unsigned i = 0U; i < 12U && !app.closed; ++i)
        (void)example_usb_close(&app);
    ASSERT_TRUE(app.closed);
    EXPECT_FALSE(app.tx_borrowed);
    observed.started = true;
    observed.closing = false;
    observed.address = 7U;
    ASSERT_EQ(example_usb_initialize(&app, &profile, &activation), NX_SUCCESS);
    EXPECT_FALSE(app.ready);
    ASSERT_NO_FATAL_FAILURE(Configured());
}
TEST_F(UsbConfiguration, UnknownConfigurationStallsWithoutEndpointEffects) {
    ASSERT_EQ(initialized, NX_SUCCESS);
    Setup(0U, 9U, 2U);
    (void)example_usb_step(&app);
    EXPECT_EQ(rejected_epoch, observed.epoch);
    EXPECT_TRUE(actions.empty());
    EXPECT_FALSE(app.ready);
}
TEST_F(UsbConfiguration, UnsupportedEmptyDescriptorStallsWithoutReplyLoop) {
    ASSERT_EQ(initialized, NX_SUCCESS);
    Setup(0x80U, 6U, 0x0100U, 0U);
    (void)example_usb_step(&app);
    EXPECT_EQ(rejected_epoch, observed.epoch);
    EXPECT_TRUE(actions.empty());
    EXPECT_FALSE(app.reply_borrowed);
}
TEST_F(UsbConfiguration, BusyStagingHonorsTheOriginalAbsoluteDeadline) {
    ASSERT_EQ(initialized, NX_SUCCESS);
    ON_CALL(ports, Activate(_, _, _))
        .WillByDefault([](uint64_t, uint8_t, bool) { return NX_ERROR_BUSY; });
    Setup(0U, 9U, 1U);
    for (unsigned i = 0U; i < 4U; ++i)
        (void)example_usb_step(&app);
    ASSERT_FALSE(app.ready);
    ASSERT_FALSE(app.closed);
    const auto deadline = app.deadline;
    clock_us = deadline;
    for (unsigned i = 0U; i < 4U && !app.closed; ++i)
        (void)example_usb_step(&app);
    EXPECT_EQ(app.deadline, deadline);
    EXPECT_TRUE(app.closed);
    EXPECT_EQ(app.failure, NX_ERROR_TIMEOUT);
    EXPECT_FALSE(app.ready);
}
TEST_F(UsbConfiguration, FailedStatusKeepsAdmissionClosedUntilStopProof) {
    ASSERT_EQ(initialized, NX_SUCCESS);
    Setup(0U, 9U, 1U);
    ASSERT_TRUE(Packet(0U));
    hold_stop = true;
    Complete(0U, NX_ERROR_IO);
    for (unsigned i = 0U; i < 4U; ++i)
        (void)example_usb_step(&app);
    EXPECT_FALSE(app.ready);
    EXPECT_FALSE(app.closed);
    EXPECT_TRUE(endpoint_active);
    EXPECT_EQ(app.failure, NX_ERROR_IO);
    hold_stop = false;
    for (unsigned i = 0U; i < 8U && !app.closed; ++i)
        (void)example_usb_close(&app);
    EXPECT_TRUE(app.closed);
    EXPECT_FALSE(endpoint_active);
}
TEST_F(UsbConfiguration, PartialActivationFailureRetainsControllerUntilStop) {
    ASSERT_EQ(initialized, NX_SUCCESS);
    ON_CALL(ports, Activate(_, _, _))
        .WillByDefault([&](uint64_t, uint8_t, bool) {
            endpoint_active = true;
            observed.closing = true;
            return NX_ERROR_IO;
        });
    hold_stop = true;
    Setup(0U, 9U, 1U);
    for (unsigned i = 0U; i < 4U; ++i)
        (void)example_usb_step(&app);
    EXPECT_FALSE(app.ready);
    EXPECT_FALSE(app.closed);
    EXPECT_TRUE(endpoint_active);
    EXPECT_EQ(app.failure, NX_ERROR_IO);
    hold_stop = false;
    for (unsigned i = 0U; i < 8U && !app.closed; ++i)
        (void)example_usb_close(&app);
    EXPECT_TRUE(app.closed);
    EXPECT_FALSE(endpoint_active);
}
TEST_F(UsbConfiguration, DescriptorsAndAddressUseTheRealControlPacketizer) {
    ASSERT_EQ(initialized, NX_SUCCESS);
    observed.address = 0U;
    Setup(0x80U, 6U, 0x0100U, 64U);
    ASSERT_TRUE(Packet(0U));
    ASSERT_EQ(pending[0]->length, 18U);
    EXPECT_EQ(pending[0]->in[1], 1U);
    Complete(0U);
    ASSERT_TRUE(Packet(1U));
    EXPECT_EQ(pending[1]->stage, NX_USB_STATUS);
    Complete(1U);
    (void)example_usb_step(&app);
    Setup(0U, 5U, 7U);
    ASSERT_TRUE(Packet(0U));
    EXPECT_EQ(observed.address, 0U);
    EXPECT_EQ(staged_address, 7U);
    Complete(0U);
    (void)example_usb_step(&app);
    EXPECT_EQ(observed.address, 7U);
    EXPECT_FALSE(app.ready);
}
TEST_F(UsbConfiguration, AddressZeroCommitsOnlyAfterTheActualStatusAck) {
    ASSERT_EQ(initialized, NX_SUCCESS);
    ASSERT_EQ(observed.address, 7U);
    Setup(0U, 5U, 0U);
    ASSERT_TRUE(Packet(0U));
    EXPECT_EQ(observed.address, 7U);
    Complete(0U);
    EXPECT_EQ(example_usb_step(&app), NX_SUCCESS);
    EXPECT_EQ(observed.address, 0U);
    EXPECT_FALSE(app.ready);
    EXPECT_FALSE(app.closed);
}
} /* namespace */
extern "C" nx_time_us_t nx_time_now_us(void) {
    return clock_us;
}
