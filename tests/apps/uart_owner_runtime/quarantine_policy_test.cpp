/**
 * \file            quarantine_policy_test.cpp
 * \brief           Owner drain continues after application escalation
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-10-11
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "quarantine_policy.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>
namespace {
class Hardware {
  public:
    MOCK_METHOD(nx_result_t, Start, ());
    MOCK_METHOD(nx_result_t, Service, (nx_result_t*, size_t*));
    MOCK_METHOD(nx_result_t, Cancel, ());
    MOCK_METHOD(void, Fault, (example_recovery_stage_t, nx_result_t));
};
class QuarantinePolicy : public testing::Test {
  protected:
    testing::StrictMock<Hardware> hardware;
    nx_bus_owner_t owner{};
    nx_owner_slot_t slots[1]{};
    nx_request_t request{};
    nx_owner_ticket_t ticket{};
    example_owner_recovery_t recovery{};
    nx_time_us_t clock{};
    uintptr_t depth{};
    int operation{};
    static uintptr_t enter(void* context) {
        auto* self = static_cast<QuarantinePolicy*>(context);
        EXPECT_EQ(self->depth, 0U);
        self->depth = 1U;
        return 0U;
    }
    static void leave(void* context, uintptr_t saved) {
        auto* self = static_cast<QuarantinePolicy*>(context);
        EXPECT_EQ(self->depth, 1U);
        self->depth = saved;
    }
    static nx_time_us_t now(void* context) {
        return static_cast<QuarantinePolicy*>(context)->clock;
    }
    static nx_result_t start(void* context, void*, nx_time_us_t) {
        auto* self = static_cast<QuarantinePolicy*>(context);
        EXPECT_EQ(self->depth, 0U);
        return self->hardware.Start();
    }
    static nx_result_t service(void* context, nx_result_t* result,
                               size_t* bytes) {
        auto* self = static_cast<QuarantinePolicy*>(context);
        EXPECT_EQ(self->depth, 0U);
        return self->hardware.Service(result, bytes);
    }
    static nx_result_t cancel(void* context) {
        auto* self = static_cast<QuarantinePolicy*>(context);
        EXPECT_EQ(self->depth, 0U);
        return self->hardware.Cancel();
    }
    static void fault(void* context, example_recovery_stage_t stage,
                      nx_result_t result) {
        auto* self = static_cast<QuarantinePolicy*>(context);
        EXPECT_EQ(self->depth, 0U);
        self->hardware.Fault(stage, result);
    }
    void SetUp() override {
        ASSERT_EQ(nx_bus_owner_init(&owner, slots, 1U, {this, enter, leave},
                                    {this, start, service, cancel, true},
                                    {now, this}, nullptr),
                  NX_SUCCESS);
        nx_request_initialize(&request);
        ASSERT_EQ(nx_request_prepare(&request, NX_DEADLINE_NEVER), NX_SUCCESS);
    }
    void bind(uint32_t steps = 3U, nx_time_us_t budget = 100U) {
        ASSERT_EQ(example_owner_recovery_initialize(&recovery, &owner, steps,
                                                    budget, {this, fault}),
                  NX_SUCCESS);
    }
    void activate(nx_time_us_t timestamp = 1U) {
        ASSERT_EQ(
            nx_bus_owner_submit(&owner, &request, &operation, nullptr, &ticket),
            NX_SUCCESS);
        EXPECT_CALL(hardware, Start()).WillOnce(testing::Return(NX_SUCCESS));
        ASSERT_EQ(example_owner_recovery_progress(&recovery, timestamp),
                  NX_SUCCESS);
    }
    void observe_error(nx_time_us_t timestamp = 2U) {
        EXPECT_CALL(hardware, Service(testing::_, testing::_))
            .WillOnce(testing::Return(NX_ERROR_IO));
        EXPECT_CALL(hardware,
                    Fault(EXAMPLE_RECOVERY_FAULT_OBSERVED, NX_ERROR_IO));
        EXPECT_EQ(example_owner_recovery_progress(&recovery, timestamp),
                  NX_ERROR_IO);
        EXPECT_EQ(nx_request_state(&request), NX_REQUEST_QUARANTINED);
        EXPECT_FALSE(owner.accepting);
        EXPECT_EQ(slots[0].identity.request, &request);
    }
};
TEST_F(QuarantinePolicy, RejectsInvalidBudgetAndBindingBeforeHardware) {
    EXPECT_EQ(example_owner_recovery_initialize(nullptr, &owner, 1U, 1U,
                                                {this, fault}),
              NX_ERROR_INVALID);
    EXPECT_EQ(example_owner_recovery_initialize(&recovery, &owner, 0U, 1U,
                                                {this, fault}),
              NX_ERROR_INVALID);
    EXPECT_EQ(example_owner_recovery_initialize(
                  &recovery, &owner, 1U, NX_DEADLINE_NEVER, {this, fault}),
              NX_ERROR_INVALID);
    bind();
    EXPECT_EQ(example_owner_recovery_initialize(&recovery, &owner, 1U, 1U,
                                                {this, fault}),
              NX_ERROR_STATE);
}
TEST_F(QuarantinePolicy, BusyDoesNotCloseAdmissionOrPublishFault) {
    bind();
    ASSERT_EQ(
        nx_bus_owner_submit(&owner, &request, &operation, nullptr, &ticket),
        NX_SUCCESS);
    EXPECT_CALL(hardware, Start()).WillOnce(testing::Return(NX_ERROR_BUSY));
    EXPECT_EQ(example_owner_recovery_progress(&recovery, 1U), NX_ERROR_BUSY);
    EXPECT_TRUE(owner.accepting);
    EXPECT_EQ(recovery.stage, EXAMPLE_RECOVERY_RUNNING);
    EXPECT_EQ(nx_request_state(&request), NX_REQUEST_QUEUED);
}
TEST_F(QuarantinePolicy, StepLimitEscalatesRetainedAndRealDrainStillExecutes) {
    bind(2U);
    activate();
    observe_error();
    EXPECT_CALL(hardware, Cancel()).WillOnce(testing::Return(NX_SUCCESS));
    EXPECT_CALL(hardware, Service(testing::_, testing::_))
        .WillOnce(testing::Return(NX_ERROR_BUSY));
    EXPECT_CALL(hardware,
                Fault(EXAMPLE_RECOVERY_ESCALATED_RETAINED, NX_ERROR_TIMEOUT));
    EXPECT_EQ(example_owner_recovery_progress(&recovery, 3U), NX_ERROR_BUSY);
    EXPECT_EQ(recovery.stage, EXAMPLE_RECOVERY_ESCALATED_RETAINED);
    EXPECT_EQ(slots[0].identity.request, &request);
    EXPECT_FALSE(nx_bus_owner_idle(&owner));
    EXPECT_CALL(hardware, Service(testing::_, testing::_))
        .WillOnce(testing::DoAll(testing::SetArgPointee<0>(NX_ERROR_CANCELLED),
                                 testing::SetArgPointee<1>(2U),
                                 testing::Return(NX_SUCCESS)));
    EXPECT_CALL(hardware, Fault(EXAMPLE_RECOVERY_LOAN_DRAINED, NX_SUCCESS));
    EXPECT_EQ(example_owner_recovery_progress(&recovery, 4U), NX_SUCCESS);
    EXPECT_EQ(nx_request_state(&request), NX_REQUEST_SETTLED);
    EXPECT_EQ(recovery.stage, EXAMPLE_RECOVERY_LOAN_DRAINED);
    EXPECT_EQ(slots[0].identity.request, nullptr);
    EXPECT_FALSE(owner.accepting);
}
TEST_F(QuarantinePolicy,
       ReversedTimeEscalatesWithoutReplacingTheServiceResult) {
    bind();
    activate(100U);
    observe_error(110U);
    EXPECT_CALL(hardware, Cancel()).WillOnce(testing::Return(NX_SUCCESS));
    EXPECT_CALL(hardware, Service(testing::_, testing::_))
        .WillOnce(testing::Return(NX_ERROR_BUSY));
    EXPECT_CALL(hardware,
                Fault(EXAMPLE_RECOVERY_ESCALATED_RETAINED, NX_ERROR_IO));
    EXPECT_EQ(example_owner_recovery_progress(&recovery, 109U), NX_ERROR_BUSY);
    EXPECT_EQ(recovery.policy_result, NX_ERROR_IO);
    EXPECT_EQ(slots[0].identity.request, &request);
}
TEST_F(QuarantinePolicy, DeadlineLimitDoesNotSettleOrReclaimRetainedStorage) {
    bind(100U, 10U);
    activate();
    observe_error();
    EXPECT_CALL(hardware, Cancel()).WillOnce(testing::Return(NX_SUCCESS));
    EXPECT_CALL(hardware, Service(testing::_, testing::_))
        .WillOnce(testing::Return(NX_ERROR_BUSY));
    EXPECT_CALL(hardware,
                Fault(EXAMPLE_RECOVERY_ESCALATED_RETAINED, NX_ERROR_TIMEOUT));
    EXPECT_EQ(example_owner_recovery_progress(&recovery, 12U), NX_ERROR_BUSY);
    EXPECT_EQ(nx_request_prepare(&request, 100U), NX_ERROR_STATE);
    EXPECT_EQ(nx_request_slot_release(&slots[0].identity, ticket.epoch),
              NX_ERROR_BUSY);
}
TEST_F(QuarantinePolicy, DeadlineAdditionOverflowEscalatesAndKeepsBorrow) {
    bind();
    activate(UINT64_MAX - 10U);
    EXPECT_CALL(hardware, Service(testing::_, testing::_))
        .WillOnce(testing::Return(NX_ERROR_IO));
    EXPECT_CALL(hardware, Fault(EXAMPLE_RECOVERY_FAULT_OBSERVED, NX_ERROR_IO));
    EXPECT_CALL(hardware,
                Fault(EXAMPLE_RECOVERY_ESCALATED_RETAINED, NX_ERROR_EXHAUSTED));
    EXPECT_EQ(example_owner_recovery_progress(&recovery, UINT64_MAX - 9U),
              NX_ERROR_IO);
    EXPECT_EQ(recovery.policy_result, NX_ERROR_EXHAUSTED);
    EXPECT_EQ(slots[0].identity.request, &request);
}
} /* namespace */
