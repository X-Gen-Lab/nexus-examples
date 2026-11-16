/**
 * \file            network_gateway_policy_test.cpp
 * \brief           Gateway retry, retained telemetry and cleanup contracts
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-02
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "gateway_policy.h"
#include <array>
#include <cstring>
#include <gtest/gtest.h>
#include <limits>

namespace {
class GatewayPolicy : public testing::Test {
  protected:
    example_gateway_policy_t policy{};
    example_gateway_policy_config_t config{1000U, 200U, 50U, 3U};
    example_gateway_attempt_t attempt{};
    const std::array<uint8_t, 5> payload{{'i', 'd', ':', '4', '2'}};

    void open() {
        ASSERT_EQ(example_gateway_policy_init(&policy, &config, 100U),
                  NX_SUCCESS);
    }
    void connect() {
        open();
        ASSERT_EQ(example_gateway_begin(&policy, 100U, &attempt), NX_SUCCESS);
        ASSERT_EQ(example_gateway_online(&policy, 110U), NX_SUCCESS);
    }
};

TEST_F(GatewayPolicy, InitialAttemptOwnsAFiniteNonextendingDeadline) {
    open();
    ASSERT_EQ(example_gateway_begin(&policy, 100U, &attempt), NX_SUCCESS);
    EXPECT_EQ(attempt.deadline, 300U);
    EXPECT_EQ(attempt.session, 1U);
    const auto prior = attempt;
    EXPECT_EQ(example_gateway_begin(&policy, 200U, &attempt), NX_ERROR_STATE);
    EXPECT_EQ(attempt.deadline, prior.deadline);
    EXPECT_EQ(attempt.session, prior.session);
}

TEST_F(GatewayPolicy, InvalidConfigurationAndAliasedOutputPreserveStorage) {
    std::memset(&policy, 0x5A, sizeof(policy));
    const auto saved = policy;
    config.attempts_limit = 0U;
    EXPECT_EQ(example_gateway_policy_init(&policy, &config, 100U),
              NX_ERROR_INVALID);
    EXPECT_EQ(std::memcmp(&policy, &saved, sizeof(policy)), 0);
    config.attempts_limit = EXAMPLE_GATEWAY_MAX_ATTEMPTS + 1U;
    EXPECT_EQ(example_gateway_policy_init(&policy, &config, 100U),
              NX_ERROR_INVALID);
    config.attempts_limit = 3U;
    EXPECT_EQ(example_gateway_policy_init(&policy, &config, 1000U),
              NX_ERROR_INVALID);
    config.attempt_budget_us = 0U;
    EXPECT_EQ(example_gateway_policy_init(&policy, &config, 100U),
              NX_ERROR_INVALID);
    config.attempt_budget_us = 200U;
    policy = {};
    ASSERT_EQ(example_gateway_policy_init(&policy, &config, 100U), NX_SUCCESS);
    const auto initialized = policy;
    auto* alias = reinterpret_cast<example_gateway_attempt_t*>(&policy);
    EXPECT_EQ(example_gateway_begin(&policy, 100U, alias), NX_ERROR_INVALID);
    EXPECT_EQ(std::memcmp(&policy, &initialized, sizeof(policy)), 0);
}

TEST_F(GatewayPolicy, BusyAndFailedCleanupPreventEveryReconnect) {
    connect();
    ASSERT_EQ(example_gateway_failed(&policy, NX_ERROR_EXHAUSTED), NX_SUCCESS);
    EXPECT_EQ(example_gateway_cleaned(&policy, NX_ERROR_BUSY, 120U),
              NX_ERROR_BUSY);
    EXPECT_EQ(example_gateway_begin(&policy, 200U, &attempt), NX_ERROR_STATE);
    EXPECT_EQ(example_gateway_cleaned(&policy, NX_ERROR_IO, 200U), NX_ERROR_IO);
    EXPECT_EQ(example_gateway_begin(&policy, 300U, &attempt), NX_ERROR_STATE);
    ASSERT_EQ(example_gateway_cleaned(&policy, NX_SUCCESS, 300U), NX_SUCCESS);
    EXPECT_EQ(example_gateway_begin(&policy, 349U, &attempt), NX_ERROR_BUSY);
    ASSERT_EQ(example_gateway_begin(&policy, 350U, &attempt), NX_SUCCESS);
    EXPECT_EQ(attempt.session, 2U);
    EXPECT_EQ(attempt.deadline, 550U);
}

TEST_F(GatewayPolicy, NoAttemptOrOnlineObservationCanExtendOverallBudget) {
    open();
    ASSERT_EQ(example_gateway_begin(&policy, 950U, &attempt), NX_SUCCESS);
    EXPECT_EQ(attempt.deadline, 1000U);
    EXPECT_EQ(example_gateway_online(&policy, 1000U), NX_ERROR_TIMEOUT);
    EXPECT_EQ(policy.state, EXAMPLE_GATEWAY_DRAINING);
    EXPECT_EQ(example_gateway_begin(&policy, 1001U, &attempt), NX_ERROR_STATE);
    EXPECT_EQ(example_gateway_cleaned(&policy, NX_SUCCESS, 1001U), NX_SUCCESS);
    EXPECT_EQ(example_gateway_begin(&policy, 1002U, &attempt),
              NX_ERROR_TIMEOUT);
}

TEST_F(GatewayPolicy, MaximumAttemptsExhaustOnlyAfterRealCleanup) {
    config.attempts_limit = 1U;
    connect();
    ASSERT_EQ(example_gateway_failed(&policy, NX_ERROR_IO), NX_SUCCESS);
    EXPECT_EQ(example_gateway_begin(&policy, 150U, &attempt), NX_ERROR_STATE);
    ASSERT_EQ(example_gateway_cleaned(&policy, NX_SUCCESS, 150U), NX_SUCCESS);
    EXPECT_EQ(example_gateway_begin(&policy, 200U, &attempt),
              NX_ERROR_EXHAUSTED);
    EXPECT_EQ(policy.attempts, 1U);
}

TEST_F(GatewayPolicy, LocalPublishNeverReleasesTheCopiedTelemetry) {
    connect();
    auto original = payload;
    ASSERT_EQ(example_gateway_queue(&policy, original.data(), original.size()),
              NX_SUCCESS);
    original.fill(0U);
    EXPECT_EQ(std::memcmp(policy.payload, payload.data(), payload.size()), 0);
    ASSERT_EQ(example_gateway_published(&policy, 7U), NX_SUCCESS);
    EXPECT_EQ(policy.payload_size, payload.size());
    EXPECT_EQ(example_gateway_queue(&policy, payload.data(), payload.size()),
              NX_ERROR_BUSY);
    EXPECT_EQ(example_gateway_ack(&policy, 1U, 8U), NX_ERROR_STATE);
    EXPECT_EQ(example_gateway_ack(&policy, 2U, 7U), NX_ERROR_STATE);
    EXPECT_EQ(policy.payload_size, payload.size());
    ASSERT_EQ(example_gateway_ack(&policy, 1U, 7U), NX_SUCCESS);
    EXPECT_EQ(policy.payload_size, 0U);
    EXPECT_EQ(example_gateway_ack(&policy, 1U, 7U), NX_ERROR_STATE);
}

TEST_F(GatewayPolicy, SettledReconnectReplaysTheSameStableApplicationRecord) {
    connect();
    ASSERT_EQ(example_gateway_queue(&policy, payload.data(), payload.size()),
              NX_SUCCESS);
    ASSERT_EQ(example_gateway_published(&policy, 1U), NX_SUCCESS);
    ASSERT_EQ(example_gateway_failed(&policy, NX_ERROR_IO), NX_SUCCESS);
    ASSERT_EQ(example_gateway_cleaned(&policy, NX_SUCCESS, 120U), NX_SUCCESS);
    ASSERT_EQ(example_gateway_begin(&policy, 170U, &attempt), NX_SUCCESS);
    ASSERT_EQ(example_gateway_online(&policy, 180U), NX_SUCCESS);
    EXPECT_EQ(std::memcmp(policy.payload, payload.data(), payload.size()), 0);
    EXPECT_EQ(policy.payload_size, payload.size());
    EXPECT_EQ(policy.packet_id, 0U);
    ASSERT_EQ(example_gateway_published(&policy, 1U), NX_SUCCESS);
    EXPECT_EQ(example_gateway_ack(&policy, 1U, 1U), NX_ERROR_STATE);
    EXPECT_EQ(policy.payload_size, payload.size());
    EXPECT_EQ(example_gateway_ack(&policy, 2U, 1U), NX_SUCCESS);
}

TEST_F(GatewayPolicy, StopPreservesUnacknowledgedBytesUntilAllUsersJoin) {
    connect();
    ASSERT_EQ(example_gateway_queue(&policy, payload.data(), payload.size()),
              NX_SUCCESS);
    ASSERT_EQ(example_gateway_published(&policy, 7U), NX_SUCCESS);
    ASSERT_EQ(example_gateway_stop(&policy), NX_SUCCESS);
    EXPECT_EQ(policy.state, EXAMPLE_GATEWAY_DRAINING);
    EXPECT_EQ(example_gateway_queue(&policy, payload.data(), payload.size()),
              NX_ERROR_STATE);
    EXPECT_EQ(example_gateway_cleaned(&policy, NX_ERROR_BUSY, 130U),
              NX_ERROR_BUSY);
    EXPECT_EQ(policy.state, EXAMPLE_GATEWAY_DRAINING);
    EXPECT_EQ(policy.payload_size, payload.size());
    ASSERT_EQ(example_gateway_cleaned(&policy, NX_SUCCESS, 150U), NX_SUCCESS);
    EXPECT_EQ(policy.state, EXAMPLE_GATEWAY_STOPPED);
    EXPECT_EQ(policy.payload_size, payload.size());
    EXPECT_EQ(example_gateway_begin(&policy, 200U, &attempt), NX_ERROR_STATE);
}

TEST_F(GatewayPolicy, ReversedClockClosesAdmissionButAllowsActualCleanup) {
    connect();
    EXPECT_EQ(example_gateway_online(&policy, 109U), NX_ERROR_STATE);
    ASSERT_EQ(example_gateway_failed(&policy, NX_ERROR_IO), NX_SUCCESS);
    EXPECT_EQ(example_gateway_cleaned(&policy, NX_SUCCESS, 100U),
              NX_ERROR_STATE);
    EXPECT_EQ(policy.state, EXAMPLE_GATEWAY_FAILED);
    EXPECT_EQ(example_gateway_begin(&policy, 120U, &attempt), NX_ERROR_STATE);
}

TEST_F(GatewayPolicy, ArithmeticClampsBeforeTheFiniteOverallDeadline) {
    const auto limit = std::numeric_limits<nx_time_us_t>::max();
    config.overall_deadline = limit - 1U;
    config.attempt_budget_us = 50U;
    config.retry_delay_us = 50U;
    ASSERT_EQ(example_gateway_policy_init(&policy, &config, limit - 20U),
              NX_SUCCESS);
    ASSERT_EQ(example_gateway_begin(&policy, limit - 20U, &attempt),
              NX_SUCCESS);
    EXPECT_EQ(attempt.deadline, limit - 1U);
    ASSERT_EQ(example_gateway_failed(&policy, NX_ERROR_IO), NX_SUCCESS);
    ASSERT_EQ(example_gateway_cleaned(&policy, NX_SUCCESS, limit - 10U),
              NX_SUCCESS);
    EXPECT_EQ(policy.next_attempt, limit - 1U);
    EXPECT_EQ(example_gateway_begin(&policy, limit - 1U, &attempt),
              NX_ERROR_TIMEOUT);
}

TEST_F(GatewayPolicy, PayloadAndPublishAdmissionRejectWithoutChangingTheSlot) {
    connect();
    EXPECT_EQ(example_gateway_queue(&policy, nullptr, 1U), NX_ERROR_INVALID);
    EXPECT_EQ(example_gateway_queue(&policy, payload.data(), 0U),
              NX_ERROR_INVALID);
    const std::array<uint8_t, EXAMPLE_GATEWAY_PAYLOAD_BYTES + 1U> large{};
    EXPECT_EQ(example_gateway_queue(&policy, large.data(), large.size()),
              NX_ERROR_INVALID);
    EXPECT_EQ(example_gateway_queue(&policy, policy.payload, 1U),
              NX_ERROR_INVALID);
    EXPECT_EQ(example_gateway_published(&policy, 1U), NX_ERROR_STATE);
    ASSERT_EQ(example_gateway_queue(&policy, payload.data(), payload.size()),
              NX_SUCCESS);
    EXPECT_EQ(example_gateway_published(&policy, 0U), NX_ERROR_INVALID);
    ASSERT_EQ(example_gateway_published(&policy, 2U), NX_SUCCESS);
    EXPECT_EQ(example_gateway_published(&policy, 3U), NX_ERROR_STATE);
    EXPECT_EQ(policy.packet_id, 2U);
    EXPECT_EQ(policy.payload_size, payload.size());
}

TEST_F(GatewayPolicy, IndependentProductsNeverShareRetryOrPayloadState) {
    connect();
    example_gateway_policy_t other{};
    ASSERT_EQ(example_gateway_policy_init(&other, &config, 100U), NX_SUCCESS);
    ASSERT_EQ(example_gateway_queue(&policy, payload.data(), payload.size()),
              NX_SUCCESS);
    EXPECT_EQ(other.attempts, 0U);
    EXPECT_EQ(other.payload_size, 0U);
    ASSERT_EQ(example_gateway_stop(&other), NX_SUCCESS);
    EXPECT_EQ(policy.state, EXAMPLE_GATEWAY_ONLINE);
}

TEST_F(GatewayPolicy,
       ReinitializationCannotDiscardLiveTelemetryOrSessionState) {
    for (unsigned stage = 0U; stage < 5U; ++stage) {
        policy = {};
        open();
        ASSERT_EQ(
            example_gateway_queue(&policy, payload.data(), payload.size()),
            NX_SUCCESS);
        if (stage > 0U) {
            ASSERT_EQ(example_gateway_begin(&policy, 100U, &attempt),
                      NX_SUCCESS);
        }
        if (stage > 1U) {
            ASSERT_EQ(example_gateway_online(&policy, 110U), NX_SUCCESS);
        }
        if (stage > 2U) {
            ASSERT_EQ(example_gateway_failed(&policy, NX_ERROR_IO), NX_SUCCESS);
        }
        if (stage > 3U) {
            ASSERT_EQ(example_gateway_cleaned(&policy, NX_SUCCESS, 120U),
                      NX_SUCCESS);
        }
        const auto retained = policy;
        EXPECT_EQ(example_gateway_policy_init(&policy, &config, 130U),
                  NX_ERROR_STATE);
        EXPECT_EQ(std::memcmp(&policy, &retained, sizeof(policy)), 0);
    }
    ASSERT_EQ(example_gateway_stop(&policy), NX_SUCCESS);
    EXPECT_EQ(example_gateway_policy_init(&policy, &config, 140U), NX_SUCCESS);
}
} /* namespace */
