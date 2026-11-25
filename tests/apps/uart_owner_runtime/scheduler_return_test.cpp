/**
 * \file            scheduler_return_test.cpp
 * \brief           Actual terminal policy preserves scheduler-start effects
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-10-11
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "nexus/arch/atomic.h"
#include "scheduler_return.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace {
using ::testing::Invoke;
using ::testing::StrictMock;

/** \brief Escape a terminal external sink without changing application logic.
 */
struct Terminal {};

/** \brief The application only delegates the final diagnostic/reset action.
 */
class Sink {
  public:
    MOCK_METHOD(void, fail_stop, ());
};

/** \brief A real product sink would record/reset or never return. */
void fail_stop(void* context) {
    static_cast<Sink*>(context)->fail_stop();
}

TEST(SchedulerReturn, PublishesStageAndFailureBeforeCallingItsTerminalSink) {
    StrictMock<Sink> sink;
    example_owner_bootstrap_t effects = {
        4, 4, true, true, true, true, EXAMPLE_WORKERS_KERNEL_UNSTARTED};
    uint32_t status = NX_SUCCESS;
    uint32_t stage = 0;
    EXPECT_CALL(sink, fail_stop()).WillOnce(Invoke([&]() {
        EXPECT_EQ(nx_atomic_u32_load_acquire(&status), NX_ERROR_IO);
        EXPECT_EQ(nx_atomic_u32_load_relaxed(&stage),
                  EXAMPLE_OWNER_SCHEDULER_RETURNED);
        EXPECT_EQ(effects.notifications, 4U);
        EXPECT_EQ(effects.workers, 4U);
        EXPECT_TRUE(effects.owner_initialized);
        EXPECT_TRUE(effects.publishers_live);
        EXPECT_TRUE(effects.platform_live);
        EXPECT_TRUE(effects.scheduler_started);
        throw Terminal{};
    }));
    EXPECT_THROW(example_owner_scheduler_returned(&effects, &status, &stage,
                                                  {&sink, fail_stop}),
                 Terminal);
}

TEST(SchedulerReturn,
     DoesNotTreatAnUncertainSchedulerFlagAsNeverExecutedTasks) {
    StrictMock<Sink> sink;
    example_owner_bootstrap_t effects = {
        3, 2, true, true, true, false, EXAMPLE_WORKERS_KERNEL_UNSTARTED};
    uint32_t status = NX_SUCCESS;
    uint32_t stage = 0;
    EXPECT_CALL(sink, fail_stop()).WillOnce(Invoke([&]() {
        EXPECT_TRUE(effects.scheduler_started);
        EXPECT_EQ(effects.workers, 2U);
        EXPECT_EQ(effects.notifications, 3U);
        EXPECT_TRUE(effects.platform_live);
        EXPECT_TRUE(effects.publishers_live);
        throw Terminal{};
    }));
    EXPECT_THROW(example_owner_scheduler_returned(&effects, &status, &stage,
                                                  {&sink, fail_stop}),
                 Terminal);
    EXPECT_EQ(effects.execution, EXAMPLE_WORKERS_KERNEL_UNSTARTED);
    EXPECT_TRUE(effects.scheduler_started);
}
} /* namespace */
