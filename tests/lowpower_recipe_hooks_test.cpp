/**
 * \file            lowpower_recipe_hooks_test.cpp
 * \brief           Actual C11 recipe hooks retain loans until acquired joins
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-10-30
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "stop2_application.h"
#ifdef EXAMPLE_STOP2_COPY_HOOK_TEST
#include "nexus/components/stm32u575_stop2_copy.h"
#else
#include "nexus/components/stm32u575_stop2_lptim1.h"
#endif
#include <array>
#include <cstdlib>
#include <functional>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace {
#ifdef EXAMPLE_STOP2_COPY_HOOK_TEST
using Receiver = nx_stm32u575_lpdma_copy_t;
using Request = nx_stm32u575_lpdma_copy_request_t;
using Entry = nx_stm32u575_stop2_copy_result_t;
constexpr size_t transfer_size = 17U;
#else
using Receiver = nx_stm32u575_lpdma_lptim1_t;
using Request = nx_stm32u575_lpdma_lptim1_request_t;
using Entry = nx_stm32u575_stop2_lptim1_result_t;
constexpr size_t transfer_size = sizeof(nx_stm32u575_lptim1_tuple_t);
#endif

class SdkBoundary {
  public:
    MOCK_METHOD(Entry, Enter,
                (nx_clock_barrier_t, const Receiver*, Request*, uint32_t,
                 uint32_t));
    MOCK_METHOD(nx_result_t, Result,
                (const nx_request_t*, nx_result_t*, size_t*));
    MOCK_METHOD(nx_result_t, Stop, (const Receiver*, uint32_t));
};
SdkBoundary* boundary;

class Stop2RecipeHooksDeathTest : public testing::Test {
  protected:
    testing::StrictMock<SdkBoundary> sdk;
    Entry entry{};
    Request* request{};
    const Receiver* receiver{};
    unsigned domain_storage{};
    nx_clock_barrier_t owners{};
    std::array<unsigned char, 17U> source{};
    std::array<unsigned char, 17U> destination{};

    void SetUp() override {
        boundary = &sdk;
        owners = {reinterpret_cast<nx_clock_domain_t*>(&domain_storage), 9U};
        entry.accepted = true;
        entry.dma_wake = true;
        entry.transition.primary = NX_SUCCESS;
        entry.transition.cleanup = NX_SUCCESS;
        entry.transition.wait_executed = true;
        entry.transition.before.identity = &domain_storage;
        entry.transition.after.identity = &domain_storage;
        entry.transition.before.generation = 2U;
        entry.transition.after.generation = 2U;
        entry.transition.before.now = 12U;
        entry.transition.after.now = 34U;
        for (size_t index = 0U; index < source.size(); ++index) {
            source[index] = static_cast<unsigned char>(index + 1U);
        }
        destination = source;
    }

    void TearDown() override {
        boundary = nullptr;
    }

    /* Each recipe has one firmware entry. Forking keeps its real C11 statics
     * fresh without a reset API or a C++ compilation of production source. */
    void isolated(const std::function<void()>& scenario) {
        EXPECT_EXIT(
            {
                scenario();
                const bool verified =
                    testing::Mock::VerifyAndClearExpectations(&sdk);
                std::_Exit(verified && !testing::Test::HasFailure() ? 0 : 1);
            },
            testing::ExitedWithCode(0), "");
    }

    void enter() {
        EXPECT_CALL(sdk,
                    Enter(testing::_, testing::_, testing::_, 4096U, 10000U))
            .WillOnce([this](nx_clock_barrier_t barrier, const Receiver* face,
                             Request* prepared, uint32_t, uint32_t) {
                EXPECT_EQ(barrier.domain, owners.domain);
                EXPECT_EQ(barrier.epoch, owners.epoch);
                receiver = face;
                request = prepared;
#ifdef EXAMPLE_STOP2_COPY_HOOK_TEST
                prepared->source = source.data();
                prepared->destination = destination.data();
                prepared->length = transfer_size;
#endif
                return entry;
            });
        const auto transition = example_stop2_application_enter(owners);
        EXPECT_EQ(transition.primary, entry.transition.primary);
        EXPECT_EQ(transition.remaining_effects,
                  entry.transition.remaining_effects);
        ASSERT_NE(request, nullptr);
        ASSERT_NE(receiver, nullptr);
    }

    void acquire(nx_result_t operation = NX_SUCCESS,
                 size_t count = transfer_size) {
        EXPECT_CALL(sdk, Result(&request->base, testing::_, testing::_))
            .WillOnce([operation, count](const nx_request_t*,
                                         nx_result_t* result,
                                         size_t* transferred) {
                *result = operation;
                *transferred = count;
                return NX_SUCCESS;
            });
    }

    void stop(nx_result_t cleanup = NX_SUCCESS) {
        EXPECT_CALL(sdk, Stop(receiver, 10000U))
            .WillOnce(testing::Return(cleanup));
    }

    void joined(nx_result_t cleanup, nx_result_t operation) {
        const auto result = example_stop2_application_join();
        EXPECT_EQ(result.cleanup, cleanup);
        EXPECT_EQ(result.operation, operation);
    }
};

TEST_F(Stop2RecipeHooksDeathTest, RejectedEntryNeverReadsRequestAndStillStops) {
    isolated([this] {
        entry.accepted = false;
        entry.transition.primary = NX_ERROR_PERMISSION;
        enter();
        stop();
        joined(NX_SUCCESS, NX_ERROR_STATE);
        joined(NX_SUCCESS, NX_ERROR_STATE);
    });
}

TEST_F(Stop2RecipeHooksDeathTest, NoEntryStillJoinsPartialReceiverStart) {
    isolated([this] {
        EXPECT_CALL(sdk, Stop(testing::_, 10000U))
            .WillOnce(testing::Return(NX_SUCCESS));
        joined(NX_SUCCESS, NX_SUCCESS);
        joined(NX_SUCCESS, NX_SUCCESS);
    });
}

TEST_F(Stop2RecipeHooksDeathTest, PendingRequestRetainsReceiverUntilAcquire) {
    isolated([this] {
        enter();
        EXPECT_CALL(sdk, Result(&request->base, testing::_, testing::_))
            .WillOnce(testing::Return(NX_ERROR_BUSY));
        joined(NX_ERROR_BUSY, NX_SUCCESS);
        EXPECT_CALL(sdk, Result(&request->base, testing::_, testing::_))
            .WillOnce(testing::Return(NX_ERROR_BUSY));
        joined(NX_ERROR_BUSY, NX_SUCCESS);
        acquire();
        stop();
        joined(NX_SUCCESS, NX_SUCCESS);
        joined(NX_SUCCESS, NX_SUCCESS);
    });
}

TEST_F(Stop2RecipeHooksDeathTest, ResultObservationErrorRetainsReceiver) {
    isolated([this] {
        enter();
        EXPECT_CALL(sdk, Result(&request->base, testing::_, testing::_))
            .WillOnce(testing::Return(NX_ERROR_STATE));
        joined(NX_ERROR_STATE, NX_SUCCESS);
        acquire();
        stop();
        joined(NX_SUCCESS, NX_SUCCESS);
    });
}

TEST_F(Stop2RecipeHooksDeathTest, FinalRequestFailureSurvivesBusyReceiverStop) {
    isolated([this] {
        enter();
        acquire(NX_ERROR_TIMEOUT, 0U);
        stop(NX_ERROR_BUSY);
        joined(NX_ERROR_BUSY, NX_ERROR_TIMEOUT);
        stop();
        joined(NX_SUCCESS, NX_ERROR_TIMEOUT);
        joined(NX_SUCCESS, NX_ERROR_TIMEOUT);
    });
}

TEST_F(Stop2RecipeHooksDeathTest,
       SettledBusyOperationIsDistinctFromPendingLoan) {
    isolated([this] {
        enter();
        acquire(NX_ERROR_BUSY, 0U);
        stop();
        joined(NX_SUCCESS, NX_ERROR_BUSY);
        joined(NX_SUCCESS, NX_ERROR_BUSY);
    });
}

TEST_F(Stop2RecipeHooksDeathTest, ShortTransferFailsAfterAcquiredSettlement) {
    isolated([this] {
        enter();
        acquire(NX_SUCCESS, transfer_size - 1U);
        stop();
        joined(NX_SUCCESS, NX_ERROR_IO);
        joined(NX_SUCCESS, NX_ERROR_IO);
    });
}

TEST_F(Stop2RecipeHooksDeathTest, OversizedTransferAlsoFailsExactCountCheck) {
    isolated([this] {
        enter();
        acquire(NX_SUCCESS, transfer_size + 1U);
        stop();
        joined(NX_SUCCESS, NX_ERROR_IO);
    });
}

TEST_F(Stop2RecipeHooksDeathTest,
       BusyStopRetainsAcquiredResultWithoutReacquire) {
    isolated([this] {
        enter();
        acquire();
        stop(NX_ERROR_BUSY);
        joined(NX_ERROR_BUSY, NX_SUCCESS);
        stop(NX_ERROR_BUSY);
        joined(NX_ERROR_BUSY, NX_SUCCESS);
        stop();
        joined(NX_SUCCESS, NX_SUCCESS);
        joined(NX_SUCCESS, NX_SUCCESS);
    });
}

TEST_F(Stop2RecipeHooksDeathTest, StopErrorRetainsCleanupResponsibility) {
    isolated([this] {
        enter();
        acquire();
        stop(NX_ERROR_IO);
        joined(NX_ERROR_IO, NX_SUCCESS);
        stop();
        joined(NX_SUCCESS, NX_SUCCESS);
        joined(NX_SUCCESS, NX_SUCCESS);
    });
}

#ifdef EXAMPLE_STOP2_COPY_HOOK_TEST
TEST_F(Stop2RecipeHooksDeathTest,
       BorrowedCopyBytesAreUnreadUntilSuccessfulResult) {
    isolated([this] {
        enter();
        request->source = reinterpret_cast<const void*>(uintptr_t{1U});
        request->destination = reinterpret_cast<void*>(uintptr_t{1U});
        EXPECT_CALL(sdk, Result(&request->base, testing::_, testing::_))
            .WillOnce(testing::Return(NX_ERROR_BUSY));
        joined(NX_ERROR_BUSY, NX_SUCCESS);
        acquire(NX_ERROR_IO, 0U);
        stop();
        joined(NX_SUCCESS, NX_ERROR_IO);
    });
}

TEST_F(Stop2RecipeHooksDeathTest, CopyByteMismatchFailsOnlyAfterAcquire) {
    isolated([this] {
        destination[8U] ^= 1U;
        enter();
        EXPECT_CALL(sdk, Result(&request->base, testing::_, testing::_))
            .WillOnce(testing::Return(NX_ERROR_BUSY));
        joined(NX_ERROR_BUSY, NX_SUCCESS);
        acquire();
        stop();
        joined(NX_SUCCESS, NX_ERROR_IO);
        joined(NX_SUCCESS, NX_ERROR_IO);
    });
}

#endif

TEST_F(Stop2RecipeHooksDeathTest, MissingDmaWakePreventsSuccessfulOperation) {
    isolated([this] {
        entry.dma_wake = false;
        enter();
        acquire();
        stop();
        joined(NX_SUCCESS, NX_ERROR_IO);
    });
}

TEST_F(Stop2RecipeHooksDeathTest, ChangedRetainedGenerationFailsOperation) {
    isolated([this] {
        entry.transition.after.generation += 1U;
        enter();
        acquire();
        stop();
        joined(NX_SUCCESS, NX_ERROR_IO);
    });
}
} /* namespace */

extern "C" {
#ifdef EXAMPLE_STOP2_COPY_HOOK_TEST
nx_stm32u575_stop2_copy_result_t
nx_stm32u575_stop2_copy_enter(nx_clock_barrier_t owners,
                              const nx_stm32u575_lpdma_copy_t* copy,
                              nx_stm32u575_lpdma_copy_request_t* request,
                              uint32_t escape_ticks, uint32_t poll_limit) {
    return boundary->Enter(owners, copy, request, escape_ticks, poll_limit);
}
nx_result_t nx_stm32u575_lpdma_copy_stop(const nx_stm32u575_lpdma_copy_t* copy,
                                         uint32_t poll_limit) {
    return boundary->Stop(copy, poll_limit);
}
#else
nx_stm32u575_stop2_lptim1_result_t
nx_stm32u575_stop2_lptim1_enter(nx_clock_barrier_t owners,
                                const nx_stm32u575_lpdma_lptim1_t* receiver,
                                nx_stm32u575_lpdma_lptim1_request_t* request,
                                uint32_t escape_ticks, uint32_t poll_limit) {
    return boundary->Enter(owners, receiver, request, escape_ticks, poll_limit);
}
nx_result_t
nx_stm32u575_lpdma_lptim1_stop(const nx_stm32u575_lpdma_lptim1_t* receiver,
                               uint32_t poll_limit) {
    return boundary->Stop(receiver, poll_limit);
}
#endif
nx_result_t nx_request_result(const nx_request_t* request, nx_result_t* result,
                              size_t* transferred) {
    return boundary->Result(request, result, transferred);
}
} /* extern "C" */
