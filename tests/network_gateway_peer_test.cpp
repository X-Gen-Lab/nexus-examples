/**
 * \file            network_gateway_peer_test.cpp
 * \brief           Two real lwIP processes relay bytes to an OS TCP peer
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-02
 * \copyright       Copyright (c) 2026 Nexus Team
 */
/* POSIX declarations precede lwIP's optional byte-order aliases. */
#include <netinet/in.h>
#include <sys/socket.h>

#include "host_peer.h"
extern "C" {
#include "lwip/init.h"
}
#include <array>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <gtest/gtest.h>
#include <sys/wait.h>
#include <thread>
#include <time.h>
#include <unistd.h>
#include <vector>

namespace {
enum class Fault {
    None,
    LateConnect,
    LateForward,
    ListenerClose,
    InterruptedShutdown,
    FatalShutdown,
    BrokerHalfClose
};
bool peer_role = false;
Fault active_fault = Fault::None;
nx_time_us_t forced_deadline = 0U;
bool clock_expired = false;
bool shutdown_injected = false;
int controlled_broker = -1;
uint32_t broker_io_after_deadline = 0U;
uint32_t shutdown_calls = 0U;
struct Observations {
    nx_time_us_t pause_until;
    nx_time_us_t deadline;
    uint32_t broker_io_after_deadline;
    uint32_t shutdown_calls;
    bool listener_retained;
    bool frames_started;
};
nx_time_us_t host_now(void*) {
    timespec value{};
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) {
        std::abort();
    }
    return static_cast<uint64_t>(value.tv_sec) * 1000000U +
           static_cast<uint64_t>(value.tv_nsec) / 1000U;
}
void wait_tick() {
    const timespec duration{0, 1000000};
    (void)nanosleep(&duration, nullptr);
}
int wait_child(pid_t child) {
    const auto deadline = host_now(nullptr) + 5000000U;
    int status = 0;
    while (host_now(nullptr) < deadline) {
        const auto done = waitpid(child, &status, WNOHANG);
        if (done == child) {
            return WIFEXITED(status) ? WEXITSTATUS(status) : 128;
        }
        if (done < 0) {
            return 129;
        }
        wait_tick();
    }
    (void)kill(child, SIGKILL);
    (void)waitpid(child, &status, 0);
    return 130;
}
struct Client {
    example_gateway_native_frames_t frames{};
    nx_lwip_adapter_t adapter{};
    std::array<uint8_t, NX_ETH_FRAME_MAX> transmit{};
    nx_eth_fragment_t view{};
    tcp_pcb* pcb = nullptr;
    bool connected = false;
    bool reset = false;
    bool eof = false;
    std::vector<uint8_t> received;

    static err_t connected_callback(void* context, tcp_pcb*, err_t error) {
        auto& client = *static_cast<Client*>(context);
        client.connected = error == ERR_OK;
        return error;
    }
    static void error_callback(void* context, err_t) {
        auto& client = *static_cast<Client*>(context);
        client.pcb = nullptr;
        client.reset = true;
    }
    static err_t receive_callback(void* context, tcp_pcb* pcb, pbuf* packet,
                                  err_t error) {
        auto& client = *static_cast<Client*>(context);
        if (packet == nullptr) {
            client.eof = true;
            return ERR_OK;
        }
        if (error != ERR_OK) {
            return error;
        }
        const auto old = client.received.size();
        client.received.resize(old + packet->tot_len);
        const auto bytes = packet->tot_len;
        if (pbuf_copy_partial(packet, client.received.data() + old, bytes,
                              0U) != bytes) {
            client.received.resize(old);
            pbuf_free(packet);
            return ERR_OK;
        }
        tcp_recved(pcb, bytes);
        pbuf_free(packet);
        return ERR_OK;
    }
    bool initialize(int descriptor) {
        lwip_init();
        if (example_gateway_native_frames_init(&frames, descriptor,
                                               nx_clock_t{host_now, nullptr}) !=
            NX_SUCCESS) {
            return false;
        }
        nx_lwip_config_t config{};
        config.port = &frames.face;
        config.clock = {host_now, nullptr};
        config.tx_timeout_us = 100000U;
        config.tx_buffer = transmit.data();
        config.tx_capacity = transmit.size();
        config.rx_views = &view;
        config.rx_capacity = 1U;
        const uint8_t mac[6]{2U, 0U, 0U, 0U, 0U, 1U};
        std::memcpy(config.hardware_address, mac, sizeof(mac));
        const uint8_t ip[4]{192U, 0U, 2U, 1U};
        const uint8_t mask[4]{255U, 255U, 255U, 0U};
        std::memcpy(config.address, ip, sizeof(ip));
        std::memcpy(config.netmask, mask, sizeof(mask));
        const nx_eth_link_t link{true, true, 100U};
        return nx_lwip_adapter_start(&adapter, &config) == NX_SUCCESS &&
               nx_lwip_adapter_apply_link(&adapter, &link) == NX_SUCCESS;
    }
    bool progress() {
        return nx_lwip_adapter_service(&adapter, 4U) == NX_SUCCESS;
    }
    bool connect() {
        connected = reset = eof = false;
        received.clear();
        pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
        if (pcb == nullptr) {
            return false;
        }
        tcp_arg(pcb, this);
        tcp_recv(pcb, receive_callback);
        tcp_err(pcb, error_callback);
        tcp_nagle_disable(pcb);
        ip_addr_t address{};
        IP_ADDR4(&address, 192U, 0U, 2U, 2U);
        return tcp_connect(pcb, &address, 8883U, connected_callback) == ERR_OK;
    }
    bool pump_until(bool (*predicate)(const Client&), nx_time_us_t deadline) {
        while (!predicate(*this) && host_now(nullptr) < deadline) {
            if (!progress()) {
                return false;
            }
            wait_tick();
        }
        return predicate(*this);
    }
    bool exchange(const std::array<uint8_t, 64>& bytes) {
        if (!connect() ||
            !pump_until([](const Client& c) { return c.connected; },
                        host_now(nullptr) + 1500000U)) {
            return false;
        }
        if (tcp_write(pcb, bytes.data(), static_cast<u16_t>(bytes.size()),
                      TCP_WRITE_FLAG_COPY) != ERR_OK ||
            tcp_output(pcb) != ERR_OK) {
            return false;
        }
        if (!pump_until(
                [](const Client& c) { return c.received.size() >= 64U; },
                host_now(nullptr) + 1500000U)) {
            return false;
        }
        return received.size() == bytes.size() &&
               std::memcmp(received.data(), bytes.data(), bytes.size()) == 0;
    }
    void close_flow() {
        if (pcb == nullptr) {
            return;
        }
        auto* old = pcb;
        pcb = nullptr;
        tcp_arg(old, nullptr);
        tcp_recv(old, nullptr);
        tcp_err(old, nullptr);
        if (tcp_close(old) != ERR_OK) {
            tcp_abort(old);
        }
    }
    bool command(int descriptor, uint8_t value) {
        if (send(descriptor, &value, 1U, MSG_NOSIGNAL) != 1) {
            return false;
        }
        const auto deadline = host_now(nullptr) + 1500000U;
        while (host_now(nullptr) < deadline) {
            uint8_t reply = 0U;
            if (recv(descriptor, &reply, 1U, MSG_DONTWAIT) == 1) {
                return reply == static_cast<uint8_t>(value | 0x80U);
            }
            if (!progress()) {
                return false;
            }
            wait_tick();
        }
        return false;
    }
};

struct Outcome {
    int client_status = 255;
    int peer_status = 255;
    example_gateway_peer_report_t report{};
    bool report_received = false;
    Observations observations{};
    bool observations_received = false;
    size_t server_received_bytes = 0U;
    bool server_reset = false;
};
Outcome scenario(bool reset_flow, bool refuse, bool fragmented,
                 Fault fault = Fault::None) {
    Outcome outcome;
    const int listener = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (listener < 0) {
        return outcome;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(listener, reinterpret_cast<sockaddr*>(&address),
             sizeof(address)) != 0 ||
        listen(listener, 2) != 0) {
        close(listener);
        return outcome;
    }
    socklen_t size = sizeof(address);
    if (getsockname(listener, reinterpret_cast<sockaddr*>(&address), &size) !=
        0) {
        close(listener);
        return outcome;
    }
    int frames[2]{-1, -1};
    int control[2]{-1, -1};
    if (socketpair(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK, 0, frames) != 0 ||
        socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK, 0, control) != 0) {
        for (const int descriptor :
             {frames[0], frames[1], control[0], control[1]}) {
            if (descriptor >= 0) {
                close(descriptor);
            }
        }
        close(listener);
        return outcome;
    }
    const auto peer_pid = fork();
    if (peer_pid == 0) {
        close(listener);
        close(frames[0]);
        close(control[0]);
        peer_role = true;
        active_fault = fault;
        lwip_init();
        static example_gateway_peer_t peer{};
        const example_gateway_peer_config_t config{
            frames[1],
            control[1],
            ntohs(address.sin_port),
            host_now(nullptr) + 4500000U,
            fragmented || fault == Fault::LateConnect ? 40000U : 0U,
            fragmented ? 17U : 2048U,
            10000U};
        forced_deadline = config.deadline;
        example_gateway_peer_report_t report{};
        const auto result = example_gateway_peer_run(&peer, &config, &report);
        (void)send(control[1], &report, sizeof(report), MSG_NOSIGNAL);
        nx_eth_frames_state_t state{};
        (void)nx_eth_frames_port_state(&peer.frames.face, &state);
        const Observations observed{
            peer.pause_until,         config.deadline,
            broker_io_after_deadline, shutdown_calls,
            peer.listener != nullptr, state.mac.started};
        (void)send(control[1], &observed, sizeof(observed), MSG_NOSIGNAL);
        _exit(result == NX_SUCCESS ? 0 : 40 + static_cast<int>(result));
    }
    if (peer_pid < 0) {
        close(frames[0]);
        close(frames[1]);
        close(control[0]);
        close(control[1]);
        close(listener);
        return outcome;
    }
    const auto client_pid = fork();
    if (client_pid == 0) {
        close(listener);
        close(frames[1]);
        close(control[1]);
        static Client client;
        bool good = client.initialize(frames[0]);
        std::array<uint8_t, 64> bytes{};
        bytes[0] = 0x3AU;
        bytes[63] = 0xD7U;
        if (good && (refuse || fault == Fault::LateConnect)) {
            good = client.connect() &&
                   client.pump_until([](const Client& c) { return c.reset; },
                                     host_now(nullptr) + 1500000U);
        } else if (good && fault == Fault::BrokerHalfClose) {
            const std::array<uint8_t, 8> marker{0x15U, 0x03U, 0x03U, 0U,
                                                3U,    2U,    0x74U, 0xA6U};
            good = client.connect() &&
                   client.pump_until([](const Client& c) { return c.eof; },
                                     host_now(nullptr) + 1500000U);
            good = good && client.received.size() == marker.size() &&
                   std::memcmp(client.received.data(), marker.data(),
                               marker.size()) == 0;
            good = good && client.pcb != nullptr &&
                   tcp_write(client.pcb, bytes.data(),
                             static_cast<u16_t>(bytes.size()),
                             TCP_WRITE_FLAG_COPY) == ERR_OK &&
                   tcp_output(client.pcb) == ERR_OK;
            const auto late_drain = host_now(nullptr) + 100000U;
            while (host_now(nullptr) < late_drain) {
                good = client.progress() && good;
                wait_tick();
            }
            good = !client.reset && good;
        } else if (good && fault == Fault::LateForward) {
            good =
                client.connect() &&
                client.pump_until([](const Client& c) { return c.connected; },
                                  host_now(nullptr) + 1500000U);
            good = good && tcp_write(client.pcb, bytes.data(),
                                     static_cast<u16_t>(bytes.size()),
                                     TCP_WRITE_FLAG_COPY) == ERR_OK;
            good = good && tcp_output(client.pcb) == ERR_OK;
            good = good &&
                   client.pump_until([](const Client& c) { return c.reset; },
                                     host_now(nullptr) + 1500000U);
        } else if (good) {
            good = client.exchange(bytes);
            if (good && reset_flow) {
                good =
                    client.command(control[0], EXAMPLE_GATEWAY_PEER_RESET) &&
                    client.pump_until([](const Client& c) { return c.reset; },
                                      host_now(nullptr) + 1500000U);
                bytes[63] = 0x82U;
                good = good && client.exchange(bytes);
            }
        }
        client.close_flow();
        const auto drain = host_now(nullptr) + 20000U;
        while (host_now(nullptr) < drain) {
            good = client.progress() && good;
            wait_tick();
        }
        if (good && fault == Fault::FatalShutdown) {
            bytes[63] = 0x93U;
            good = client.exchange(bytes);
            client.close_flow();
            const auto second_drain = host_now(nullptr) + 20000U;
            while (host_now(nullptr) < second_drain) {
                good = client.progress() && good;
                wait_tick();
            }
        }
        if (fault == Fault::ListenerClose) {
            const uint8_t stop = EXAMPLE_GATEWAY_PEER_STOP;
            good = send(control[0], &stop, 1U, MSG_NOSIGNAL) == 1 && good;
        } else if (fault != Fault::LateConnect && fault != Fault::LateForward) {
            good =
                client.command(control[0], EXAMPLE_GATEWAY_PEER_STOP) && good;
        }
        good = nx_lwip_adapter_stop(&client.adapter) == NX_SUCCESS && good;
        _exit(good ? 0 : 50);
    }
    close(frames[0]);
    close(frames[1]);
    close(control[1]);
    if (refuse) {
        close(listener);
    }
    std::atomic<bool> stopping{false};
    std::thread server;
    if (!refuse) {
        server = std::thread([&] {
            const auto end = host_now(nullptr) + 4500000U;
            while (!stopping.load() && host_now(nullptr) < end) {
                const int connection = accept(listener, nullptr, nullptr);
                if (connection < 0) {
                    wait_tick();
                    continue;
                }
                (void)fcntl(connection, F_SETFL, O_NONBLOCK);
                if (fault == Fault::BrokerHalfClose) {
                    const std::array<uint8_t, 8> marker{
                        0x15U, 0x03U, 0x03U, 0U, 3U, 2U, 0x74U, 0xA6U};
                    size_t offset = 0U;
                    while (offset < marker.size() && host_now(nullptr) < end) {
                        const auto sent =
                            send(connection, marker.data() + offset,
                                 marker.size() - offset,
                                 MSG_DONTWAIT | MSG_NOSIGNAL);
                        if (sent > 0) {
                            offset += static_cast<size_t>(sent);
                        } else {
                            wait_tick();
                        }
                    }
                    (void)shutdown(connection, SHUT_WR);
                }
                while (!stopping.load() && host_now(nullptr) < end) {
                    std::array<uint8_t, 512> buffer{};
                    const auto count = recv(connection, buffer.data(),
                                            buffer.size(), MSG_DONTWAIT);
                    if (count == 0) {
                        break;
                    }
                    if (count < 0) {
                        if (errno != EAGAIN && errno != EWOULDBLOCK &&
                            errno != EINTR) {
                            outcome.server_reset = errno == ECONNRESET;
                            break;
                        }
                        wait_tick();
                        continue;
                    }
                    outcome.server_received_bytes += static_cast<size_t>(count);
                    if (fault == Fault::BrokerHalfClose) {
                        continue;
                    }
                    size_t offset = 0U;
                    while (offset < static_cast<size_t>(count) &&
                           !stopping.load() && host_now(nullptr) < end) {
                        const auto sent =
                            send(connection, buffer.data() + offset,
                                 static_cast<size_t>(count) - offset,
                                 MSG_DONTWAIT | MSG_NOSIGNAL);
                        if (sent > 0) {
                            offset += static_cast<size_t>(sent);
                        } else {
                            wait_tick();
                        }
                    }
                }
                shutdown(connection, SHUT_WR);
                close(connection);
            }
        });
    }
    outcome.client_status = client_pid > 0 ? wait_child(client_pid) : 131;
    outcome.peer_status = wait_child(peer_pid);
    stopping.store(true);
    if (server.joinable()) {
        server.join();
        close(listener);
    }
    const auto got =
        recv(control[0], &outcome.report, sizeof(outcome.report), MSG_DONTWAIT);
    outcome.report_received =
        got == static_cast<ssize_t>(sizeof(outcome.report));
    const auto extra = recv(control[0], &outcome.observations,
                            sizeof(outcome.observations), MSG_DONTWAIT);
    outcome.observations_received =
        extra == static_cast<ssize_t>(sizeof(outcome.observations));
    close(control[0]);
    return outcome;
}

TEST(GatewayPeer, TwoRealStacksRelayToAnIndependentOsTcpEcho) {
    const auto outcome = scenario(false, false, false);
    EXPECT_EQ(outcome.client_status, 0);
    EXPECT_EQ(outcome.peer_status, 0);
    ASSERT_TRUE(outcome.report_received);
    EXPECT_EQ(outcome.report.client_to_broker_bytes, 64U);
    EXPECT_EQ(outcome.report.broker_to_client_bytes, 64U);
    EXPECT_GT(outcome.report.received_frames, 0U);
    EXPECT_TRUE(outcome.report.local_resources_released);
}
TEST(GatewayPeer, ExplicitResetAllowsANewClientWithoutStackRestart) {
    const auto outcome = scenario(true, false, false);
    EXPECT_EQ(outcome.client_status, 0);
    EXPECT_EQ(outcome.peer_status, 0);
    ASSERT_TRUE(outcome.report_received);
    EXPECT_EQ(outcome.report.connections, 2U);
    EXPECT_EQ(outcome.report.resets, 1U);
    EXPECT_EQ(outcome.report.client_to_broker_bytes, 128U);
    EXPECT_EQ(outcome.report.broker_to_client_bytes, 128U);
    EXPECT_TRUE(outcome.report.local_resources_released);
}
TEST(GatewayPeer, SeventeenByteRelayAndActualPausePreserveEveryByte) {
    const auto outcome = scenario(false, false, true);
    EXPECT_EQ(outcome.client_status, 0);
    EXPECT_EQ(outcome.peer_status, 0);
    ASSERT_TRUE(outcome.report_received);
    EXPECT_EQ(outcome.report.client_to_broker_bytes, 64U);
    EXPECT_EQ(outcome.report.broker_to_client_bytes, 64U);
    EXPECT_GT(outcome.report.paused_rounds, 0U);
    EXPECT_TRUE(outcome.report.local_resources_released);
}
TEST(GatewayPeer, RefusedOsConnectionResetsTheClientAndReleasesLocalState) {
    const auto outcome = scenario(false, true, false);
    EXPECT_EQ(outcome.client_status, 0);
    EXPECT_EQ(outcome.peer_status, 0);
    ASSERT_TRUE(outcome.report_received);
    EXPECT_EQ(outcome.report.connections, 1U);
    EXPECT_TRUE(outcome.report.local_resources_released);
}
TEST(GatewayPeer, BrokerWriteEofPreservesLateClientBytesWithoutAReset) {
    const auto outcome = scenario(false, false, false, Fault::BrokerHalfClose);
    EXPECT_EQ(outcome.client_status, 0);
    EXPECT_EQ(outcome.peer_status, 0);
    ASSERT_TRUE(outcome.report_received);
    EXPECT_EQ(outcome.server_received_bytes, 64U);
    EXPECT_FALSE(outcome.server_reset);
    EXPECT_EQ(outcome.report.client_to_broker_bytes, 64U);
    EXPECT_EQ(outcome.report.broker_to_client_bytes, 8U);
    EXPECT_TRUE(outcome.report.local_resources_released);
}
TEST(GatewayPeer, InvalidConfigurationDoesNotInitializeOrAcquireAnything) {
    example_gateway_peer_t peer{};
    example_gateway_peer_report_t report{};
    const example_gateway_peer_config_t config{-1, -1, 0U, 1U, 0U, 0U, 0U};
    EXPECT_EQ(example_gateway_peer_run(&peer, &config, &report),
              NX_ERROR_INVALID);
    EXPECT_FALSE(peer.initialized);
    EXPECT_FALSE(peer.adapter.attached);
}
TEST(GatewayPeer, LateConnectNeverExtendsPauseBeyondTheOverallDeadline) {
    const auto outcome = scenario(false, false, false, Fault::LateConnect);
    EXPECT_EQ(outcome.client_status, 0);
    EXPECT_EQ(outcome.peer_status, 40 + static_cast<int>(NX_ERROR_TIMEOUT));
    ASSERT_TRUE(outcome.observations_received);
    EXPECT_LE(outcome.observations.pause_until, outcome.observations.deadline);
    EXPECT_EQ(outcome.observations.broker_io_after_deadline, 0U);
    EXPECT_TRUE(outcome.report.local_resources_released);
}
TEST(GatewayPeer, ACompletedWriteCannotAuthorizeAnotherForwardAfterDeadline) {
    const auto outcome = scenario(false, false, false, Fault::LateForward);
    EXPECT_EQ(outcome.client_status, 0);
    EXPECT_EQ(outcome.peer_status, 40 + static_cast<int>(NX_ERROR_TIMEOUT));
    ASSERT_TRUE(outcome.observations_received);
    EXPECT_EQ(outcome.report.client_to_broker_bytes, 64U);
    EXPECT_EQ(outcome.observations.broker_io_after_deadline, 0U);
    EXPECT_TRUE(outcome.report.local_resources_released);
}
TEST(GatewayPeer, ARefusedListenerCloseRetainsItsLiveLowerNetwork) {
    const auto outcome = scenario(false, false, false, Fault::ListenerClose);
    EXPECT_EQ(outcome.client_status, 0);
    EXPECT_EQ(outcome.peer_status, 40 + static_cast<int>(NX_ERROR_IO));
    ASSERT_TRUE(outcome.observations_received);
    EXPECT_TRUE(outcome.observations.listener_retained);
    EXPECT_TRUE(outcome.observations.frames_started);
    EXPECT_FALSE(outcome.report.local_resources_released);
}
TEST(GatewayPeer, InterruptedHalfCloseIsRetriedUntilTheKernelAcceptsIt) {
    const auto outcome =
        scenario(false, false, false, Fault::InterruptedShutdown);
    EXPECT_EQ(outcome.client_status, 0);
    EXPECT_EQ(outcome.peer_status, 0);
    ASSERT_TRUE(outcome.observations_received);
    EXPECT_GE(outcome.observations.shutdown_calls, 2U);
    EXPECT_TRUE(outcome.report.local_resources_released);
}
TEST(GatewayPeer, FatalHalfCloseRetiresTheFlowBeforeAnotherAdmission) {
    const auto outcome = scenario(false, false, false, Fault::FatalShutdown);
    EXPECT_EQ(outcome.client_status, 0);
    EXPECT_EQ(outcome.peer_status, 0);
    ASSERT_TRUE(outcome.report_received);
    EXPECT_EQ(outcome.report.connections, 2U);
    EXPECT_EQ(outcome.report.client_to_broker_bytes, 128U);
    EXPECT_TRUE(outcome.report.local_resources_released);
}
} /* namespace */

extern "C" int __real_clock_gettime(clockid_t clock, timespec* value);
extern "C" int __wrap_clock_gettime(clockid_t clock, timespec* value) {
    const int result = __real_clock_gettime(clock, value);
    if (result == 0 && clock == CLOCK_MONOTONIC && peer_role && clock_expired) {
        const auto forced = forced_deadline + 1U;
        value->tv_sec = static_cast<time_t>(forced / 1000000U);
        value->tv_nsec = static_cast<long>((forced % 1000000U) * 1000U);
    }
    return result;
}
extern "C" int __real_connect(int descriptor, const sockaddr* address,
                              socklen_t size);
extern "C" int __wrap_connect(int descriptor, const sockaddr* address,
                              socklen_t size) {
    const int result = __real_connect(descriptor, address, size);
    if (peer_role && address->sa_family == AF_INET) {
        controlled_broker = descriptor;
        if (active_fault == Fault::LateConnect) {
            clock_expired = true;
        }
    }
    return result;
}
extern "C" ssize_t __real_send(int descriptor, const void* bytes, size_t count,
                               int flags);
extern "C" ssize_t __wrap_send(int descriptor, const void* bytes, size_t count,
                               int flags) {
    if (peer_role && descriptor == controlled_broker && clock_expired) {
        ++broker_io_after_deadline;
    }
    const auto result = __real_send(descriptor, bytes, count, flags);
    if (peer_role && descriptor == controlled_broker && result > 0 &&
        active_fault == Fault::LateForward) {
        clock_expired = true;
    }
    return result;
}
extern "C" ssize_t __real_recv(int descriptor, void* bytes, size_t count,
                               int flags);
extern "C" ssize_t __wrap_recv(int descriptor, void* bytes, size_t count,
                               int flags) {
    if (peer_role && descriptor == controlled_broker && clock_expired) {
        ++broker_io_after_deadline;
    }
    return __real_recv(descriptor, bytes, count, flags);
}
extern "C" err_t __real_tcp_close(tcp_pcb* pcb);
extern "C" err_t __wrap_tcp_close(tcp_pcb* pcb) {
    if (peer_role && active_fault == Fault::ListenerClose &&
        pcb->state == LISTEN) {
        return ERR_MEM;
    }
    return __real_tcp_close(pcb);
}
extern "C" int __real_shutdown(int descriptor, int how);
extern "C" int __wrap_shutdown(int descriptor, int how) {
    if (peer_role && descriptor == controlled_broker && how == SHUT_WR) {
        ++shutdown_calls;
        if (!shutdown_injected && (active_fault == Fault::InterruptedShutdown ||
                                   active_fault == Fault::FatalShutdown)) {
            shutdown_injected = true;
            errno = active_fault == Fault::InterruptedShutdown ? EINTR : EIO;
            return -1;
        }
    }
    return __real_shutdown(descriptor, how);
}
