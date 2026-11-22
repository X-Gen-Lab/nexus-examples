/**
 * \file            terminal_application_test.cpp
 * \brief           UI, managed-route and durable-settings application contracts
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-22
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "terminal_application.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <gtest/gtest.h>
#include <vector>

namespace {
struct Ports {
    unsigned services{}, dhcp_starts{}, dhcp_stops{}, dns_cancels{};
    unsigned writes{}, touch_stops{}, display_stops{}, mutations{};
    bool leased{}, resolved{}, retained_dns{}, pressed{}, fail_program{};
    nx_result_t udp_close{NX_SUCCESS}, display_stop{NX_SUCCESS};
    nx_time_us_t* time{};
    nx_time_us_t after_draw{};
    std::vector<uint16_t> colors;
    std::vector<uint8_t> packet;
    std::vector<char> cleanup;
    std::array<uint8_t, 1024> flash;
    Ports() {
        flash.fill(0xffU);
    }
};
Ports* ports;
nx_time_us_t read_time(void* context) {
    return *static_cast<nx_time_us_t*>(context);
}
nx_storage_status_t flash_read(void*, size_t offset, void* out, size_t bytes) {
    std::memcpy(out, ports->flash.data() + offset, bytes);
    return NX_STORAGE_OK;
}
nx_storage_status_t flash_program(void*, size_t offset, const void* in,
                                  size_t bytes) {
    ++ports->mutations;
    if (ports->fail_program)
        return NX_STORAGE_IO;
    const auto* input = static_cast<const uint8_t*>(in);
    for (size_t i = 0U; i < bytes; ++i)
        ports->flash[offset + i] &= input[i];
    return NX_STORAGE_OK;
}
nx_storage_status_t flash_erase(void*, size_t offset, size_t bytes) {
    ++ports->mutations;
    std::fill_n(ports->flash.data() + offset, bytes, 0xffU);
    return NX_STORAGE_OK;
}
nx_storage_status_t flash_sync(void*) {
    return NX_STORAGE_OK;
}

class Terminal : public testing::Test {
  protected:
    Ports fixture;
    example_terminal_t terminal{};
    example_terminal_config_t config{};
    nx_ili9341_t display{};
    nx_xpt2046_t touch{};
    nx_storage_t storage{};
    nx_settings_t settings{};
    std::array<uint8_t, 32> workspace{};
    std::array<uint8_t, 128> image{};
    std::array<uint16_t, 480> tile{};
    std::array<uint8_t, 16> receive{};
    std::array<max_align_t, 4> adapter{}, dhcp{}, dns{}, udp{};
    nx_time_us_t now{10U};
    nx_storage_port_t flash{};
    void SetUp() override {
        ports = &fixture;
        fixture.time = &now;
        flash = {nullptr,    1024U,         512U,        4U,
                 flash_read, flash_program, flash_erase, flash_sync};
        reopen();
        config.display = &display;
        config.touch = &touch;
        config.calibration = {100U, 3900U, 200U,  3800U, 240U,
                              320U, false, false, false};
        config.tile = tile.data();
        config.tile_capacity = tile.size();
        config.bar_height = 4U;
        config.settings = &settings;
        config.settings_key = 7U;
        config.defaults = {50U, 0xf800U, 0x07e0U};
        config.route.network =
            reinterpret_cast<nx_lwip_adapter_t*>(adapter.data());
        config.route.dhcp = reinterpret_cast<nx_lwip_dhcp_t*>(dhcp.data());
        config.route.dns = reinterpret_cast<nx_lwip_dns_t*>(dns.data());
        config.route.clock = {read_time, &now};
        config.route.hostname = "terminal.example";
        config.route.deadline = 1000000U;
        config.route.receive_budget = 2U;
        config.route.progress_turns = 100U;
        config.udp = reinterpret_cast<nx_lwip_udp_t*>(udp.data());
        config.receive = receive.data();
        config.local_port = 4000U;
        config.remote_port = 4001U;
        ASSERT_EQ(example_terminal_start(&terminal, &config), NX_SUCCESS);
    }
    void TearDown() override {
        ports = nullptr;
    }
    void reopen() {
        ASSERT_EQ(nx_storage_open(&storage, &flash, 0U, 512U, workspace.data(),
                                  workspace.size()),
                  NX_STORAGE_OK);
        ASSERT_EQ(nx_settings_open(&settings, &storage, 1U, image.data(),
                                   image.size(), 2U),
                  NX_STORAGE_OK);
    }
    void resolve() {
        fixture.leased = true;
        fixture.resolved = true;
        ASSERT_EQ(example_terminal_step(&terminal, true, 900000U), NX_SUCCESS);
        ASSERT_TRUE(terminal.network_ready);
    }
};

TEST_F(Terminal, OfflineTurnIsOneTileAndOneRouteProgressWithoutFlash) {
    EXPECT_EQ(example_terminal_step(&terminal, false, 900000U), NX_SUCCESS);
    EXPECT_EQ(fixture.services, 1U);
    EXPECT_EQ(fixture.dhcp_starts, 0U);
    EXPECT_EQ(fixture.writes, 1U);
    EXPECT_EQ(fixture.colors, std::vector<uint16_t>{0xf800U});
    EXPECT_EQ(terminal.fill.completed_pixels, 480U);
    EXPECT_EQ(fixture.mutations, 0U);
    EXPECT_TRUE(fixture.packet.empty());
}

TEST_F(Terminal, ResolvedRouteCopiesOneVersionedTouchStatusDatagram) {
    fixture.pressed = true;
    resolve();
    ASSERT_EQ(fixture.packet.size(), 16U);
    EXPECT_EQ(fixture.packet[0], 'N');
    EXPECT_EQ(fixture.packet[1], 'T');
    EXPECT_EQ(fixture.packet[2], 1U);
    EXPECT_EQ(fixture.packet[3], 1U);
    EXPECT_EQ(fixture.packet[4], 1U);
    EXPECT_EQ(fixture.packet[8], 0U);
    EXPECT_EQ(fixture.packet[10], 0U);
    EXPECT_EQ(fixture.colors, std::vector<uint16_t>{0x07e0U});
    EXPECT_EQ(fixture.mutations, 0U);
}

TEST_F(Terminal, UiCannotExtendTheFixedRouteDeadlineForStatusAdmission) {
    fixture.leased = true;
    fixture.resolved = true;
    fixture.after_draw = config.route.deadline;
    EXPECT_EQ(example_terminal_step(&terminal, true, 2000000U), NX_SUCCESS);
    EXPECT_TRUE(fixture.packet.empty());
    EXPECT_EQ(terminal.network_error, NX_ERROR_TIMEOUT);
    EXPECT_EQ(fixture.services, 1U);
}

TEST_F(Terminal, ParameterUpdateIsWholeAndNeverRecolorsAnActiveRectangle) {
    ASSERT_EQ(example_terminal_step(&terminal, false, 900000U), NX_SUCCESS);
    example_terminal_parameters_t candidate{0U, 0x001fU, 0xffffU};
    EXPECT_EQ(example_terminal_apply(&terminal, &candidate), NX_ERROR_INVALID);
    EXPECT_EQ(terminal.version, 1U);
    candidate.refresh_ms = 25U;
    ASSERT_EQ(example_terminal_apply(&terminal, &candidate), NX_SUCCESS);
    EXPECT_EQ(terminal.version, 2U);
    EXPECT_EQ(example_terminal_step(&terminal, false, 900000U), NX_SUCCESS);
    EXPECT_EQ(fixture.colors.back(), 0xf800U);
    EXPECT_EQ(example_terminal_step(&terminal, false, 900000U), NX_SUCCESS);
    EXPECT_EQ(fixture.colors.back(), 0x001fU);
    EXPECT_EQ(fixture.mutations, 0U);
}

TEST_F(Terminal, LinkLossDrainsUdpBeforeRouteWhileUiRemainsUsable) {
    resolve();
    fixture.udp_close = NX_ERROR_BUSY;
    EXPECT_EQ(example_terminal_step(&terminal, false, 900000U), NX_SUCCESS);
    EXPECT_EQ(terminal.network_error, NX_ERROR_IO);
    EXPECT_FALSE(terminal.network_ready);
    EXPECT_TRUE(terminal.udp_owned);
    EXPECT_EQ(fixture.dhcp_stops, 0U);
    fixture.udp_close = NX_SUCCESS;
    EXPECT_EQ(example_terminal_step(&terminal, false, 900000U), NX_SUCCESS);
    EXPECT_FALSE(terminal.route_owned);
    EXPECT_EQ(fixture.dhcp_stops, 1U);
    EXPECT_EQ(example_terminal_step(&terminal, true, 900000U), NX_SUCCESS);
    EXPECT_EQ(fixture.dhcp_starts, 1U);
}

TEST_F(Terminal, OnlyAuthorizedMaintenancePersistsAndReopenLoadsExactVersion) {
    example_terminal_parameters_t candidate{25U, 0x001fU, 0xffffU};
    ASSERT_EQ(example_terminal_apply(&terminal, &candidate), NX_SUCCESS);
    EXPECT_EQ(example_terminal_save(&terminal, false), NX_ERROR_PERMISSION);
    EXPECT_EQ(fixture.mutations, 0U);
    ASSERT_EQ(example_terminal_save(&terminal, true), NX_SUCCESS);
    EXPECT_EQ(terminal.persisted_version, 2U);
    ASSERT_EQ(example_terminal_stop(&terminal, 900000U), NX_SUCCESS);
    nx_settings_close(&settings);
    reopen();
    ASSERT_EQ(example_terminal_start(&terminal, &config), NX_SUCCESS);
    EXPECT_EQ(terminal.version, 2U);
    EXPECT_EQ(terminal.parameters.offline_color, 0x001fU);
}

TEST_F(Terminal, AmbiguousCommitFailurePreservesActiveParametersWithoutRetry) {
    example_terminal_parameters_t candidate{25U, 0x001fU, 0xffffU};
    ASSERT_EQ(example_terminal_apply(&terminal, &candidate), NX_SUCCESS);
    fixture.fail_program = true;
    EXPECT_EQ(example_terminal_save(&terminal, true), NX_ERROR_IO);
    EXPECT_EQ(terminal.storage_error, NX_STORAGE_IO);
    EXPECT_FALSE(settings.opened);
    EXPECT_EQ(terminal.parameters.offline_color, 0x001fU);
    const unsigned writes = fixture.mutations;
    EXPECT_EQ(example_terminal_step(&terminal, false, 900000U), NX_SUCCESS);
    EXPECT_EQ(fixture.mutations, writes);
}

TEST_F(Terminal, StopKeepsExactOwnersUntilDnsAndDevicesActuallyJoin) {
    fixture.leased = true;
    ASSERT_EQ(example_terminal_step(&terminal, true, 900000U), NX_SUCCESS);
    fixture.retained_dns = true;
    EXPECT_EQ(example_terminal_stop(&terminal, 900000U), NX_ERROR_BUSY);
    EXPECT_EQ(fixture.touch_stops, 0U);
    EXPECT_EQ(example_terminal_step(&terminal, true, 900000U), NX_ERROR_STATE);
    fixture.retained_dns = false;
    fixture.display_stop = NX_ERROR_IO;
    EXPECT_EQ(example_terminal_stop(&terminal, 900000U), NX_ERROR_IO);
    EXPECT_FALSE(terminal.touch_owned);
    EXPECT_TRUE(terminal.display_owned);
    EXPECT_TRUE(terminal.live);
    fixture.display_stop = NX_SUCCESS;
    EXPECT_EQ(example_terminal_stop(&terminal, 900000U), NX_SUCCESS);
    EXPECT_EQ(fixture.touch_stops, 1U);
    EXPECT_EQ(fixture.cleanup,
              (std::vector<char>{'D', 'D', 'H', 'T', 'I', 'I'}));
    EXPECT_FALSE(terminal.live);
}
} /* namespace */

extern "C" nx_result_t nx_ili9341_write(nx_ili9341_t*, uint16_t, uint16_t,
                                        uint16_t, uint16_t,
                                        const uint16_t* pixels, size_t count,
                                        size_t* written, nx_time_us_t) {
    EXPECT_LE(count, 480U);
    ++ports->writes;
    ports->colors.push_back(pixels[0]);
    if (ports->after_draw != 0U)
        *ports->time = ports->after_draw;
    *written = count;
    return NX_SUCCESS;
}
extern "C" nx_result_t nx_ili9341_stop(nx_ili9341_t*, nx_time_us_t) {
    ++ports->display_stops;
    ports->cleanup.push_back('I');
    return ports->display_stop;
}
extern "C" nx_result_t
nx_xpt2046_sample(nx_xpt2046_t*, nx_xpt2046_sample_t* sample, nx_time_us_t) {
    if (!ports->pressed)
        return NX_ERROR_EMPTY;
    *sample = {};
    sample->x = 100U;
    sample->y = 200U;
    return NX_SUCCESS;
}
extern "C" nx_result_t nx_xpt2046_stop(nx_xpt2046_t*, nx_time_us_t) {
    ++ports->touch_stops;
    ports->cleanup.push_back('T');
    return NX_SUCCESS;
}
extern "C" nx_result_t nx_lwip_adapter_service(nx_lwip_adapter_t*, size_t) {
    ++ports->services;
    return NX_SUCCESS;
}
extern "C" nx_result_t nx_lwip_dhcp_start(nx_lwip_dhcp_t*, nx_lwip_adapter_t*) {
    ++ports->dhcp_starts;
    return NX_SUCCESS;
}
extern "C" nx_result_t nx_lwip_dhcp_address(const nx_lwip_dhcp_t*,
                                            uint8_t address[4]) {
    if (!ports->leased)
        return NX_ERROR_BUSY;
    const uint8_t local[4] = {192U, 0U, 2U, 1U};
    std::memcpy(address, local, sizeof(local));
    return NX_SUCCESS;
}
extern "C" nx_result_t nx_lwip_dhcp_stop(nx_lwip_dhcp_t*) {
    ++ports->dhcp_stops;
    ports->cleanup.push_back('H');
    return NX_SUCCESS;
}
extern "C" nx_result_t nx_lwip_dns_start(nx_lwip_dns_t*, nx_lwip_adapter_t*,
                                         const char*, nx_time_us_t) {
    return NX_SUCCESS;
}
extern "C" nx_result_t nx_lwip_dns_poll(nx_lwip_dns_t*, uint8_t address[4]) {
    if (!ports->resolved)
        return NX_ERROR_BUSY;
    const uint8_t remote[4] = {192U, 0U, 2U, 2U};
    std::memcpy(address, remote, sizeof(remote));
    return NX_SUCCESS;
}
extern "C" nx_result_t nx_lwip_dns_cancel(nx_lwip_dns_t*) {
    ++ports->dns_cancels;
    ports->cleanup.push_back('D');
    return ports->retained_dns ? NX_ERROR_BUSY : NX_ERROR_CANCELLED;
}
extern "C" nx_result_t nx_lwip_udp_open(nx_lwip_udp_t*, nx_lwip_adapter_t*,
                                        uint16_t, uint8_t*, size_t) {
    return NX_SUCCESS;
}
extern "C" nx_result_t nx_lwip_udp_send(nx_lwip_udp_t*,
                                        const nx_ipv4_peer_t* peer,
                                        const uint8_t* bytes, size_t count,
                                        nx_time_us_t deadline) {
    EXPECT_EQ(peer->port, 4001U);
    if (*ports->time >= deadline)
        return NX_ERROR_TIMEOUT;
    ports->packet.assign(bytes, bytes + count);
    return NX_SUCCESS;
}
extern "C" nx_result_t nx_lwip_udp_close(nx_lwip_udp_t*) {
    ports->cleanup.push_back('U');
    return ports->udp_close;
}
