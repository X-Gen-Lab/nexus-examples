/**
 * \file            network_route_test.cpp
 * \brief           Managed address/DNS withdrawal and UTC quality contracts
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-22
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "route_reference.h"
#include <array>
#include <cstring>
#include <gtest/gtest.h>

namespace {
struct Ports {
    unsigned services{};
    unsigned starts{};
    unsigned dns_starts{};
    unsigned dns_cancels{};
    unsigned dhcp_stops{};
    bool leased{};
    bool resolved{};
    bool retained_dns{};
    nx_result_t service_result{NX_SUCCESS};
    nx_result_t dns_result{NX_SUCCESS};
    std::array<uint8_t, 4> local{192U, 0U, 2U, 1U};
    std::array<uint8_t, 4> remote{192U, 0U, 2U, 2U};
};
Ports* ports;
nx_time_us_t clock_snapshot(void* context) {
    return *static_cast<nx_time_us_t*>(context);
}

class Route : public testing::Test {
  protected:
    Ports fixture;
    example_network_route_t route{};
    nx_time_us_t now{10U};
    std::array<max_align_t, 4> adapter{};
    std::array<max_align_t, 4> dhcp{};
    std::array<max_align_t, 4> dns{};
    example_network_route_config_t config{};
    void SetUp() override {
        ports = &fixture;
        config.network = reinterpret_cast<nx_lwip_adapter_t*>(adapter.data());
        config.dhcp = reinterpret_cast<nx_lwip_dhcp_t*>(dhcp.data());
        config.dns = reinterpret_cast<nx_lwip_dns_t*>(dns.data());
        config.clock = {clock_snapshot, &now};
        config.hostname = "broker.example";
        config.deadline = 100U;
        config.receive_budget = 2U;
        config.progress_turns = 16U;
        ASSERT_EQ(example_network_route_start(&route, &config), NX_SUCCESS);
    }
    void TearDown() override {
        ports = nullptr;
    }
    void resolve() {
        EXPECT_EQ(example_network_route_step(&route, true), NX_ERROR_BUSY);
        fixture.leased = true;
        EXPECT_EQ(example_network_route_step(&route, true), NX_ERROR_BUSY);
        fixture.resolved = true;
        EXPECT_EQ(example_network_route_step(&route, true), NX_SUCCESS);
    }
};

TEST_F(Route, StartsDhcpOnlyAfterLinkAndPublishesOnlyResolvedPeer) {
    EXPECT_EQ(example_network_route_step(&route, false), NX_ERROR_BUSY);
    EXPECT_EQ(fixture.starts, 0U);
    std::array<uint8_t, 4> peer{};
    EXPECT_EQ(example_network_route_peer(&route, peer.data()), NX_ERROR_STATE);
    resolve();
    ASSERT_EQ(example_network_route_peer(&route, peer.data()), NX_SUCCESS);
    EXPECT_EQ(peer, fixture.remote);
    EXPECT_EQ(fixture.starts, 1U);
    EXPECT_EQ(fixture.dns_starts, 1U);
}

TEST_F(Route, AddressChangeClosesRouteBeforeAnyNewSessionAndRequiresStop) {
    resolve();
    fixture.local[3] = 9U;
    EXPECT_EQ(example_network_route_step(&route, true), NX_ERROR_STATE);
    std::array<uint8_t, 4> peer{};
    EXPECT_EQ(example_network_route_peer(&route, peer.data()), NX_ERROR_STATE);
    EXPECT_EQ(example_network_route_start(&route, &config), NX_ERROR_STATE);
    EXPECT_EQ(fixture.dhcp_stops, 0U);
    EXPECT_EQ(example_network_route_stop(&route), NX_SUCCESS);
    EXPECT_EQ(fixture.dhcp_stops, 1U);
    EXPECT_EQ(example_network_route_start(&route, &config), NX_SUCCESS);
}

TEST_F(Route, StopRetainsDnsCallbackUntilActualCancellationJoins) {
    EXPECT_EQ(example_network_route_step(&route, true), NX_ERROR_BUSY);
    fixture.leased = true;
    EXPECT_EQ(example_network_route_step(&route, true), NX_ERROR_BUSY);
    fixture.retained_dns = true;
    EXPECT_EQ(example_network_route_stop(&route), NX_ERROR_BUSY);
    EXPECT_EQ(fixture.dhcp_stops, 0U);
    EXPECT_EQ(example_network_route_start(&route, &config), NX_ERROR_STATE);
    fixture.retained_dns = false;
    EXPECT_EQ(example_network_route_stop(&route), NX_SUCCESS);
    EXPECT_EQ(fixture.dhcp_stops, 1U);
    EXPECT_EQ(fixture.dns_cancels, 2U);
}

TEST_F(Route, RejectedDnsPollDoesNotProveUpstreamCallbackRetirement) {
    EXPECT_EQ(example_network_route_step(&route, true), NX_ERROR_BUSY);
    fixture.leased = true;
    fixture.dns_result = NX_ERROR_INVALID;
    EXPECT_EQ(example_network_route_step(&route, true), NX_ERROR_INVALID);
    fixture.retained_dns = true;
    EXPECT_EQ(example_network_route_stop(&route), NX_ERROR_BUSY);
    EXPECT_EQ(fixture.dhcp_stops, 0U);
    fixture.retained_dns = false;
    EXPECT_EQ(example_network_route_stop(&route), NX_SUCCESS);
    EXPECT_EQ(fixture.dns_cancels, 2U);
}

TEST_F(Route, StalledClockBudgetAndExpiredDeadlineCannotCreateUnboundedWork) {
    config.progress_turns = 1U;
    ASSERT_EQ(example_network_route_stop(&route), NX_SUCCESS);
    ASSERT_EQ(example_network_route_start(&route, &config), NX_SUCCESS);
    EXPECT_EQ(example_network_route_step(&route, false), NX_ERROR_BUSY);
    EXPECT_EQ(example_network_route_step(&route, true), NX_ERROR_EXHAUSTED);
    EXPECT_EQ(fixture.starts, 0U);
    EXPECT_EQ(example_network_route_stop(&route), NX_SUCCESS);
    now = 100U;
    EXPECT_EQ(example_network_route_start(&route, &config), NX_ERROR_TIMEOUT);
}

TEST_F(Route, LinkLossAndServiceFailureRetainTheActualDhcpOwner) {
    resolve();
    EXPECT_EQ(example_network_route_step(&route, false), NX_ERROR_IO);
    EXPECT_EQ(fixture.dhcp_stops, 0U);
    EXPECT_EQ(example_network_route_stop(&route), NX_SUCCESS);
    ASSERT_EQ(example_network_route_start(&route, &config), NX_SUCCESS);
    fixture.service_result = NX_ERROR_IO;
    EXPECT_EQ(example_network_route_step(&route, true), NX_ERROR_IO);
    EXPECT_EQ(fixture.starts, 1U);
}

TEST(TrustedUtc, MeasuredExpiredAndUncertainTimeCannotAdmitTls) {
    nx_wall_clock_t wall{};
    nx_time_us_t now{10U};
    example_trusted_utc_t bridge{&wall, {clock_snapshot, &now}, 100U};
    nx_utc_sample_t sample{1700000000000000LL, 20U, 10U, NX_UTC_MEASURED};
    ASSERT_EQ(nx_wall_clock_set(&wall, &sample, 100U, 20U), NX_SUCCESS);
    int64_t seconds{42};
    EXPECT_EQ(example_trusted_utc_read(&bridge, &seconds), NX_ERROR_PERMISSION);
    EXPECT_EQ(seconds, 42);
    sample.quality = NX_UTC_TRUSTED;
    ASSERT_EQ(nx_wall_clock_set(&wall, &sample, 100U, 20U), NX_SUCCESS);
    EXPECT_EQ(example_trusted_utc_read(&bridge, &seconds), NX_SUCCESS);
    EXPECT_EQ(seconds, 1700000000LL);
    bridge.max_uncertainty_us = 10U;
    EXPECT_EQ(example_trusted_utc_read(&bridge, &seconds), NX_ERROR_PERMISSION);
    now = 100U;
    EXPECT_EQ(example_trusted_utc_read(&bridge, &seconds), NX_ERROR_TIMEOUT);
}
} /* namespace */

extern "C" nx_result_t nx_lwip_adapter_service(nx_lwip_adapter_t*, size_t) {
    ++ports->services;
    return ports->service_result;
}
extern "C" nx_result_t nx_lwip_dhcp_start(nx_lwip_dhcp_t*, nx_lwip_adapter_t*) {
    ++ports->starts;
    return NX_SUCCESS;
}
extern "C" nx_result_t nx_lwip_dhcp_address(const nx_lwip_dhcp_t*,
                                            uint8_t address[4]) {
    if (!ports->leased) {
        return NX_ERROR_BUSY;
    }
    std::memcpy(address, ports->local.data(), 4U);
    return NX_SUCCESS;
}
extern "C" nx_result_t nx_lwip_dhcp_stop(nx_lwip_dhcp_t*) {
    ++ports->dhcp_stops;
    return NX_SUCCESS;
}
extern "C" nx_result_t nx_lwip_dns_start(nx_lwip_dns_t*, nx_lwip_adapter_t*,
                                         const char*, nx_time_us_t deadline) {
    EXPECT_EQ(deadline, 100U);
    ++ports->dns_starts;
    return NX_SUCCESS;
}
extern "C" nx_result_t nx_lwip_dns_poll(nx_lwip_dns_t*, uint8_t address[4]) {
    if (ports->dns_result != NX_SUCCESS) {
        return ports->dns_result;
    }
    if (!ports->resolved) {
        return NX_ERROR_BUSY;
    }
    std::memcpy(address, ports->remote.data(), 4U);
    return NX_SUCCESS;
}
extern "C" nx_result_t nx_lwip_dns_cancel(nx_lwip_dns_t*) {
    ++ports->dns_cancels;
    return ports->retained_dns ? NX_ERROR_BUSY : NX_ERROR_CANCELLED;
}
