/**
 * \file            owner_bootstrap_test.cpp
 * \brief           Partial startup retains loans and reclaims recorded effects
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-10-11
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "bootstrap.h"
#include "nexus/os/storage.h"
#include <atomic>
#include <chrono>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <mutex>
#include <thread>

namespace {
using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::StrictMock;

/** \brief Mock external workers and hardware, not the real rollback protocol.
 */
class Actions {
  public:
    MOCK_METHOD(nx_result_t, workers, (example_owner_workers_t, size_t));
    MOCK_METHOD(nx_result_t, publishers, ());
    MOCK_METHOD(nx_result_t, notification, (size_t));
    MOCK_METHOD(nx_result_t, platform, ());
};
nx_result_t retire(void* context, example_owner_workers_t mode, size_t count) {
    return static_cast<Actions*>(context)->workers(mode, count);
}
nx_result_t quiesce(void* context) {
    return static_cast<Actions*>(context)->publishers();
}
nx_result_t destroy(void* context, size_t index) {
    return static_cast<Actions*>(context)->notification(index);
}
nx_result_t stop(void* context) {
    return static_cast<Actions*>(context)->platform();
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

class Bootstrap : public ::testing::Test {
  protected:
    void SetUp() override {
        ASSERT_EQ(nx_bus_owner_init(&owner, &slot, 1, {nullptr, enter, leave},
                                    {nullptr, start, service, cancel, false},
                                    {now, nullptr}, nullptr),
                  NX_SUCCESS);
        port = {&actions, retire, quiesce, destroy, stop};
    }
    nx_bus_owner_t owner{};
    nx_owner_slot_t slot{};
    StrictMock<Actions> actions;
    example_owner_bootstrap_port_t port{};
    example_owner_bootstrap_t effects{};
};

TEST_F(Bootstrap, NotificationFailureDestroysOnlyCreatedObjectsInReverseOrder) {
    effects.notifications = 2;
    effects.platform_live = true;
    InSequence order;
    EXPECT_CALL(actions, notification(1)).WillOnce(Return(NX_SUCCESS));
    EXPECT_CALL(actions, notification(0)).WillOnce(Return(NX_SUCCESS));
    EXPECT_CALL(actions, platform()).WillOnce(Return(NX_SUCCESS));
    EXPECT_EQ(example_owner_bootstrap_abort(&effects, &owner, port),
              NX_SUCCESS);
    EXPECT_EQ(effects.notifications, 0U);
    EXPECT_FALSE(effects.platform_live);
    EXPECT_EQ(example_owner_bootstrap_abort(&effects, &owner, port),
              NX_SUCCESS);
}

TEST_F(Bootstrap,
       NativePartialTaskFailureClosesAdmissionBeforeJoiningLiveThreads) {
    effects = {1, 2, true, true, true, false, EXAMPLE_WORKERS_NATIVE_RUNNING};
    InSequence order;
    EXPECT_CALL(actions, workers(EXAMPLE_WORKERS_NATIVE_RUNNING, 2))
        .WillOnce(Invoke([&](example_owner_workers_t, size_t) {
            nx_request_t request{};
            nx_request_initialize(&request);
            EXPECT_EQ(nx_request_prepare(&request, NX_DEADLINE_NEVER),
                      NX_SUCCESS);
            nx_owner_ticket_t ticket{};
            int operation = 1;
            EXPECT_EQ(nx_bus_owner_submit(&owner, &request, &operation, nullptr,
                                          &ticket),
                      NX_ERROR_STATE);
            return NX_SUCCESS;
        }));
    EXPECT_CALL(actions, publishers()).WillOnce(Return(NX_SUCCESS));
    EXPECT_CALL(actions, notification(0)).WillOnce(Return(NX_SUCCESS));
    EXPECT_CALL(actions, platform()).WillOnce(Return(NX_SUCCESS));
    EXPECT_EQ(example_owner_bootstrap_abort(&effects, &owner, port),
              NX_SUCCESS);
    EXPECT_EQ(effects.workers, 0U);
}

TEST_F(Bootstrap, FreeRTOSUnstartedTasksUseDiscardActionAndNeverJoin) {
    effects.workers = 3;
    effects.owner_initialized = true;
    effects.execution = EXAMPLE_WORKERS_KERNEL_UNSTARTED;
    EXPECT_CALL(actions, workers(EXAMPLE_WORKERS_KERNEL_UNSTARTED, 3))
        .WillOnce(Return(NX_SUCCESS));
    EXPECT_EQ(example_owner_bootstrap_abort(&effects, &owner, port),
              NX_SUCCESS);
    EXPECT_EQ(effects.workers, 0U);
}

TEST_F(Bootstrap, RunningSchedulerRejectsPrestartRollbackWithoutSideEffects) {
    effects.scheduler_started = true;
    effects.owner_initialized = true;
    effects.execution = EXAMPLE_WORKERS_KERNEL_UNSTARTED;
    effects.workers = 3;
    EXPECT_EQ(example_owner_bootstrap_abort(&effects, &owner, port),
              NX_ERROR_CONTEXT);
    EXPECT_EQ(effects.workers, 3U);
}

TEST_F(Bootstrap, FailedJoinRetainsPublishersNotificationsAndWorkerRecord) {
    effects = {2, 2, true, true, true, false, EXAMPLE_WORKERS_NATIVE_RUNNING};
    EXPECT_CALL(actions, workers(EXAMPLE_WORKERS_NATIVE_RUNNING, 2))
        .WillOnce(Return(NX_ERROR_TIMEOUT));
    EXPECT_EQ(example_owner_bootstrap_abort(&effects, &owner, port),
              NX_ERROR_TIMEOUT);
    EXPECT_EQ(effects.workers, 2U);
    EXPECT_EQ(effects.notifications, 2U);
    EXPECT_TRUE(effects.publishers_live);
    EXPECT_TRUE(effects.platform_live);
}

TEST_F(Bootstrap, DestroyFailureRetriesOnlyUnreclaimedEffects) {
    effects.notifications = 3;
    effects.platform_live = true;
    InSequence order;
    EXPECT_CALL(actions, notification(2)).WillOnce(Return(NX_SUCCESS));
    EXPECT_CALL(actions, notification(1)).WillOnce(Return(NX_ERROR_BUSY));
    EXPECT_EQ(example_owner_bootstrap_abort(&effects, &owner, port),
              NX_ERROR_BUSY);
    EXPECT_EQ(effects.notifications, 2U);
    EXPECT_TRUE(effects.platform_live);
    EXPECT_CALL(actions, notification(1)).WillOnce(Return(NX_SUCCESS));
    EXPECT_CALL(actions, notification(0)).WillOnce(Return(NX_SUCCESS));
    EXPECT_CALL(actions, platform()).WillOnce(Return(NX_SUCCESS));
    EXPECT_EQ(example_owner_bootstrap_abort(&effects, &owner, port),
              NX_SUCCESS);
}

TEST_F(Bootstrap, UndrainedOwnerNeverDetachesPublishersOrReclaimsLoans) {
    effects.owner_initialized = true;
    effects.publishers_live = true;
    nx_request_t request{};
    nx_request_initialize(&request);
    ASSERT_EQ(nx_request_prepare(&request, NX_DEADLINE_NEVER), NX_SUCCESS);
    nx_owner_ticket_t ticket{};
    int operation = 1;
    ASSERT_EQ(
        nx_bus_owner_submit(&owner, &request, &operation, nullptr, &ticket),
        NX_SUCCESS);
    EXPECT_EQ(example_owner_bootstrap_abort(&effects, &owner, port),
              NX_ERROR_STATE);
    EXPECT_EQ(nx_request_state(&request), NX_REQUEST_QUEUED);
    EXPECT_TRUE(effects.publishers_live);
}

TEST_F(Bootstrap, InvalidOrInconsistentRecordCannotInvokeExternalActions) {
    EXPECT_EQ(example_owner_bootstrap_abort(nullptr, &owner, port),
              NX_ERROR_INVALID);
    effects.workers = 1;
    EXPECT_EQ(example_owner_bootstrap_abort(&effects, &owner, port),
              NX_ERROR_INVALID);
    effects.execution = EXAMPLE_WORKERS_NATIVE_RUNNING;
    effects.owner_initialized = true;
    EXPECT_EQ(example_owner_bootstrap_abort(&effects, nullptr, port),
              NX_ERROR_INVALID);
}

/** \brief Real Native workers demonstrate a borrow surviving partial creation.
 */
struct RunningWorkers {
    nx_bus_owner_t owner{};
    nx_owner_slot_t slot{};
    nx_task_t executor{};
    nx_task_t producer{};
    nx_task_t failed{};
    nx_notify_t completed{};
    nx_wait_port_t completion{};
    nx_request_t request{};
    nx_owner_ticket_t ticket{};
    std::mutex guard;
    std::atomic<bool> submitted{false};
    std::atomic<bool> stop{false};
    nx_result_t producer_result = NX_ERROR_STATE;
    int operation = 1;
    size_t quiesced = 0;
    size_t destroyed = 0;
    alignas(16) uint8_t executor_stack[262144]{};
    alignas(16) uint8_t producer_stack[262144]{};
};
uintptr_t threads_enter(void* context) {
    static_cast<RunningWorkers*>(context)->guard.lock();
    return 0;
}
void threads_leave(void* context, uintptr_t) {
    static_cast<RunningWorkers*>(context)->guard.unlock();
}
nx_time_us_t threads_now(void*) {
    auto elapsed = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<nx_time_us_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count());
}
bool threads_settled(void* context) {
    return nx_request_state(&static_cast<RunningWorkers*>(context)->request) ==
           NX_REQUEST_SETTLED;
}
void execute_owner(void* context) {
    auto* live = static_cast<RunningWorkers*>(context);
    while (!live->stop.load() || !nx_bus_owner_idle(&live->owner)) {
        (void)nx_bus_owner_service(&live->owner);
        std::this_thread::yield();
    }
}
void execute_producer(void* context) {
    auto* live = static_cast<RunningWorkers*>(context);
    live->producer_result =
        nx_bus_owner_submit(&live->owner, &live->request, &live->operation,
                            &live->completion, &live->ticket);
    live->submitted.store(true);
    if (live->producer_result == NX_SUCCESS) {
        live->producer_result = nx_wait_until(
            &live->completion, threads_settled, live, NX_DEADLINE_NEVER);
    }
}
nx_result_t retire_threads(void* context, example_owner_workers_t mode,
                           size_t count) {
    auto* live = static_cast<RunningWorkers*>(context);
    EXPECT_EQ(mode, EXAMPLE_WORKERS_NATIVE_RUNNING);
    EXPECT_EQ(count, 2U);
    nx_result_t result = nx_task_join_until(&live->producer, NX_DEADLINE_NEVER);
    if (result != NX_SUCCESS) {
        return result;
    }
    live->stop.store(true);
    return nx_task_join_until(&live->executor, NX_DEADLINE_NEVER);
}
nx_result_t quiesce_threads(void* context) {
    auto* live = static_cast<RunningWorkers*>(context);
    EXPECT_FALSE(live->executor.started);
    EXPECT_FALSE(live->producer.started);
    EXPECT_EQ(nx_request_state(&live->request), NX_REQUEST_SETTLED);
    ++live->quiesced;
    return NX_SUCCESS;
}
nx_result_t destroy_threads(void* context, size_t index) {
    auto* live = static_cast<RunningWorkers*>(context);
    EXPECT_EQ(index, 0U);
    EXPECT_EQ(live->quiesced, 1U);
    ++live->destroyed;
    return nx_notify_destroy(&live->completed);
}
nx_result_t no_platform(void*) {
    return NX_SUCCESS;
}

TEST(BootstrapNative, FailedNextCreationDrainsAndJoinsActualRunningThreads) {
    /* Host instrumentation storage is deliberately larger than MCU stacks. */
    RunningWorkers live;
    ASSERT_EQ(nx_notify_init(&live.completed), NX_SUCCESS);
    live.completion = nx_notify_port(&live.completed);
    ASSERT_EQ(nx_bus_owner_init(&live.owner, &live.slot, 1,
                                {&live, threads_enter, threads_leave},
                                {nullptr, start, service, cancel, false},
                                {threads_now, nullptr}, nullptr),
              NX_SUCCESS);
    nx_request_initialize(&live.request);
    ASSERT_EQ(nx_request_prepare(&live.request, NX_DEADLINE_NEVER), NX_SUCCESS);
    ASSERT_EQ(nx_task_start(&live.executor, "application", live.executor_stack,
                            sizeof(live.executor_stack), 0U, execute_owner,
                            &live),
              NX_SUCCESS);
    ASSERT_EQ(nx_task_start(&live.producer, "application", live.producer_stack,
                            sizeof(live.producer_stack), 0U, execute_producer,
                            &live),
              NX_SUCCESS);
    while (!live.submitted.load()) {
        std::this_thread::yield();
    }
    EXPECT_EQ(nx_request_state(&live.request), NX_REQUEST_QUEUED);
    EXPECT_EQ(nx_task_start(&live.failed, "application", nullptr, 0, 0U,
                            execute_producer, &live),
              NX_ERROR_INVALID);
    EXPECT_FALSE(live.failed.started);
    example_owner_bootstrap_t effects = {
        1, 2, true, true, false, false, EXAMPLE_WORKERS_NATIVE_RUNNING};
    EXPECT_EQ(
        example_owner_bootstrap_abort(&effects, &live.owner,
                                      {&live, retire_threads, quiesce_threads,
                                       destroy_threads, no_platform}),
        NX_SUCCESS);
    EXPECT_EQ(live.producer_result, NX_SUCCESS);
    EXPECT_EQ(live.destroyed, 1U);
    nx_result_t result;
    size_t transferred;
    EXPECT_EQ(nx_request_result(&live.request, &result, &transferred),
              NX_SUCCESS);
    EXPECT_EQ(result, NX_ERROR_CANCELLED);
    EXPECT_EQ(transferred, 0U);
}
} /* namespace */
