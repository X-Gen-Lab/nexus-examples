/**
 * \file            network_gateway_application_test.cpp
 * \brief           Product lifecycle with real coreMQTT and external ports
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-02
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "gateway_application.h"
#include "nexus/mqtt/core_mqtt_storage.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <vector>

namespace {
using testing::_;
using testing::Invoke;
using testing::Return;

class NetworkPorts {
  public:
    MOCK_METHOD(nx_result_t, TcpStart,
                (nx_lwip_tcp_t*, const nx_lwip_tcp_config_t*, nx_time_us_t));
    MOCK_METHOD(nx_result_t, TcpService, (nx_lwip_tcp_t*));
    MOCK_METHOD(nx_result_t, TcpState,
                (const nx_lwip_tcp_t*, nx_lwip_tcp_diagnostic_t*));
    MOCK_METHOD(nx_result_t, TcpAbort, (nx_lwip_tcp_t*));
    MOCK_METHOD(nx_result_t, TcpClose, (nx_lwip_tcp_t*, nx_time_us_t));
    MOCK_METHOD(const nx_byte_stream_t*, TcpStream, (const nx_lwip_tcp_t*));
    MOCK_METHOD(nx_result_t, TlsInit,
                (nx_tls_session_t*, const nx_tls_config_t*, nx_time_us_t));
    MOCK_METHOD(nx_result_t, TlsHandshake, (nx_tls_session_t*, nx_time_us_t));
    MOCK_METHOD(nx_result_t, TlsService, (nx_tls_session_t*));
    MOCK_METHOD(nx_result_t, TlsAbort, (nx_tls_session_t*));
    MOCK_METHOD(nx_result_t, TlsClose, (nx_tls_session_t*, nx_time_us_t));
    MOCK_METHOD(nx_result_t, TlsRead,
                (nx_tls_session_t*, uint8_t*, size_t, size_t*, nx_time_us_t));
    MOCK_METHOD(const nx_byte_stream_t*, TlsStream, (const nx_tls_session_t*));
    MOCK_METHOD(nx_result_t, TlsState,
                (const nx_tls_session_t*, nx_tls_diagnostic_t*));
    MOCK_METHOD(nx_result_t, WatchdogState,
                (const nx_watchdog_port_t*, nx_watchdog_state_t*));
    MOCK_METHOD(nx_result_t, WatchdogFeed, (const nx_watchdog_port_t*));
};
NetworkPorts* ports = nullptr;
nx_time_us_t mqtt_clock = 100U;
bool hold_mqtt_deinit_once = false;

/* Test-only broker bytes exercise the real pinned parser and QoS records.
 * TLS/TCP mocks are external dependencies; they establish no authentication. */
struct BrokerBytes {
    std::vector<uint8_t> incoming;
    std::deque<uint8_t> outgoing;
    std::vector<std::vector<uint8_t>> records;
    uint8_t refusal = 0U;
    bool drop_first_record = false;
    bool silent_puback = false;
    bool suppress_echo = false;
    bool read_failure = false;
    bool disconnected = false;
    unsigned connections = 0U;

    void respond(std::initializer_list<uint8_t> bytes) {
        outgoing.insert(outgoing.end(), bytes);
    }
    void packet(uint8_t kind, const std::vector<uint8_t>& body) {
        if (kind == 0x10U) {
            ++connections;
            read_failure = false;
            respond({0x20U, 0x02U, 0x00U, refusal});
        } else if (kind == 0x82U) {
            respond({0x90U, 0x03U, body.at(0), body.at(1), 0x01U});
        } else if (kind == 0x32U) {
            const size_t topic =
                static_cast<size_t>(body.at(0)) * 256U + body.at(1);
            const size_t id = 2U + topic;
            records.emplace_back(body.begin() + static_cast<ptrdiff_t>(id + 2U),
                                 body.end());
            if (drop_first_record && records.size() == 1U) {
                read_failure = true;
                return;
            }
            if (silent_puback) {
                return;
            }
            respond({0x40U, 0x02U, body.at(id), body.at(id + 1U)});
            if (suppress_echo) {
                return;
            }
            auto echo = body;
            echo[id] = 0U;
            echo[id + 1U] = 17U;
            outgoing.push_back(0x32U);
            outgoing.push_back(static_cast<uint8_t>(echo.size()));
            outgoing.insert(outgoing.end(), echo.begin(), echo.end());
        } else if (kind == 0xA2U) {
            respond({0xB0U, 0x02U, body.at(0), body.at(1)});
        } else if (kind == 0xE0U) {
            disconnected = true;
        }
    }
    static nx_result_t write(void* context, const uint8_t* data, size_t bytes,
                             size_t* accepted, nx_time_us_t) {
        auto& broker = *static_cast<BrokerBytes*>(context);
        if (bytes == 0U) {
            *accepted = 0U;
            return NX_SUCCESS;
        }
        broker.incoming.insert(broker.incoming.end(), data, data + bytes);
        *accepted = bytes;
        while (broker.incoming.size() >= 2U) {
            size_t remaining = 0U;
            size_t multiplier = 1U;
            size_t header = 1U;
            bool ready = false;
            while (header < broker.incoming.size() && header <= 4U) {
                const uint8_t value = broker.incoming[header++];
                remaining += (value & 0x7FU) * multiplier;
                multiplier *= 128U;
                if ((value & 0x80U) == 0U) {
                    ready = true;
                    break;
                }
            }
            if (!ready || header + remaining > broker.incoming.size()) {
                break;
            }
            const std::vector<uint8_t> body(
                broker.incoming.begin() + static_cast<ptrdiff_t>(header),
                broker.incoming.begin() +
                    static_cast<ptrdiff_t>(header + remaining));
            broker.packet(broker.incoming[0], body);
            broker.incoming.erase(
                broker.incoming.begin(),
                broker.incoming.begin() +
                    static_cast<ptrdiff_t>(header + remaining));
        }
        return NX_SUCCESS;
    }
    static nx_result_t read(void* context, uint8_t* data, size_t bytes,
                            size_t* received, nx_time_us_t) {
        auto& broker = *static_cast<BrokerBytes*>(context);
        *received = 0U;
        if (bytes == 0U) {
            return NX_SUCCESS;
        }
        if (broker.read_failure) {
            return NX_ERROR_IO;
        }
        while (*received < bytes && !broker.outgoing.empty()) {
            data[*received] = broker.outgoing.front();
            broker.outgoing.pop_front();
            ++*received;
        }
        return *received == 0U ? NX_ERROR_BUSY : NX_SUCCESS;
    }
};
const nx_byte_stream_ops_t byte_operations{BrokerBytes::write,
                                           BrokerBytes::read};

class GatewayApplication : public testing::Test {
  protected:
    testing::NiceMock<NetworkPorts> mock;
    BrokerBytes broker;
    nx_byte_stream_t stream{&byte_operations, &broker};
    example_gateway_application_t application{};
    example_gateway_report_t report{};
    example_gateway_config_t config{};
    nx_mqtt_t mqtt{};
    std::array<uint8_t, 768> network{};
    std::array<MQTTPubAckInfo_t, 2> outgoing{};
    std::array<MQTTPubAckInfo_t, 2> incoming{};
    std::array<nx_mqtt_control_t, 2> controls{};
    nx_mqtt_memory_t memory{};
    alignas(max_align_t) std::array<uint8_t, 16> tcp_tag{};
    alignas(max_align_t) std::array<uint8_t, 16> tls_tag{};
    alignas(max_align_t) std::array<uint8_t, 16> crypto_tag{};
    alignas(max_align_t) std::array<uint8_t, 16> network_tag{};
    std::array<uint8_t, NX_TLS_TX_STAGE_BYTES> stage{};
    const std::array<uint8_t, 1> der{{1U}};
    const std::array<uint8_t, 5> payload{{'i', 'd', ':', '4', '2'}};
    bool stop_requested = false;
    bool stalled = false;

    static nx_time_us_t now(void*) {
        return mqtt_clock;
    }
    static nx_result_t wait(void* context, nx_time_us_t deadline) {
        auto& test = *static_cast<GatewayApplication*>(context);
        if (!test.stalled) {
            mqtt_clock += 1000U;
        }
        return mqtt_clock >= deadline ? NX_ERROR_TIMEOUT : NX_SUCCESS;
    }
    static bool stop(void* context) {
        return static_cast<GatewayApplication*>(context)->stop_requested;
    }
    void SetUp() override {
        ports = &mock;
        mqtt_clock = 100U;
        hold_mqtt_deinit_once = false;
        memory = {network.data(),  network.size(),  outgoing.data(),
                  outgoing.size(), incoming.data(), incoming.size(),
                  controls.data(), controls.size()};
        config.tcp = reinterpret_cast<nx_lwip_tcp_t*>(tcp_tag.data());
        config.tls = reinterpret_cast<nx_tls_session_t*>(tls_tag.data());
        config.mqtt = &mqtt;
        config.mqtt_memory = &memory;
        config.tcp_config.network =
            reinterpret_cast<nx_lwip_adapter_t*>(network_tag.data());
        config.tcp_config.receive_budget = 4U;
        config.tcp_config.address[0] = 192U;
        config.tcp_config.address[1] = 0U;
        config.tcp_config.address[2] = 2U;
        config.tcp_config.address[3] = 2U;
        config.tcp_config.port = 8883U;
        config.tls_config.runtime =
            reinterpret_cast<nx_tls_runtime_t*>(crypto_tag.data());
        config.tls_config.trust_der = der.data();
        config.tls_config.trust_bytes = der.size();
        config.tls_config.hostname = "gateway.local";
        config.tls_config.hostname_length = std::strlen("gateway.local");
        config.tls_config.tx_stage = stage.data();
        config.tls_config.tx_stage_bytes = stage.size();
        config.identity.client_id = "gateway-01";
        config.identity.client_id_size = std::strlen("gateway-01");
        config.identity.keepalive_seconds = 30U;
        config.clock = {now, this};
        config.wait = wait;
        config.stop = stop;
        config.wait_context = this;
        config.topic = "nexus/gateway/status";
        config.topic_bytes = std::strlen(config.topic);
        config.payload = payload.data();
        config.payload_bytes = payload.size();
        config.policy = {5000000U, 200000U, 1000U, 2U};
        config.cleanup_budget_us = 10000U;
        config.mqtt_io_budget = 64U;
        config.progress_turns = 2000U;
        ON_CALL(mock, TcpStart(_, _, _)).WillByDefault(Return(NX_SUCCESS));
        ON_CALL(mock, TcpService(_)).WillByDefault(Return(NX_SUCCESS));
        ON_CALL(mock, TcpState(_, _))
            .WillByDefault(Invoke(
                [](const nx_lwip_tcp_t*, nx_lwip_tcp_diagnostic_t* state) {
                    *state = {};
                    state->state = NX_LWIP_TCP_ESTABLISHED;
                    return NX_SUCCESS;
                }));
        ON_CALL(mock, TcpAbort(_)).WillByDefault(Return(NX_SUCCESS));
        ON_CALL(mock, TcpClose(_, _)).WillByDefault(Return(NX_SUCCESS));
        ON_CALL(mock, TcpStream(_)).WillByDefault(Return(&stream));
        ON_CALL(mock, TlsInit(_, _, _)).WillByDefault(Return(NX_SUCCESS));
        ON_CALL(mock, TlsHandshake(_, _)).WillByDefault(Return(NX_SUCCESS));
        ON_CALL(mock, TlsService(_)).WillByDefault(Return(NX_SUCCESS));
        ON_CALL(mock, TlsAbort(_)).WillByDefault(Return(NX_SUCCESS));
        ON_CALL(mock, TlsClose(_, _)).WillByDefault(Return(NX_SUCCESS));
        ON_CALL(mock, TlsRead(_, _, _, _, _))
            .WillByDefault(Invoke([](nx_tls_session_t*, uint8_t*, size_t,
                                     size_t* bytes, nx_time_us_t) {
                *bytes = 0U;
                return NX_SUCCESS;
            }));
        ON_CALL(mock, TlsStream(_)).WillByDefault(Return(&stream));
        ON_CALL(mock, TlsState(_, _))
            .WillByDefault(
                Invoke([](const nx_tls_session_t*, nx_tls_diagnostic_t* state) {
                    *state = {};
                    state->state = NX_TLS_ESTABLISHED;
                    return NX_SUCCESS;
                }));
    }
    void TearDown() override {
        hold_mqtt_deinit_once = false;
        if (application.mqtt_owned || application.tls_owned ||
            application.tcp_owned) {
            (void)example_gateway_cleanup(&application, mqtt_clock + 10000U,
                                          &report);
        }
        ports = nullptr;
    }
};

TEST_F(GatewayApplication, RealMqttConfirmsEchoAndStopsEveryLocalSession) {
    ASSERT_EQ(example_gateway_run(&application, &config, &report), NX_SUCCESS);
    EXPECT_TRUE(report.authenticated);
    EXPECT_TRUE(report.tls_snapshot_valid);
    EXPECT_TRUE(report.delivered);
    EXPECT_TRUE(report.echoed);
    EXPECT_TRUE(report.graceful_close);
    EXPECT_TRUE(report.local_sessions_released);
    EXPECT_EQ(report.attempts, 1U);
    EXPECT_EQ(report.mqtt_events, 15U);
    EXPECT_EQ(broker.records.size(), 1U);
    EXPECT_TRUE(broker.disconnected);
}

TEST_F(GatewayApplication, PoolRejectionRetriesWithoutAbortingAnUnownedClient) {
    EXPECT_CALL(mock, TcpStart(_, _, _))
        .WillOnce(Return(NX_ERROR_EXHAUSTED))
        .WillOnce(Return(NX_SUCCESS));
    EXPECT_CALL(mock, TcpAbort(_)).Times(0);
    ASSERT_EQ(example_gateway_run(&application, &config, &report), NX_SUCCESS);
    EXPECT_EQ(report.attempts, 2U);
    EXPECT_EQ(report.first_attempt_failure, NX_ERROR_EXHAUSTED);
    EXPECT_TRUE(report.delivered);
}

TEST_F(GatewayApplication, SettledReconnectRetainsTheSameIdempotentRecord) {
    broker.drop_first_record = true;
    EXPECT_CALL(mock, TcpAbort(_)).Times(1);
    EXPECT_CALL(mock, TlsAbort(_)).Times(2);
    ASSERT_EQ(example_gateway_run(&application, &config, &report), NX_SUCCESS);
    ASSERT_EQ(broker.records.size(), 2U);
    EXPECT_EQ(broker.records[0], broker.records[1]);
    EXPECT_EQ(report.attempts, 2U);
    EXPECT_EQ(report.first_attempt_failure, NX_ERROR_IO);
    EXPECT_TRUE(report.delivered);
}

TEST_F(GatewayApplication, AuthenticationRefusalRetainsAnUnconfirmedRecord) {
    broker.refusal = 5U;
    config.policy.attempts_limit = 1U;
    EXPECT_EQ(example_gateway_run(&application, &config, &report),
              NX_ERROR_PERMISSION);
    EXPECT_FALSE(report.delivered);
    EXPECT_TRUE(report.local_sessions_released);
    EXPECT_EQ(application.policy.payload_size, payload.size());
    EXPECT_EQ(broker.records.size(), 0U);
}

TEST_F(GatewayApplication, StalledClockAndAlwaysReadyWaitHaveAFiniteTurnLimit) {
    broker.silent_puback = true;
    stalled = true;
    config.progress_turns = 7U;
    config.policy.attempts_limit = 1U;
    EXPECT_EQ(example_gateway_run(&application, &config, &report),
              NX_ERROR_EXHAUSTED);
    EXPECT_FALSE(report.delivered);
    EXPECT_LE(report.turns, 7U);
    EXPECT_TRUE(report.local_sessions_released);
}

TEST_F(GatewayApplication, ConfirmedDeliverySurvivesALaterEchoDeadlineFailure) {
    broker.suppress_echo = true;
    EXPECT_EQ(example_gateway_run(&application, &config, &report),
              NX_ERROR_TIMEOUT);
    EXPECT_TRUE(report.delivered);
    EXPECT_FALSE(report.echoed);
    EXPECT_EQ(report.attempts, 1U);
    EXPECT_EQ(broker.records.size(), 1U);
    EXPECT_TRUE(report.local_sessions_released);
}

TEST_F(GatewayApplication, StopBeforeAdmissionNeverTouchesNetworkOrCrypto) {
    stop_requested = true;
    EXPECT_CALL(mock, TcpStart(_, _, _)).Times(0);
    EXPECT_CALL(mock, TlsInit(_, _, _)).Times(0);
    EXPECT_EQ(example_gateway_run(&application, &config, &report),
              NX_ERROR_CANCELLED);
    EXPECT_EQ(report.attempts, 0U);
    EXPECT_TRUE(report.local_sessions_released);
}

TEST_F(GatewayApplication, BusyUpperCleanupRetainsEveryLowerTransport) {
    broker.refusal = 5U;
    config.policy.attempts_limit = 1U;
    hold_mqtt_deinit_once = true;
    EXPECT_CALL(mock, TlsAbort(_)).Times(0);
    EXPECT_CALL(mock, TcpAbort(_)).Times(0);
    EXPECT_EQ(example_gateway_run(&application, &config, &report),
              NX_ERROR_BUSY);
    EXPECT_FALSE(report.local_sessions_released);
    EXPECT_TRUE(application.mqtt_owned);
    EXPECT_TRUE(application.tls_owned);
    EXPECT_TRUE(application.tcp_owned);
    testing::Mock::VerifyAndClearExpectations(&mock);
    EXPECT_CALL(mock, TlsAbort(_)).Times(1);
    EXPECT_CALL(mock, TcpAbort(_)).Times(1);
    EXPECT_EQ(
        example_gateway_cleanup(&application, mqtt_clock + 10000U, &report),
        NX_SUCCESS);
    EXPECT_TRUE(report.local_sessions_released);
    EXPECT_EQ(report.primary, NX_ERROR_PERMISSION);
}

TEST_F(GatewayApplication, InvalidBudgetsAndKnownAliasesHaveNoEffects) {
    EXPECT_CALL(mock, TcpStart(_, _, _)).Times(0);
    config.progress_turns = 0U;
    EXPECT_EQ(example_gateway_run(&application, &config, &report),
              NX_ERROR_INVALID);
    config.progress_turns = 2000U;
    auto* alias = reinterpret_cast<example_gateway_report_t*>(&application);
    EXPECT_EQ(example_gateway_run(&application, &config, alias),
              NX_ERROR_INVALID);
    EXPECT_FALSE(application.running);
}

TEST_F(GatewayApplication, BusyTcpAdmissionNeverAbortsAnUnownedClient) {
    EXPECT_CALL(mock, TcpStart(_, _, _)).WillOnce(Return(NX_ERROR_BUSY));
    EXPECT_CALL(mock, TcpAbort(_)).Times(0);
    EXPECT_CALL(mock, TlsInit(_, _, _)).Times(0);
    EXPECT_EQ(example_gateway_run(&application, &config, &report),
              NX_ERROR_BUSY);
    EXPECT_EQ(report.attempts, 1U);
    EXPECT_TRUE(report.local_sessions_released);
}

TEST_F(GatewayApplication, BusyCryptoAdmissionNeverAbortsAForeignSession) {
    EXPECT_CALL(mock, TlsInit(_, _, _)).WillOnce(Return(NX_ERROR_BUSY));
    EXPECT_CALL(mock, TlsAbort(_)).Times(0);
    EXPECT_CALL(mock, TcpAbort(_)).Times(1);
    EXPECT_EQ(example_gateway_run(&application, &config, &report),
              NX_ERROR_BUSY);
    EXPECT_EQ(report.attempts, 1U);
    EXPECT_TRUE(report.local_sessions_released);
}

TEST_F(GatewayApplication, BusyTlsCleanupKeepsItsUnderlyingTcpStorage) {
    EXPECT_CALL(mock, TlsHandshake(_, _)).WillOnce(Return(NX_ERROR_IO));
    EXPECT_CALL(mock, TlsAbort(_)).WillOnce(Return(NX_ERROR_BUSY));
    EXPECT_CALL(mock, TcpAbort(_)).Times(0);
    EXPECT_EQ(example_gateway_run(&application, &config, &report),
              NX_ERROR_BUSY);
    EXPECT_TRUE(application.tls_owned);
    EXPECT_TRUE(application.tcp_owned);
    EXPECT_FALSE(report.local_sessions_released);
    testing::Mock::VerifyAndClearExpectations(&mock);
    EXPECT_CALL(mock, TcpStart(_, _, _)).Times(0);
    EXPECT_EQ(example_gateway_run(&application, &config, &report),
              NX_ERROR_STATE);
    EXPECT_CALL(mock, TlsAbort(_)).WillOnce(Return(NX_SUCCESS));
    EXPECT_CALL(mock, TcpAbort(_)).WillOnce(Return(NX_SUCCESS));
    EXPECT_EQ(
        example_gateway_cleanup(&application, mqtt_clock + 10000U, &report),
        NX_SUCCESS);
    EXPECT_TRUE(report.local_sessions_released);
    EXPECT_EQ(report.primary, NX_ERROR_IO);
}

TEST_F(GatewayApplication, WatchdogFailureNeverErasesAConfirmedDeliveryFact) {
    nx_watchdog_port_t watchdog{};
    config.watchdog = &watchdog;
    EXPECT_CALL(mock, WatchdogState(&watchdog, _))
        .WillOnce(
            Invoke([](const nx_watchdog_port_t*, nx_watchdog_state_t* state) {
                *state = {10000000U, 10000000U, true, true, false};
                return NX_SUCCESS;
            }));
    EXPECT_CALL(mock, WatchdogFeed(&watchdog))
        .WillOnce(Return(NX_SUCCESS))
        .WillOnce(Return(NX_ERROR_IO));
    EXPECT_EQ(example_gateway_run(&application, &config, &report), NX_ERROR_IO);
    EXPECT_TRUE(report.delivered);
    EXPECT_EQ(report.watchdog_feeds, 1U);
    EXPECT_TRUE(report.local_sessions_released);
    EXPECT_EQ(broker.records.size(), 1U);
}

TEST_F(GatewayApplication,
       OverallTimeoutReportsTheFinalReasonAfterTransientIo) {
    config.policy.overall_deadline = 20000U;
    config.policy.retry_delay_us = 25000U;
    EXPECT_CALL(mock, TcpStart(_, _, _)).WillOnce(Return(NX_ERROR_IO));
    EXPECT_CALL(mock, TlsInit(_, _, _)).Times(0);
    EXPECT_EQ(example_gateway_run(&application, &config, &report),
              NX_ERROR_TIMEOUT);
    EXPECT_EQ(report.primary, NX_ERROR_TIMEOUT);
    EXPECT_EQ(report.first_attempt_failure, NX_ERROR_IO);
    EXPECT_EQ(report.attempts, 1U);
    EXPECT_TRUE(report.local_sessions_released);
}

TEST_F(GatewayApplication,
       KnownInputsCannotOverlapDestroyedApplicationStorage) {
    EXPECT_CALL(mock, TcpStart(_, _, _)).Times(0);
    const auto original = config;
    config.topic = reinterpret_cast<const char*>(application.policy.payload);
    EXPECT_EQ(example_gateway_run(&application, &config, &report),
              NX_ERROR_INVALID);
    application = {};
    config = original;
    config.tls_config.tx_stage = reinterpret_cast<uint8_t*>(&application);
    EXPECT_EQ(example_gateway_run(&application, &config, &report),
              NX_ERROR_INVALID);
    application = {};
    config = original;
    config.tcp = reinterpret_cast<nx_lwip_tcp_t*>(&application.policy);
    EXPECT_EQ(example_gateway_run(&application, &config, &report),
              NX_ERROR_INVALID);
}
TEST_F(GatewayApplication, ActualTlsFailureSnapshotSurvivesBackendCleanup) {
    config.policy.attempts_limit = 1U;
    EXPECT_CALL(mock, TlsHandshake(_, _)).WillOnce(Return(NX_ERROR_IO));
    EXPECT_CALL(mock, TlsState(_, _))
        .WillOnce(
            Invoke([](const nx_tls_session_t*, nx_tls_diagnostic_t* state) {
                *state = {};
                state->state = NX_TLS_FAILED;
                state->result = NX_ERROR_IO;
                state->library_error = -30592;
                return NX_SUCCESS;
            }));
    EXPECT_EQ(example_gateway_run(&application, &config, &report), NX_ERROR_IO);
    EXPECT_TRUE(report.tls_snapshot_valid);
    EXPECT_EQ(report.tls_before_cleanup.state, NX_TLS_FAILED);
    EXPECT_EQ(report.tls_before_cleanup.library_error, -30592);
    EXPECT_FALSE(report.authenticated);
    EXPECT_TRUE(report.local_sessions_released);
}
} /* namespace */

extern "C" nx_time_us_t nx_mqtt_port_now_us(void) {
    return mqtt_clock;
}
extern "C" void nx_mqtt_port_assert(const char*, unsigned) {
    std::abort();
}
extern "C" nx_result_t nx_lwip_tcp_start(nx_lwip_tcp_t* tcp,
                                         const nx_lwip_tcp_config_t* config,
                                         nx_time_us_t deadline) {
    return ports->TcpStart(tcp, config, deadline);
}
extern "C" nx_result_t nx_lwip_tcp_service(nx_lwip_tcp_t* tcp) {
    return ports->TcpService(tcp);
}
extern "C" nx_result_t nx_lwip_tcp_diagnostic(const nx_lwip_tcp_t* tcp,
                                              nx_lwip_tcp_diagnostic_t* state) {
    return ports->TcpState(tcp, state);
}
extern "C" nx_result_t nx_lwip_tcp_abort(nx_lwip_tcp_t* tcp) {
    return ports->TcpAbort(tcp);
}
extern "C" nx_result_t nx_lwip_tcp_close(nx_lwip_tcp_t* tcp,
                                         nx_time_us_t deadline) {
    return ports->TcpClose(tcp, deadline);
}
extern "C" const nx_byte_stream_t*
nx_lwip_tcp_stream(const nx_lwip_tcp_t* tcp) {
    return ports->TcpStream(tcp);
}
extern "C" nx_result_t nx_tls_initialize(nx_tls_session_t* tls,
                                         const nx_tls_config_t* config,
                                         nx_time_us_t deadline) {
    return ports->TlsInit(tls, config, deadline);
}
extern "C" nx_result_t nx_tls_handshake_step(nx_tls_session_t* tls,
                                             nx_time_us_t deadline) {
    return ports->TlsHandshake(tls, deadline);
}
extern "C" nx_result_t nx_tls_service(nx_tls_session_t* tls) {
    return ports->TlsService(tls);
}
extern "C" nx_result_t nx_tls_abort(nx_tls_session_t* tls) {
    return ports->TlsAbort(tls);
}
extern "C" nx_result_t nx_tls_close_step(nx_tls_session_t* tls,
                                         nx_time_us_t deadline) {
    return ports->TlsClose(tls, deadline);
}
extern "C" nx_result_t nx_tls_try_read(nx_tls_session_t* tls, uint8_t* data,
                                       size_t bytes, size_t* received,
                                       nx_time_us_t deadline) {
    return ports->TlsRead(tls, data, bytes, received, deadline);
}
extern "C" const nx_byte_stream_t* nx_tls_stream(const nx_tls_session_t* tls) {
    return ports->TlsStream(tls);
}
extern "C" nx_result_t nx_tls_diagnostic(const nx_tls_session_t* tls,
                                         nx_tls_diagnostic_t* state) {
    return ports->TlsState(tls, state);
}
extern "C" nx_result_t
nx_watchdog_port_state(const nx_watchdog_port_t* watchdog,
                       nx_watchdog_state_t* state) {
    return ports->WatchdogState(watchdog, state);
}
extern "C" nx_result_t
nx_watchdog_port_feed(const nx_watchdog_port_t* watchdog) {
    return ports->WatchdogFeed(watchdog);
}
extern "C" nx_result_t __real_nx_mqtt_deinit(nx_mqtt_t* mqtt);
extern "C" nx_result_t __wrap_nx_mqtt_deinit(nx_mqtt_t* mqtt) {
    if (hold_mqtt_deinit_once) {
        hold_mqtt_deinit_once = false;
        return NX_ERROR_BUSY;
    }
    return __real_nx_mqtt_deinit(mqtt);
}
