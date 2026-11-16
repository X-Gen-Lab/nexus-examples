/**
 * \file            owner_lifecycle_test.cpp
 * \brief           Product lifecycle orders actual owner admission and joins
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-10-11
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "lifecycle.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>

extern "C" bool example_native_keeps_platform(void);
extern "C" bool example_baremetal_keeps_platform(void);
extern "C" bool example_freertos_keeps_platform(void);

namespace {
using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::StrictMock;

/** \brief Mock external scheduling and hardware quiescence, never the owner. */
class Shutdown {
  public:
    MOCK_METHOD(nx_result_t, producers, ());
    MOCK_METHOD(nx_result_t, owner, ());
    MOCK_METHOD(nx_result_t, publishers, ());
    MOCK_METHOD(nx_result_t, reclaim, ());
    MOCK_METHOD(nx_result_t, platform, ());
};

nx_result_t join_producers(void* context) {
    return static_cast<Shutdown*>(context)->producers();
}
nx_result_t join_owner(void* context) {
    return static_cast<Shutdown*>(context)->owner();
}
nx_result_t quiesce_publishers(void* context) {
    return static_cast<Shutdown*>(context)->publishers();
}
nx_result_t platform_stop(void* context) {
    return static_cast<Shutdown*>(context)->platform();
}
nx_result_t reclaim(void* context) {
    return static_cast<Shutdown*>(context)->reclaim();
}
uintptr_t enter(void*) {
    return 0;
}
void leave(void*, uintptr_t) {
}
nx_time_us_t now(void*) {
    return 0;
}
nx_result_t start(void*, void*, nx_time_us_t) {
    return NX_ERROR_BUSY;
}
nx_result_t service(void*, nx_result_t*, size_t*) {
    return NX_ERROR_BUSY;
}
nx_result_t cancel(void*) {
    return NX_SUCCESS;
}

/** \brief Initialize the real bounded owner and exact one-slot storage. */
class Lifecycle : public ::testing::Test {
  protected:
    void SetUp() override {
        ASSERT_EQ(nx_bus_owner_init(&bus, &slot, 1, {nullptr, enter, leave},
                                    {nullptr, start, service, cancel, false},
                                    {now, nullptr}, nullptr),
                  NX_SUCCESS);
        port = {&shutdown, join_producers, join_owner, quiesce_publishers,
                reclaim};
    }
    nx_bus_owner_t bus{};
    nx_owner_slot_t slot{};
    StrictMock<Shutdown> shutdown;
    example_owner_shutdown_port_t port{};
    example_owner_phase_t phase = EXAMPLE_OWNER_RUNNING;
};

TEST_F(Lifecycle, ClosesAdmissionBeforeProducerJoinThenReclaimsAfterAllJoins) {
    InSequence order;
    EXPECT_CALL(shutdown, producers()).WillOnce(Invoke([&] {
        nx_request_t request{};
        nx_request_initialize(&request);
        EXPECT_EQ(nx_request_prepare(&request, NX_DEADLINE_NEVER), NX_SUCCESS);
        nx_owner_ticket_t ticket{};
        int operation = 1;
        EXPECT_EQ(
            nx_bus_owner_submit(&bus, &request, &operation, nullptr, &ticket),
            NX_ERROR_STATE);
        EXPECT_EQ(nx_request_state(&request), NX_REQUEST_READY);
        return NX_SUCCESS;
    }));
    EXPECT_CALL(shutdown, owner()).WillOnce(Return(NX_SUCCESS));
    EXPECT_CALL(shutdown, publishers()).WillOnce(Return(NX_SUCCESS));
    EXPECT_CALL(shutdown, reclaim()).WillOnce(Return(NX_SUCCESS));
    EXPECT_EQ(example_owner_shutdown(&phase, &bus, port), NX_SUCCESS);
    EXPECT_EQ(phase, EXAMPLE_OWNER_RECLAIMABLE);
    EXPECT_EQ(example_owner_shutdown(&phase, &bus, port), NX_SUCCESS);
}

TEST_F(Lifecycle, ProducerTimeoutRetainsNotificationsAndOwnerProgress) {
    EXPECT_CALL(shutdown, producers()).WillOnce(Return(NX_ERROR_TIMEOUT));
    EXPECT_EQ(example_owner_shutdown(&phase, &bus, port), NX_ERROR_TIMEOUT);
    EXPECT_EQ(phase, EXAMPLE_OWNER_ADMISSION_CLOSED);
    EXPECT_EQ(nx_bus_owner_service(&bus), NX_SUCCESS);
    InSequence order;
    EXPECT_CALL(shutdown, producers()).WillOnce(Return(NX_SUCCESS));
    EXPECT_CALL(shutdown, owner()).WillOnce(Return(NX_SUCCESS));
    EXPECT_CALL(shutdown, publishers()).WillOnce(Return(NX_SUCCESS));
    EXPECT_CALL(shutdown, reclaim()).WillOnce(Return(NX_SUCCESS));
    EXPECT_EQ(example_owner_shutdown(&phase, &bus, port), NX_SUCCESS);
}

TEST_F(Lifecycle, OwnerJoinFailureDoesNotDetachHardwareOrReclaimStorage) {
    InSequence order;
    EXPECT_CALL(shutdown, producers()).WillOnce(Return(NX_SUCCESS));
    EXPECT_CALL(shutdown, owner()).WillOnce(Return(NX_ERROR_BUSY));
    EXPECT_EQ(example_owner_shutdown(&phase, &bus, port), NX_ERROR_BUSY);
    EXPECT_EQ(phase, EXAMPLE_OWNER_PRODUCERS_JOINED);
    EXPECT_CALL(shutdown, owner()).WillOnce(Return(NX_SUCCESS));
    EXPECT_CALL(shutdown, publishers()).WillOnce(Return(NX_SUCCESS));
    EXPECT_CALL(shutdown, reclaim()).WillOnce(Return(NX_SUCCESS));
    EXPECT_EQ(example_owner_shutdown(&phase, &bus, port), NX_SUCCESS);
}

TEST_F(Lifecycle, IdleCheckRejectsPrematureOwnerJoinBeforePublicationExits) {
    nx_request_t request{};
    nx_request_initialize(&request);
    ASSERT_EQ(nx_request_prepare(&request, NX_DEADLINE_NEVER), NX_SUCCESS);
    nx_owner_ticket_t ticket{};
    int operation = 1;
    ASSERT_EQ(nx_bus_owner_submit(&bus, &request, &operation, nullptr, &ticket),
              NX_SUCCESS);
    InSequence order;
    EXPECT_CALL(shutdown, producers()).WillOnce(Return(NX_SUCCESS));
    EXPECT_CALL(shutdown, owner()).WillOnce(Return(NX_SUCCESS));
    EXPECT_EQ(example_owner_shutdown(&phase, &bus, port), NX_ERROR_STATE);
    EXPECT_EQ(phase, EXAMPLE_OWNER_PRODUCERS_JOINED);
    ASSERT_EQ(nx_bus_owner_service(&bus), NX_SUCCESS);
    EXPECT_EQ(nx_request_state(&request), NX_REQUEST_SETTLED);
}

TEST_F(Lifecycle, QuiescenceFailureRetainsStorageAndRetriesOnlyThatMilestone) {
    InSequence order;
    EXPECT_CALL(shutdown, producers()).WillOnce(Return(NX_SUCCESS));
    EXPECT_CALL(shutdown, owner()).WillOnce(Return(NX_SUCCESS));
    EXPECT_CALL(shutdown, publishers()).WillOnce(Return(NX_ERROR_BUSY));
    EXPECT_EQ(example_owner_shutdown(&phase, &bus, port), NX_ERROR_BUSY);
    EXPECT_EQ(phase, EXAMPLE_OWNER_JOINED);
    EXPECT_CALL(shutdown, publishers()).WillOnce(Return(NX_SUCCESS));
    EXPECT_CALL(shutdown, reclaim()).WillOnce(Return(NX_SUCCESS));
    EXPECT_EQ(example_owner_shutdown(&phase, &bus, port), NX_SUCCESS);
}

TEST_F(Lifecycle, InvalidInputHasNoSchedulingOrHardwareEffects) {
    EXPECT_EQ(example_owner_shutdown(nullptr, &bus, port), NX_ERROR_INVALID);
    EXPECT_EQ(example_owner_shutdown(&phase, nullptr, port), NX_ERROR_INVALID);
    auto incomplete = port;
    incomplete.join_owner = nullptr;
    EXPECT_EQ(example_owner_shutdown(&phase, &bus, incomplete),
              NX_ERROR_INVALID);
    EXPECT_EQ(phase, EXAMPLE_OWNER_RUNNING);
}

TEST_F(Lifecycle, RunningSchedulerRetainsPlatformClocksAndTickAfterIOReclaim) {
    phase = EXAMPLE_OWNER_RECLAIMABLE;
    EXPECT_EQ(example_owner_finish(&phase, true, platform_stop, &shutdown),
              NX_SUCCESS);
    EXPECT_EQ(phase, EXAMPLE_OWNER_RECLAIMABLE);
}

TEST_F(Lifecycle, StoppedSchedulerMayStopPlatformOnlyOnceAfterReclaim) {
    phase = EXAMPLE_OWNER_RECLAIMABLE;
    EXPECT_CALL(shutdown, platform()).WillOnce(Return(NX_SUCCESS));
    EXPECT_EQ(example_owner_finish(&phase, false, platform_stop, &shutdown),
              NX_SUCCESS);
    EXPECT_EQ(phase, EXAMPLE_OWNER_PLATFORM_STOPPED);
    EXPECT_EQ(example_owner_finish(&phase, false, platform_stop, &shutdown),
              NX_SUCCESS);
}

TEST_F(Lifecycle, PlatformStopFailureRetainsReclaimMilestoneAndCanRetry) {
    phase = EXAMPLE_OWNER_RECLAIMABLE;
    EXPECT_CALL(shutdown, platform()).WillOnce(Return(NX_ERROR_BUSY));
    EXPECT_EQ(example_owner_finish(&phase, false, platform_stop, &shutdown),
              NX_ERROR_BUSY);
    EXPECT_EQ(phase, EXAMPLE_OWNER_RECLAIMABLE);
    EXPECT_CALL(shutdown, platform()).WillOnce(Return(NX_SUCCESS));
    EXPECT_EQ(example_owner_finish(&phase, false, platform_stop, &shutdown),
              NX_SUCCESS);
}

TEST_F(Lifecycle, PlatformStopRejectsCallsBeforeQuiescenceOrInvalidInputs) {
    EXPECT_EQ(example_owner_finish(&phase, false, platform_stop, &shutdown),
              NX_ERROR_STATE);
    EXPECT_EQ(example_owner_finish(nullptr, false, platform_stop, &shutdown),
              NX_ERROR_INVALID);
    EXPECT_EQ(example_owner_finish(&phase, false, nullptr, &shutdown),
              NX_ERROR_INVALID);
}
TEST(BackendPolicy, OnlyFreeRTOSPreservesPlatformForItsLiveScheduler) {
    EXPECT_FALSE(example_native_keeps_platform());
    EXPECT_FALSE(example_baremetal_keeps_platform());
    EXPECT_TRUE(example_freertos_keeps_platform());
}
} /* namespace */
