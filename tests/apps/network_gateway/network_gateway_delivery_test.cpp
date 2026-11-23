/**
 * \file            network_gateway_delivery_test.cpp
 * \brief           Durable business IDs across the actual gateway call seam
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-22
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "gateway_delivery.h"
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
    return release_session ? NX_SUCCESS : NX_ERROR_BUSY;
}
extern "C" nx_result_t
example_gateway_cleanup(example_gateway_application_t*, nx_time_us_t,
                        example_gateway_report_t* report) {
    report->local_sessions_released = release_session;
    return release_session ? NX_SUCCESS : NX_ERROR_BUSY;
}
