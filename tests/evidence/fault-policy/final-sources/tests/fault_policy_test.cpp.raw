/**
 * \file            fault_policy_test.cpp
 * \brief           Real product policy observes before bounded output action
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-10-11
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "fault_policy.h"
#include "nexus/core/request.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>
namespace {
class Output {
  public:
    MOCK_METHOD(nx_result_t, apply, (nx_fault_domain_t, nx_result_t));
};
example_fault_policy_t* current;
nx_result_t apply(void* context, nx_fault_domain_t domain, nx_result_t reason) {
    return static_cast<Output*>(context)->apply(domain, reason);
}
class ProductFault : public testing::Test {
  protected:
    example_fault_policy_t policy{};
    testing::StrictMock<Output> output;
    nx_request_t request{};
    void SetUp() override {
        current = &policy;
        nx_request_initialize(&request);
        ASSERT_EQ(nx_request_prepare(&request, NX_DEADLINE_NEVER), NX_SUCCESS);
        ASSERT_EQ(nx_request_admit(&request, NX_REQUEST_ACTIVE), NX_SUCCESS);
    }
};
TEST_F(ProductFault, UnboundOutputsRemainUnprovenAndNeverReclaimRequest) {
    ASSERT_EQ(example_fault_policy_initialize(&policy, {}), NX_SUCCESS);
    EXPECT_EQ(example_fault_policy_observe(&policy, NX_FAULT_PLATFORM,
                                           NX_ERROR_IO, 9U),
              NX_ERROR_UNSUPPORTED);
    example_fault_snapshot_t snapshot{};
    ASSERT_EQ(example_fault_policy_sample(&policy, &snapshot), NX_SUCCESS);
    EXPECT_EQ(snapshot.output_result, NX_ERROR_UNSUPPORTED);
    EXPECT_EQ(snapshot.reason, NX_ERROR_IO);
    EXPECT_EQ(nx_request_state(&request), NX_REQUEST_ACTIVE);
}
TEST_F(ProductFault, DiagnosticsPublishBeforeTheExplicitProductAction) {
    ASSERT_EQ(example_fault_policy_initialize(&policy, {&output, apply}),
              NX_SUCCESS);
    EXPECT_CALL(output, apply(NX_FAULT_PLATFORM, NX_ERROR_IO))
        .WillOnce(testing::Invoke([&](nx_fault_domain_t, nx_result_t) {
            example_fault_snapshot_t snapshot{};
            EXPECT_EQ(example_fault_policy_sample(&policy, &snapshot),
                      NX_SUCCESS);
            EXPECT_EQ(snapshot.domain, NX_FAULT_PLATFORM);
            EXPECT_EQ(snapshot.reason, NX_ERROR_IO);
            EXPECT_EQ(snapshot.line, 44U);
            EXPECT_EQ(snapshot.output_result, NX_ERROR_BUSY);
            EXPECT_FALSE(snapshot.action_completed);
            return NX_SUCCESS;
        }));
    ASSERT_EQ(example_fault_policy_observe(&policy, NX_FAULT_PLATFORM,
                                           NX_ERROR_IO, 44U),
              NX_SUCCESS);
    example_fault_snapshot_t snapshot{};
    ASSERT_EQ(example_fault_policy_sample(&policy, &snapshot), NX_SUCCESS);
    EXPECT_TRUE(snapshot.action_completed);
    EXPECT_EQ(snapshot.output_result, NX_SUCCESS);
    EXPECT_EQ(nx_request_state(&request), NX_REQUEST_ACTIVE);
}
TEST_F(ProductFault, FailedOutputActionRetainsFaultAndBorrow) {
    ASSERT_EQ(example_fault_policy_initialize(&policy, {&output, apply}),
              NX_SUCCESS);
    EXPECT_CALL(output, apply(NX_FAULT_FREERTOS_ASSERT, NX_ERROR_STATE))
        .WillOnce(testing::Return(NX_ERROR_IO));
    EXPECT_EQ(example_fault_policy_observe(&policy, NX_FAULT_FREERTOS_ASSERT,
                                           NX_ERROR_STATE, 55U),
              NX_ERROR_IO);
    example_fault_snapshot_t snapshot{};
    ASSERT_EQ(example_fault_policy_sample(&policy, &snapshot), NX_SUCCESS);
    EXPECT_EQ(snapshot.output_result, NX_ERROR_IO);
    EXPECT_EQ(snapshot.reason, NX_ERROR_STATE);
    EXPECT_EQ(nx_request_state(&request), NX_REQUEST_ACTIVE);
}
TEST_F(ProductFault, DuplicateFaultDoesNotRepeatOutputsOrReplaceFirstReason) {
    ASSERT_EQ(example_fault_policy_initialize(&policy, {&output, apply}),
              NX_SUCCESS);
    EXPECT_CALL(output, apply(NX_FAULT_PLATFORM, NX_ERROR_IO))
        .WillOnce(testing::Return(NX_SUCCESS));
    ASSERT_EQ(example_fault_policy_observe(&policy, NX_FAULT_PLATFORM,
                                           NX_ERROR_IO, 3U),
              NX_SUCCESS);
    EXPECT_EQ(example_fault_policy_observe(&policy, NX_FAULT_FREERTOS_STACK,
                                           NX_ERROR_STATE, 4U),
              NX_ERROR_STATE);
    EXPECT_EQ(example_fault_policy_initialize(&policy, {}), NX_ERROR_STATE);
    example_fault_snapshot_t snapshot{};
    ASSERT_EQ(example_fault_policy_sample(&policy, &snapshot), NX_SUCCESS);
    EXPECT_EQ(snapshot.domain, NX_FAULT_PLATFORM);
    EXPECT_EQ(snapshot.reason, NX_ERROR_IO);
    EXPECT_EQ(snapshot.line, 3U);
}
TEST_F(ProductFault, NestedFaultCannotReenterProductOutputAction) {
    ASSERT_EQ(example_fault_policy_initialize(&policy, {&output, apply}),
              NX_SUCCESS);
    EXPECT_CALL(output, apply(NX_FAULT_PLATFORM, NX_ERROR_IO))
        .WillOnce(testing::Invoke([&](nx_fault_domain_t, nx_result_t) {
            EXPECT_EQ(example_fault_policy_observe(
                          &policy, NX_FAULT_FREERTOS_STACK, NX_ERROR_STATE, 8U),
                      NX_ERROR_BUSY);
            return NX_SUCCESS;
        }));
    EXPECT_EQ(example_fault_policy_observe(&policy, NX_FAULT_PLATFORM,
                                           NX_ERROR_IO, 7U),
              NX_SUCCESS);
}
TEST_F(ProductFault, NullOrAliasedSnapshotCannotDestroyImmutablePolicy) {
    example_fault_snapshot_t snapshot{};
    EXPECT_EQ(example_fault_policy_sample(nullptr, &snapshot),
              NX_ERROR_INVALID);
    EXPECT_EQ(example_fault_policy_sample(&policy, &snapshot), NX_ERROR_EMPTY);
    EXPECT_EQ(example_fault_policy_observe(nullptr, NX_FAULT_PLATFORM,
                                           NX_ERROR_IO, 2U),
              NX_ERROR_INVALID);
    EXPECT_EQ(example_fault_policy_initialize(nullptr, {}), NX_ERROR_INVALID);
    EXPECT_EQ(
        example_fault_policy_sample(
            &policy, reinterpret_cast<example_fault_snapshot_t*>(&policy)),
        NX_ERROR_INVALID);
}
TEST_F(ProductFault, ActualStrongObserverForwardsWithoutReadingUnknownFile) {
    ASSERT_EQ(example_fault_policy_initialize(&policy, {&output, apply}),
              NX_SUCCESS);
    EXPECT_CALL(output, apply(NX_FAULT_THREADX_ADAPTER, NX_ERROR_CONTEXT))
        .WillOnce(testing::Return(NX_SUCCESS));
    nx_fault_observe(NX_FAULT_THREADX_ADAPTER, NX_ERROR_CONTEXT,
                     reinterpret_cast<const char*>(1U), 101U);
    example_fault_snapshot_t snapshot{};
    ASSERT_EQ(example_fault_policy_sample(&policy, &snapshot), NX_SUCCESS);
    EXPECT_EQ(snapshot.line, 101U);
    EXPECT_EQ(nx_request_state(&request), NX_REQUEST_ACTIVE);
}
} /* namespace */
extern "C" example_fault_policy_t* example_owner_fault_policy(void) {
    return current;
}
