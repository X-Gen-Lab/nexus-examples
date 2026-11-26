/**
 * \file            network_gateway_delivery_test.cpp
 * \brief           Durable business IDs across the actual gateway call seam
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-22
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "gateway_delivery.h"
#include "gateway_managed.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <gtest/gtest.h>
#include <vector>

namespace {
std::vector<uint8_t> stored;
std::vector<uint8_t> wire;
unsigned runs;
unsigned writes;
bool release_session;
bool authenticated;
const nx_flashdb_operation_t* active;
struct RoutePorts {
    bool link = true;
    bool lease = true;
    nx_result_t phy_result = NX_SUCCESS;
    bool dns_ready = true;
    bool lose_lease_in_gateway = false;
    bool busy_phy_in_gateway = false;
    std::array<uint8_t, 4> local{192U, 0U, 2U, 2U};
    std::array<uint8_t, 4> peer{192U, 0U, 2U, 1U};
    std::array<uint8_t, 4> connected{};
    const char* resolved_name = nullptr;
    const char* tls_name = nullptr;
    unsigned dhcp_stops = 0U;
    unsigned dns_cancels = 0U;
    unsigned services = 0U;
} ports;

class GatewayDelivery : public testing::Test {
  protected:
    nx_flashdb_t database{};
    nx_flashdb_operation_t operation{&database, 100U};
    example_outbox_t outbox{};
    example_gateway_delivery_t delivery{};
    example_gateway_config_t config{};
    example_gateway_delivery_report_t report{};
    void SetUp() override {
        stored.clear();
        wire.clear();
        runs = writes = 0U;
        release_session = true;
        authenticated = true;
        ports = {};
        database.opened = true;
        database.kind = NX_FLASHDB_KV;
        active = &operation;
        ASSERT_EQ(example_outbox_load(&outbox, &operation), NX_SUCCESS);
    }
    void TearDown() override {
        active = nullptr;
    }
    void queue() {
        const uint8_t payload[]{10U, 20U};
        ASSERT_EQ(example_outbox_enqueue(&outbox, &operation, 257U, payload,
                                         sizeof(payload)),
                  NX_SUCCESS);
    }
};

TEST_F(GatewayDelivery, EncodesStableIdAndPersistsOnlyConfirmedDelivery) {
    queue();
    ASSERT_EQ(example_gateway_delivery_run(&delivery, &outbox, &operation,
                                           &config, &report),
              NX_SUCCESS);
    EXPECT_TRUE(report.durable_ack);
    EXPECT_TRUE(report.network.delivered);
    ASSERT_EQ(wire.size(), 10U);
    EXPECT_EQ(wire[0], 1U);
    EXPECT_EQ(wire[1], 1U);
    EXPECT_EQ(wire[8], 10U);
    EXPECT_EQ(wire[9], 20U);
    EXPECT_FALSE(outbox.pending);
    EXPECT_EQ(runs, 1U);
}

TEST_F(GatewayDelivery, RetainedGatewayStorageBlocksNewJobUntilCleanup) {
    queue();
    release_session = false;
    ASSERT_EQ(example_gateway_delivery_run(&delivery, &outbox, &operation,
                                           &config, &report),
              NX_ERROR_BUSY);
    EXPECT_TRUE(outbox.pending);
    EXPECT_FALSE(report.durable_ack);
    EXPECT_EQ(example_gateway_delivery_run(&delivery, &outbox, &operation,
                                           &config, &report),
              NX_ERROR_STATE);
    EXPECT_EQ(runs, 1U);
    const unsigned before = writes;
    EXPECT_EQ(
        example_gateway_delivery_cleanup(&delivery, &operation, 100U, &report),
        NX_ERROR_BUSY);
    EXPECT_EQ(writes, before);
    release_session = true;
    EXPECT_EQ(
        example_gateway_delivery_cleanup(&delivery, &operation, 100U, &report),
        NX_SUCCESS);
    EXPECT_FALSE(outbox.pending);
    EXPECT_TRUE(report.durable_ack);
}

TEST_F(GatewayDelivery, OversizedWirePayloadRejectsBeforeGatewayAdmission) {
    uint8_t payload[EXAMPLE_OUTBOX_PAYLOAD_BYTES]{};
    ASSERT_EQ(example_outbox_enqueue(&outbox, &operation, 1U, payload,
                                     sizeof(payload)),
              NX_SUCCESS);
    EXPECT_EQ(example_gateway_delivery_run(&delivery, &outbox, &operation,
                                           &config, &report),
              NX_ERROR_EXHAUSTED);
    EXPECT_EQ(runs, 0U);
    EXPECT_TRUE(outbox.pending);
}

TEST_F(GatewayDelivery, WrongAckDatabaseRejectsBeforeAnyGatewayEffect) {
    queue();
    nx_flashdb_t other = database;
    const nx_flashdb_operation_t wrong{&other, 100U};
    EXPECT_EQ(example_gateway_delivery_run(&delivery, &outbox, &wrong, &config,
                                           &report),
              NX_ERROR_INVALID);
    EXPECT_EQ(runs, 0U);
    EXPECT_TRUE(outbox.pending);
}

TEST_F(GatewayDelivery,
       CandidateCredentialSelectionFollowsRealAuthenticatedJob) {
    example_gateway_application_t application{};
    example_gateway_report_t network{};
    bool selected = false;
    ASSERT_EQ(example_gateway_credentials_try(&application, &config, &operation,
                                              17U, &network, &selected),
              NX_SUCCESS);
    EXPECT_TRUE(selected);
    EXPECT_TRUE(network.authenticated);
    EXPECT_EQ(writes, 1U);
}

TEST_F(GatewayDelivery,
       UnauthenticatedOrUnjoinedCandidateCannotReplaceSelection) {
    example_gateway_application_t application{};
    example_gateway_report_t network{};
    bool selected = false;
    authenticated = false;
    EXPECT_EQ(example_gateway_credentials_try(&application, &config, &operation,
                                              17U, &network, &selected),
              NX_ERROR_STATE);
    EXPECT_FALSE(selected);
    EXPECT_EQ(writes, 0U);
    authenticated = true;
    release_session = false;
    EXPECT_EQ(example_gateway_credentials_try(&application, &config, &operation,
                                              17U, &network, &selected),
              NX_ERROR_BUSY);
    EXPECT_FALSE(selected);
    EXPECT_EQ(writes, 0U);
}

class ManagedDelivery : public GatewayDelivery {
  protected:
    example_gateway_managed_t managed{};
    example_network_route_config_t route{};
    nx_time_us_t now = 10U;
    uint8_t handles[3]{};

    void SetUp() override {
        GatewayDelivery::SetUp();
        route = {
            reinterpret_cast<nx_lwip_adapter_t*>(&handles[0]),
            reinterpret_cast<nx_lwip_dhcp_t*>(&handles[1]),
            reinterpret_cast<nx_lwip_dns_t*>(&handles[2]),
            {[](void* context) { return *static_cast<nx_time_us_t*>(context); },
             &now},
            "broker.test",
            100U,
            2U,
            12U};
        config.tcp_config.network = route.network;
        config.clock = route.clock;
        config.policy.overall_deadline = 90U;
        config.tls_config.hostname = "ignored.test";
        ASSERT_EQ(start(), NX_SUCCESS);
    }

    nx_result_t start() {
        return example_gateway_managed_start(
            &managed, &route,
            [](void*, bool* up, nx_time_us_t deadline) -> nx_result_t {
                EXPECT_LE(deadline, 100U);
                *up = ports.link;
                return ports.phy_result;
            },
            nullptr);
    }

    nx_result_t run() {
        return example_gateway_managed_run(&managed, &outbox, &operation,
                                           &config, &report);
    }

    nx_result_t stop() {
        return example_gateway_managed_stop(&managed, &operation, 100U,
                                            &report);
    }
};

TEST_F(ManagedDelivery, LeaseAndDnsGateTheJobAndSupplyItsExactTlsIdentity) {
    queue();
    ports.lease = false;
    EXPECT_EQ(run(), NX_ERROR_BUSY);
    ports.lease = true;
    ports.dns_ready = false;
    EXPECT_EQ(run(), NX_ERROR_BUSY);
    EXPECT_EQ(runs, 0U);
    ports.dns_ready = true;
    ASSERT_EQ(run(), NX_SUCCESS);
    EXPECT_EQ(ports.connected, ports.peer);
    EXPECT_STREQ(ports.resolved_name, ports.tls_name);
    EXPECT_STREQ(ports.tls_name, route.hostname);
    EXPECT_TRUE(report.durable_ack);
    EXPECT_EQ(stop(), NX_SUCCESS);
    EXPECT_EQ(ports.dhcp_stops, 1U);
}

TEST_F(ManagedDelivery, BusyPhyDoesNotPublishAnUnappliedLinkObservation) {
    queue();
    ports.phy_result = NX_ERROR_BUSY;
    EXPECT_EQ(run(), NX_ERROR_BUSY);
    EXPECT_EQ(runs, 0U);
    EXPECT_EQ(ports.services, 0U);
    ports.phy_result = NX_SUCCESS;
    EXPECT_EQ(run(), NX_SUCCESS);
    EXPECT_TRUE(report.durable_ack);
}

TEST_F(ManagedDelivery, BusyPhyClosesTheLiveJobWithoutPublishingLinkDown) {
    queue();
    ports.busy_phy_in_gateway = true;
    EXPECT_EQ(run(), NX_ERROR_IO);
    EXPECT_EQ(ports.services, 1U);
    EXPECT_EQ(runs, 1U);
    EXPECT_TRUE(outbox.pending);
    EXPECT_TRUE(report.network.local_sessions_released);
    EXPECT_EQ(run(), NX_ERROR_IO);
    EXPECT_EQ(runs, 1U);
    EXPECT_EQ(stop(), NX_SUCCESS);
    EXPECT_EQ(ports.dhcp_stops, 1U);
}

TEST_F(ManagedDelivery, HostnameProbeHasA254ByteLimit) {
    ASSERT_EQ(stop(), NX_SUCCESS);
    std::array<char, 254> no_terminator;
    no_terminator.fill('a');
    route.hostname = no_terminator.data();
    EXPECT_EQ(start(), NX_ERROR_INVALID);
    EXPECT_EQ(ports.services, 0U);
}

TEST_F(ManagedDelivery, LeaseLossRetainsTheRecordAndJoinsTransportBeforeRoute) {
    queue();
    ports.lose_lease_in_gateway = true;
    release_session = false;
    EXPECT_EQ(run(), NX_ERROR_BUSY);
    EXPECT_TRUE(outbox.pending);
    EXPECT_EQ(stop(), NX_ERROR_BUSY);
    EXPECT_EQ(ports.dhcp_stops, 0U);
    EXPECT_EQ(start(), NX_ERROR_STATE);
    EXPECT_EQ(run(), NX_ERROR_STATE);
    release_session = true;
    EXPECT_EQ(stop(), NX_SUCCESS);
    EXPECT_EQ(ports.dhcp_stops, 1U);
    EXPECT_TRUE(outbox.pending);
    ASSERT_EQ(start(), NX_SUCCESS);
    ports.lease = true;
    ports.lose_lease_in_gateway = false;
    EXPECT_EQ(run(), NX_SUCCESS);
    EXPECT_EQ(report.identifier, 257U);
    EXPECT_TRUE(report.durable_ack);
}

TEST_F(ManagedDelivery, PendingDnsRetainsTheRouteWithoutRunningTheGateway) {
    queue();
    ports.dns_ready = false;
    EXPECT_EQ(run(), NX_ERROR_BUSY);
    EXPECT_EQ(stop(), NX_ERROR_BUSY);
    EXPECT_EQ(ports.dhcp_stops, 0U);
    EXPECT_EQ(start(), NX_ERROR_STATE);
    EXPECT_EQ(runs, 0U);
    ports.dns_ready = true;
    EXPECT_EQ(stop(), NX_SUCCESS);
    EXPECT_EQ(ports.dhcp_stops, 1U);
    EXPECT_EQ(ports.dns_cancels, 2U);
}
} /* namespace */

extern "C" const nx_flashdb_operation_t* nx_flashdb_operation_get(void) {
    return active;
}
extern "C" nx_result_t nx_flashdb_kv_get(const nx_flashdb_operation_t*,
                                         const char*, void* output,
                                         size_t* bytes) {
    if (stored.empty()) {
        return NX_ERROR_EMPTY;
    }
    if (*bytes < stored.size()) {
        *bytes = stored.size();
        return NX_ERROR_EXHAUSTED;
    }
    std::memcpy(output, stored.data(), stored.size());
    *bytes = stored.size();
    return NX_SUCCESS;
}
extern "C" nx_result_t nx_flashdb_kv_set(const nx_flashdb_operation_t*,
                                         const char*, const void* data,
                                         size_t bytes) {
    const auto* first = static_cast<const uint8_t*>(data);
    stored.assign(first, first + bytes);
    ++writes;
    return NX_SUCCESS;
}
extern "C" nx_result_t example_gateway_run(example_gateway_application_t*,
                                           const example_gateway_config_t* c,
                                           example_gateway_report_t* report) {
    ++runs;
    if (c->payload_bytes != 0U) {
        wire.assign(c->payload, c->payload + c->payload_bytes);
    }
    *report = {};
    report->delivered = true;
    report->authenticated = authenticated;
    report->local_sessions_released = release_session;
    if (c->route_check != nullptr) {
        std::copy_n(c->tcp_config.address, 4U, ports.connected.begin());
        ports.tls_name = c->tls_config.hostname;
        if (ports.lose_lease_in_gateway) {
            ports.lease = false;
        }
        if (ports.busy_phy_in_gateway) {
            ports.phy_result = NX_ERROR_BUSY;
        }
        const nx_result_t result = c->route_check(c->route_context, 80U);
        if (result != NX_SUCCESS) {
            report->delivered = false;
            report->primary = result;
            return release_session ? result : NX_ERROR_BUSY;
        }
    }
    return release_session ? NX_SUCCESS : NX_ERROR_BUSY;
}
extern "C" nx_result_t
example_gateway_cleanup(example_gateway_application_t*, nx_time_us_t,
                        example_gateway_report_t* report) {
    report->local_sessions_released = release_session;
    return release_session ? NX_SUCCESS : NX_ERROR_BUSY;
}

extern "C" nx_result_t nx_lwip_adapter_service(nx_lwip_adapter_t*, size_t) {
    ++ports.services;
    return NX_SUCCESS;
}
extern "C" nx_result_t nx_lwip_dhcp_start(nx_lwip_dhcp_t*, nx_lwip_adapter_t*) {
    return NX_SUCCESS;
}
extern "C" nx_result_t nx_lwip_dhcp_address(const nx_lwip_dhcp_t*,
                                            uint8_t address[4]) {
    if (!ports.lease) {
        return NX_ERROR_BUSY;
    }
    std::memcpy(address, ports.local.data(), 4U);
    return NX_SUCCESS;
}
extern "C" nx_result_t nx_lwip_dhcp_stop(nx_lwip_dhcp_t*) {
    ++ports.dhcp_stops;
    return NX_SUCCESS;
}
extern "C" nx_result_t nx_lwip_dns_start(nx_lwip_dns_t*, nx_lwip_adapter_t*,
                                         const char* name, nx_time_us_t) {
    ports.resolved_name = name;
    return NX_SUCCESS;
}
extern "C" nx_result_t nx_lwip_dns_poll(nx_lwip_dns_t*, uint8_t address[4]) {
    if (!ports.dns_ready) {
        return NX_ERROR_BUSY;
    }
    std::memcpy(address, ports.peer.data(), 4U);
    return NX_SUCCESS;
}
extern "C" nx_result_t nx_lwip_dns_cancel(nx_lwip_dns_t*) {
    ++ports.dns_cancels;
    return ports.dns_ready ? NX_ERROR_CANCELLED : NX_ERROR_BUSY;
}
