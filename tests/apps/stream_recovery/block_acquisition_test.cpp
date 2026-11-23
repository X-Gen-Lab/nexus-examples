/**
 * \file            block_acquisition_test.cpp
 * \brief           Actual ADC and audio application loans survive failed exit
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-23
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "adc_acquisition.h"
#include "audio_acquisition.h"
#include <cstring>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;

namespace {
class ReleaseBoundary {
  public:
    MOCK_METHOD(nx_result_t, release, (nx_stream_t*, const nx_stream_block_t*));
};
ReleaseBoundary* releases;
size_t unexpected_provider_calls;

class Blocks : public testing::Test {
  protected:
    NiceMock<ReleaseBoundary> release;
    uint16_t payload[3][8]{};
    nx_stream_slot_t slots[3]{};
    nx_stream_t stream{};

    void SetUp() override {
        releases = &release;
        for (size_t i = 0U; i < 3U; ++i) {
            slots[i].data = reinterpret_cast<uint8_t*>(payload[i]);
            slots[i].capacity = sizeof(payload[i]);
        }
        ASSERT_EQ(nx_stream_initialize(&stream, slots, 3U), NX_SUCCESS);
        ON_CALL(release, release(_, _))
            .WillByDefault(Invoke(nx_stream_release));
    }

    void publish(size_t bytes = 8U, uint32_t flags = 0U) {
        nx_stream_fill_t fill{};
        ASSERT_EQ(nx_stream_reserve(&stream, &fill), NX_SUCCESS);
        const uint16_t samples[] = {12U, 34U, 56U, 78U};
        std::memcpy(fill.data, samples, sizeof(samples));
        if ((flags & NX_STREAM_BOUNDARY_LOSS) != 0U) {
            nx_stream_note_loss(&stream, 1U);
        }
        ASSERT_EQ(nx_stream_publish(&stream, &fill, bytes,
                                    flags & ~NX_STREAM_BOUNDARY_LOSS),
                  NX_SUCCESS);
    }
};

class AdcBoundary {
  public:
    MOCK_METHOD(nx_result_t, info, (nx_adc_info_t*), (const));
    MOCK_METHOD(nx_result_t, start, (nx_stream_t*, uint32_t));
    MOCK_METHOD(nx_result_t, service, ());
    MOCK_METHOD(nx_result_t, stop, ());
};

class AdcAcquisition : public Blocks {
  protected:
    NiceMock<AdcBoundary> model;
    nx_adc_ops_t operations{};
    nx_adc_port_t port{&operations, &model};
    example_adc_acquisition_t app{};

    void SetUp() override {
        Blocks::SetUp();
        operations.info = [](const void* context, nx_adc_info_t* info) {
            return static_cast<const AdcBoundary*>(context)->info(info);
        };
        operations.stream_start =
            [](void* context, nx_stream_t* selected_stream, uint32_t hz) {
                return static_cast<AdcBoundary*>(context)->start(
                    selected_stream, hz);
            };
        operations.stream_service = [](void* context) {
            return static_cast<AdcBoundary*>(context)->service();
        };
        operations.stream_stop = [](void* context) {
            return static_cast<AdcBoundary*>(context)->stop();
        };
        ON_CALL(model, info(_)).WillByDefault(Invoke([](nx_adc_info_t* info) {
            *info = {12U, 3300U, 2U};
            return NX_SUCCESS;
        }));
        ON_CALL(model, start(_, _)).WillByDefault(Return(NX_SUCCESS));
        ON_CALL(model, service()).WillByDefault(Return(NX_SUCCESS));
        ON_CALL(model, stop()).WillByDefault(Return(NX_SUCCESS));
    }

    nx_result_t start(size_t limit = 1U) {
        return example_adc_acquisition_start(&app, &port, &stream, 1000U, limit,
                                             10U, 100U);
    }
};

TEST_F(AdcAcquisition, DeadlineOverflowRejectsBeforeTouchingProvider) {
    EXPECT_CALL(model, info(_)).Times(0);
    EXPECT_CALL(model, start(_, _)).Times(0);
    EXPECT_EQ(example_adc_acquisition_start(&app, &port, &stream, 1000U, 1U,
                                            NX_DEADLINE_NEVER - 2U, 3U),
              NX_ERROR_EXHAUSTED);
    EXPECT_FALSE(app.active);
}

TEST_F(AdcAcquisition, RejectedStartDoesNotBecomeLive) {
    EXPECT_CALL(model, start(&stream, 1000U))
        .WillOnce(Return(NX_ERROR_UNSUPPORTED));
    EXPECT_EQ(start(), NX_ERROR_UNSUPPORTED);
    EXPECT_FALSE(app.active);
    EXPECT_EQ(stream.outstanding, 0U);
}

TEST_F(AdcAcquisition, ConsumesWholeScansAndJoinsNormalWindow) {
    ASSERT_EQ(start(), NX_SUCCESS);
    publish();
    EXPECT_EQ(example_adc_acquisition_step(&app, 11U), NX_SUCCESS);
    EXPECT_FALSE(app.active);
    EXPECT_EQ(app.blocks, 1U);
    EXPECT_EQ(app.scans, 2U);
    EXPECT_EQ(app.last_count, 78U);
    EXPECT_EQ(stream.outstanding, 0U);
    EXPECT_TRUE(stream.stopping);
}

TEST_F(AdcAcquisition, OneStepConsumesAtMostOneBlock) {
    ASSERT_EQ(start(2U), NX_SUCCESS);
    publish();
    publish();
    EXPECT_CALL(model, service()).Times(1);
    EXPECT_EQ(example_adc_acquisition_step(&app, 11U), NX_ERROR_BUSY);
    EXPECT_TRUE(app.active);
    EXPECT_EQ(app.blocks, 1U);
    EXPECT_EQ(stream.outstanding, 1U);
    testing::Mock::VerifyAndClearExpectations(&model);
    EXPECT_EQ(example_adc_acquisition_step(&app, 12U), NX_SUCCESS);
    EXPECT_EQ(app.blocks, 2U);
}

TEST_F(AdcAcquisition, ReleaseFailureKeepsExactLoanWithoutDoubleProcessing) {
    ASSERT_EQ(start(), NX_SUCCESS);
    publish();
    EXPECT_CALL(release, release(&stream, _)).WillOnce(Return(NX_ERROR_IO));
    EXPECT_EQ(example_adc_acquisition_step(&app, 11U), NX_ERROR_IO);
    ASSERT_TRUE(app.has_pending);
    const nx_stream_block_t token = app.pending;
    EXPECT_EQ(slots[0].state, NX_STREAM_SLOT_BORROWED);
    EXPECT_TRUE(app.active);
    EXPECT_CALL(release, release(&stream, &app.pending))
        .WillOnce(Invoke([&](nx_stream_t* s, const nx_stream_block_t* block) {
            EXPECT_EQ(block->epoch, token.epoch);
            EXPECT_EQ(block->slot, token.slot);
            return nx_stream_release(s, block);
        }));
    EXPECT_EQ(example_adc_acquisition_step(&app, 12U), NX_ERROR_IO);
    EXPECT_FALSE(app.active);
    EXPECT_EQ(app.blocks, 1U);
}

TEST_F(AdcAcquisition, TimeoutDoesNotRevokeFillingLoanOrBusyStop) {
    ASSERT_EQ(start(), NX_SUCCESS);
    nx_stream_fill_t fill{};
    ASSERT_EQ(nx_stream_reserve(&stream, &fill), NX_SUCCESS);
    EXPECT_CALL(model, stop()).WillOnce(Return(NX_ERROR_BUSY));
    EXPECT_EQ(example_adc_acquisition_step(&app, 110U), NX_ERROR_BUSY);
    EXPECT_TRUE(app.active);
    EXPECT_EQ(slots[0].state, NX_STREAM_SLOT_FILLING);
    EXPECT_EQ(nx_stream_abort(&stream, &fill, true), NX_SUCCESS);
    testing::Mock::VerifyAndClearExpectations(&model);
    EXPECT_EQ(example_adc_acquisition_step(&app, 111U), NX_ERROR_TIMEOUT);
    EXPECT_FALSE(app.active);
}

TEST_F(AdcAcquisition, LossStopsWithoutReusingInvalidSample) {
    ASSERT_EQ(start(), NX_SUCCESS);
    publish(8U, NX_STREAM_BOUNDARY_LOSS);
    EXPECT_EQ(example_adc_acquisition_step(&app, 11U), NX_ERROR_IO);
    EXPECT_FALSE(app.active);
    EXPECT_EQ(app.blocks, 0U);
    EXPECT_EQ(app.scans, 0U);
}

TEST_F(AdcAcquisition, IncompleteScanIsReleasedAndReported) {
    ASSERT_EQ(start(), NX_SUCCESS);
    publish(6U);
    EXPECT_EQ(example_adc_acquisition_step(&app, 11U), NX_ERROR_IO);
    EXPECT_FALSE(app.active);
    EXPECT_EQ(stream.outstanding, 0U);
    EXPECT_EQ(app.scans, 0U);
}

TEST_F(AdcAcquisition, ExplicitCancelIsBoundedAndRetainsFailedStop) {
    ASSERT_EQ(start(), NX_SUCCESS);
    EXPECT_EQ(example_adc_acquisition_cancel(&app), NX_SUCCESS);
    EXPECT_CALL(model, stop()).WillOnce(Return(NX_ERROR_IO));
    EXPECT_EQ(example_adc_acquisition_step(&app, 11U), NX_ERROR_IO);
    EXPECT_TRUE(app.active);
    testing::Mock::VerifyAndClearExpectations(&model);
    EXPECT_EQ(example_adc_acquisition_step(&app, 12U), NX_ERROR_CANCELLED);
    EXPECT_FALSE(app.active);
}

TEST_F(AdcAcquisition, PayloadCannotOverwriteAnyApplicationControl) {
    slots[0].data = reinterpret_cast<uint8_t*>(&app.result);
    slots[0].capacity = sizeof(app.result);
    app.result = NX_ERROR_NACK;
    EXPECT_CALL(model, info(_)).Times(0);
    EXPECT_CALL(model, start(_, _)).Times(0);
    EXPECT_EQ(start(), NX_ERROR_INVALID);
    EXPECT_EQ(app.result, NX_ERROR_NACK);
    EXPECT_FALSE(app.active);
}

TEST_F(AdcAcquisition, SlotMetadataCannotOverlapApplicationStorage) {
    stream.slots = reinterpret_cast<nx_stream_slot_t*>(&app);
    stream.count = 1U;
    EXPECT_CALL(model, info(_)).Times(0);
    EXPECT_CALL(model, start(_, _)).Times(0);
    EXPECT_EQ(start(), NX_ERROR_INVALID);
    EXPECT_FALSE(app.active);
}

TEST_F(AdcAcquisition, WrappedReservationRejectsBeforeProviderCalls) {
    slots[0].data = reinterpret_cast<uint8_t*>(UINTPTR_MAX - 1U);
    slots[0].capacity = 4U;
    EXPECT_CALL(model, info(_)).Times(0);
    EXPECT_CALL(model, start(_, _)).Times(0);
    EXPECT_EQ(start(), NX_ERROR_INVALID);
    EXPECT_FALSE(app.active);
}

TEST_F(AdcAcquisition, IndependentSlotsCannotSharePayloadReservations) {
    slots[1].data = slots[0].data + 1U;
    EXPECT_CALL(model, info(_)).Times(0);
    EXPECT_CALL(model, start(_, _)).Times(0);
    EXPECT_EQ(start(), NX_ERROR_INVALID);
}

TEST_F(AdcAcquisition, OpaqueContextCannotBeginInsideApplicationStorage) {
    unexpected_provider_calls = 0U;
    port.context = &app.result;
    operations.info = [](const void*, nx_adc_info_t* info) {
        ++unexpected_provider_calls;
        *info = {12U, 3300U, 2U};
        return NX_SUCCESS;
    };
    operations.stream_start = [](void*, nx_stream_t*, uint32_t) {
        ++unexpected_provider_calls;
        return NX_SUCCESS;
    };
    EXPECT_EQ(start(), NX_ERROR_INVALID);
    EXPECT_EQ(unexpected_provider_calls, 0U);
    EXPECT_FALSE(app.active);
}

class AudioBoundary {
  public:
    MOCK_METHOD(nx_result_t, layout, (nx_audio_layout_t*));
    MOCK_METHOD(nx_result_t, start, (nx_audio_blocks_request_t*));
    MOCK_METHOD(nx_result_t, service, ());
    MOCK_METHOD(nx_result_t, stop, ());
};

class AudioAcquisition : public Blocks {
  protected:
    NiceMock<AudioBoundary> model;
    nx_audio_ops_t operations{};
    nx_audio_port_t port{&operations, &model};
    example_audio_acquisition_t app{};
    nx_audio_blocks_request_t* session{};

    void SetUp() override {
        Blocks::SetUp();
        operations.layout = [](void* context, nx_audio_layout_t* layout) {
            return static_cast<AudioBoundary*>(context)->layout(layout);
        };
        operations.start_blocks = [](void* context,
                                     nx_audio_blocks_request_t* request) {
            return static_cast<AudioBoundary*>(context)->start(request);
        };
        operations.service = [](void* context) {
            return static_cast<AudioBoundary*>(context)->service();
        };
        operations.stop = [](void* context) {
            return static_cast<AudioBoundary*>(context)->stop();
        };
        ON_CALL(model, layout(_))
            .WillByDefault(Invoke([](nx_audio_layout_t* layout) {
                *layout = {NX_AUDIO_STORAGE_LITTLE_ENDIAN,
                           NX_FRAME_SAMPLE_LSB,
                           3U,
                           16U,
                           16U,
                           2U};
                return NX_SUCCESS;
            }));
        ON_CALL(model, start(_))
            .WillByDefault(Invoke([&](nx_audio_blocks_request_t* request) {
                session = request;
                EXPECT_EQ(request->tx, nullptr);
                EXPECT_EQ(request->rx, &stream);
                return nx_request_admit(&request->base, NX_REQUEST_ACTIVE);
            }));
        ON_CALL(model, service()).WillByDefault(Return(NX_SUCCESS));
        ON_CALL(model, stop()).WillByDefault(Invoke([&]() {
            if (nx_request_state(&session->base) != NX_REQUEST_SETTLED) {
                nx_request_settle(&session->base, NX_ERROR_CANCELLED, 0U);
            }
            return NX_SUCCESS;
        }));
    }

    nx_result_t start(size_t limit = 1U, size_t block_bytes = 8U) {
        return example_audio_acquisition_start(&app, &port, &stream,
                                               block_bytes, limit, 10U, 100U);
    }
};

TEST_F(AudioAcquisition, DeadlineOverflowHasNoProviderEffects) {
    EXPECT_CALL(model, layout(_)).Times(0);
    EXPECT_CALL(model, start(_)).Times(0);
    EXPECT_EQ(example_audio_acquisition_start(&app, &port, &stream, 8U, 1U,
                                              NX_DEADLINE_NEVER - 1U, 2U),
              NX_ERROR_EXHAUSTED);
    EXPECT_FALSE(app.active);
}

TEST_F(AudioAcquisition, NonWholeFrameRejectsBeforeSessionAdmission) {
    EXPECT_CALL(model, start(_)).Times(0);
    EXPECT_EQ(start(1U, 6U), NX_ERROR_INVALID);
    EXPECT_FALSE(app.active);
}

TEST_F(AudioAcquisition, ReceivesWholeFramesAndJoinsSessionAndStream) {
    ASSERT_EQ(start(), NX_SUCCESS);
    publish();
    EXPECT_EQ(example_audio_acquisition_step(&app, 11U), NX_SUCCESS);
    EXPECT_FALSE(app.active);
    EXPECT_EQ(app.blocks, 1U);
    EXPECT_EQ(app.frames, 2U);
    EXPECT_EQ(app.first_container[0], 12U);
    EXPECT_EQ(app.first_container[1], 0U);
    EXPECT_EQ(nx_request_state(&app.request.base), NX_REQUEST_SETTLED);
    EXPECT_EQ(stream.outstanding, 0U);
}

TEST_F(AudioAcquisition, OneStepNeverRunsAnUnboundedReceiveLoop) {
    ASSERT_EQ(start(2U), NX_SUCCESS);
    publish();
    publish();
    EXPECT_CALL(model, service()).Times(1);
    EXPECT_EQ(example_audio_acquisition_step(&app, 11U), NX_ERROR_BUSY);
    EXPECT_EQ(app.blocks, 1U);
    EXPECT_TRUE(app.active);
    testing::Mock::VerifyAndClearExpectations(&model);
    EXPECT_EQ(example_audio_acquisition_step(&app, 12U), NX_SUCCESS);
}

TEST_F(AudioAcquisition, FailedReleaseRetainsRequestAndExactBlock) {
    ASSERT_EQ(start(), NX_SUCCESS);
    publish();
    EXPECT_CALL(release, release(_, _)).WillOnce(Return(NX_ERROR_STATE));
    EXPECT_EQ(example_audio_acquisition_step(&app, 11U), NX_ERROR_STATE);
    ASSERT_TRUE(app.has_pending);
    const auto token = app.pending;
    EXPECT_TRUE(app.active);
    EXPECT_EQ(slots[0].state, NX_STREAM_SLOT_BORROWED);
    testing::Mock::VerifyAndClearExpectations(&release);
    EXPECT_EQ(example_audio_acquisition_step(&app, 12U), NX_ERROR_STATE);
    EXPECT_FALSE(app.active);
    EXPECT_EQ(app.pending.epoch, token.epoch);
    EXPECT_EQ(app.blocks, 1U);
}

TEST_F(AudioAcquisition, QuarantinedStopKeepsSessionAndStorageLive) {
    ASSERT_EQ(start(), NX_SUCCESS);
    EXPECT_CALL(model, stop()).WillOnce(Invoke([&]() {
        EXPECT_EQ(nx_request_transition(&session->base, NX_REQUEST_QUARANTINED),
                  NX_SUCCESS);
        return NX_ERROR_IO;
    }));
    EXPECT_EQ(example_audio_acquisition_step(&app, 110U), NX_ERROR_IO);
    EXPECT_TRUE(app.active);
    EXPECT_EQ(nx_request_state(&app.request.base), NX_REQUEST_QUARANTINED);
    testing::Mock::VerifyAndClearExpectations(&model);
    EXPECT_EQ(example_audio_acquisition_step(&app, 111U), NX_ERROR_TIMEOUT);
    EXPECT_FALSE(app.active);
}

TEST_F(AudioAcquisition, SettledSessionStillDrainsEveryReadyConsumerBlock) {
    ASSERT_EQ(start(1U), NX_SUCCESS);
    publish();
    publish();
    EXPECT_EQ(example_audio_acquisition_step(&app, 11U), NX_ERROR_BUSY);
    EXPECT_TRUE(app.active);
    EXPECT_EQ(nx_request_state(&app.request.base), NX_REQUEST_SETTLED);
    EXPECT_EQ(stream.outstanding, 1U);
    EXPECT_EQ(example_audio_acquisition_step(&app, 12U), NX_SUCCESS);
    EXPECT_FALSE(app.active);
    EXPECT_EQ(app.blocks, 1U);
}

TEST_F(AudioAcquisition, ErrorBlockRetainsNoFalseFrameCount) {
    ASSERT_EQ(start(), NX_SUCCESS);
    publish(8U, NX_STREAM_BOUNDARY_ERROR);
    EXPECT_EQ(example_audio_acquisition_step(&app, 11U), NX_ERROR_IO);
    EXPECT_FALSE(app.active);
    EXPECT_EQ(app.frames, 0U);
}

TEST_F(AudioAcquisition, CancelRetainsBusyProducerAndReturnsFinalReason) {
    ASSERT_EQ(start(), NX_SUCCESS);
    EXPECT_EQ(example_audio_acquisition_cancel(&app), NX_SUCCESS);
    EXPECT_CALL(model, stop()).WillOnce(Return(NX_ERROR_BUSY));
    EXPECT_EQ(example_audio_acquisition_step(&app, 11U), NX_ERROR_BUSY);
    EXPECT_TRUE(app.active);
    testing::Mock::VerifyAndClearExpectations(&model);
    EXPECT_EQ(example_audio_acquisition_step(&app, 12U), NX_ERROR_CANCELLED);
    EXPECT_FALSE(app.active);
}

class AudioPlayback : public AudioAcquisition {
  protected:
    uint16_t tx_payload[3][8]{};
    nx_stream_slot_t tx_slots[3]{};
    nx_stream_t tx{};
    example_audio_playback_t playback{};
    nx_stream_block_t hardware[2]{};
    bool held[2]{};
    size_t bank{};

    void SetUp() override {
        AudioAcquisition::SetUp();
        for (size_t i = 0U; i < 3U; ++i) {
            tx_slots[i].data = reinterpret_cast<uint8_t*>(tx_payload[i]);
            tx_slots[i].capacity = sizeof(tx_payload[i]);
        }
        ASSERT_EQ(nx_stream_initialize(&tx, tx_slots, 3U), NX_SUCCESS);
        playback.stream = &tx;
        for (size_t i = 0U; i < 3U; ++i) {
            nx_stream_fill_t fill{};
            ASSERT_EQ(nx_stream_reserve(&tx, &fill), NX_SUCCESS);
            std::memset(fill.data, static_cast<int>(i + 1U), 8U);
            ASSERT_EQ(nx_stream_publish(&tx, &fill, 8U, 0U), NX_SUCCESS);
        }
        ON_CALL(model, start(_))
            .WillByDefault(Invoke([&](nx_audio_blocks_request_t* request) {
                session = request;
                EXPECT_EQ(request->tx, &tx);
                nx_result_t result =
                    nx_request_admit(&request->base, NX_REQUEST_ACTIVE);
                if (result != NX_SUCCESS) {
                    return result;
                }
                for (size_t i = 0U; i < 2U; ++i) {
                    EXPECT_EQ(nx_stream_acquire(&tx, &hardware[i]), NX_SUCCESS);
                    held[i] = true;
                }
                return NX_SUCCESS;
            }));
        ON_CALL(model, service()).WillByDefault(Invoke([&]() {
            EXPECT_TRUE(held[bank]);
            EXPECT_EQ(nx_stream_release(&tx, &hardware[bank]), NX_SUCCESS);
            held[bank] = false;
            EXPECT_EQ(nx_stream_acquire(&tx, &hardware[bank]), NX_SUCCESS);
            held[bank] = true;
            bank = 1U - bank;
            if (session->rx != nullptr) {
                publish();
            }
            return NX_SUCCESS;
        }));
        ON_CALL(model, stop()).WillByDefault(Invoke([&]() {
            for (size_t i = 0U; i < 2U; ++i) {
                if (held[i]) {
                    EXPECT_EQ(nx_stream_release(&tx, &hardware[i]), NX_SUCCESS);
                    held[i] = false;
                }
            }
            nx_request_settle(&session->base, NX_ERROR_CANCELLED, 8U);
            return NX_SUCCESS;
        }));
    }

    nx_result_t play(bool duplex, size_t limit = 1U) {
        return example_audio_acquisition_start_streams(
            &app, &port, &playback, duplex ? &stream : nullptr, 8U, limit, 10U,
            100U);
    }
};

TEST_F(AudioPlayback, TransmitWindowCountsOnlyDetachedBlocks) {
    ASSERT_EQ(play(false), NX_SUCCESS);
    EXPECT_EQ(app.blocks, 0U);
    EXPECT_EQ(example_audio_acquisition_step(&app, 11U), NX_SUCCESS);
    EXPECT_EQ(playback.blocks, 1U);
    EXPECT_EQ(app.blocks, 1U);
    EXPECT_EQ(app.frames, 2U);
    EXPECT_EQ(app.first_container[0], 1U);
    EXPECT_FALSE(app.active);
    EXPECT_EQ(tx.outstanding, 0U);
}

TEST_F(AudioPlayback, ExplicitRepeatingPatternKeepsOneBlockStepBudget) {
    ASSERT_EQ(play(false, 2U), NX_SUCCESS);
    EXPECT_EQ(example_audio_acquisition_step(&app, 11U), NX_ERROR_BUSY);
    EXPECT_EQ(playback.blocks, 1U);
    EXPECT_EQ(tx_slots[0].state, NX_STREAM_SLOT_READY);
    EXPECT_EQ(tx_payload[0][0], 0x0101U);
    EXPECT_EQ(example_audio_acquisition_step(&app, 12U), NX_SUCCESS);
    EXPECT_EQ(playback.blocks, 2U);
    EXPECT_FALSE(app.active);
    EXPECT_EQ(tx.outstanding, 0U);
}

TEST_F(AudioPlayback, DuplexJoinsBothDirectionsBeforeReportingCompletion) {
    ASSERT_EQ(play(true), NX_SUCCESS);
    EXPECT_EQ(example_audio_acquisition_step(&app, 11U), NX_SUCCESS);
    EXPECT_EQ(app.blocks, 1U);
    EXPECT_EQ(playback.blocks, 1U);
    EXPECT_EQ(tx.outstanding, 0U);
    EXPECT_EQ(stream.outstanding, 0U);
    EXPECT_FALSE(app.active);
}

TEST_F(AudioPlayback, DetachedTxDoesNotRevokeFailedRxConsumerLoan) {
    ASSERT_EQ(play(true), NX_SUCCESS);
    EXPECT_CALL(release, release(&tx, _)).WillOnce(Invoke(nx_stream_release));
    EXPECT_CALL(release, release(&stream, _)).WillOnce(Return(NX_ERROR_IO));
    EXPECT_EQ(example_audio_acquisition_step(&app, 11U), NX_ERROR_IO);
    EXPECT_EQ(tx.outstanding, 0U);
    ASSERT_TRUE(app.has_pending);
    EXPECT_TRUE(app.active);
    const auto token = app.pending;
    testing::Mock::VerifyAndClearExpectations(&release);
    EXPECT_CALL(release, release(&stream, &app.pending))
        .WillOnce(
            Invoke([&](nx_stream_t* value, const nx_stream_block_t* block) {
                EXPECT_EQ(block->epoch, token.epoch);
                return nx_stream_release(value, block);
            }));
    EXPECT_EQ(example_audio_acquisition_step(&app, 12U), NX_ERROR_IO);
    EXPECT_FALSE(app.active);
}

TEST_F(AudioPlayback, FailedTxQueueReleaseRetainsItsSeparateToken) {
    ASSERT_EQ(play(true), NX_SUCCESS);
    EXPECT_CALL(release, release(&stream, _))
        .WillOnce(Invoke(nx_stream_release));
    EXPECT_CALL(release, release(&tx, _)).WillOnce(Return(NX_ERROR_STATE));
    EXPECT_EQ(example_audio_acquisition_step(&app, 11U), NX_ERROR_STATE);
    ASSERT_TRUE(playback.has_pending);
    EXPECT_FALSE(app.has_pending);
    EXPECT_TRUE(app.active);
    EXPECT_EQ(stream.outstanding, 0U);
    const auto token = playback.pending;
    testing::Mock::VerifyAndClearExpectations(&release);
    EXPECT_CALL(release, release(&tx, &playback.pending))
        .WillOnce(
            Invoke([&](nx_stream_t* value, const nx_stream_block_t* block) {
                EXPECT_EQ(block->epoch, token.epoch);
                return nx_stream_release(value, block);
            }));
    EXPECT_EQ(example_audio_acquisition_step(&app, 12U), NX_ERROR_STATE);
    EXPECT_FALSE(app.active);
}

TEST_F(AudioPlayback, QuarantinedDuplexKeepsBothSessionReferences) {
    ASSERT_EQ(play(true), NX_SUCCESS);
    ASSERT_EQ(example_audio_acquisition_cancel(&app), NX_SUCCESS);
    EXPECT_CALL(model, stop()).WillOnce(Invoke([&]() {
        EXPECT_EQ(nx_request_transition(&session->base, NX_REQUEST_QUARANTINED),
                  NX_SUCCESS);
        return NX_ERROR_IO;
    }));
    EXPECT_EQ(example_audio_acquisition_step(&app, 11U), NX_ERROR_IO);
    EXPECT_TRUE(app.active);
    EXPECT_EQ(session->tx, &tx);
    EXPECT_EQ(session->rx, &stream);
    EXPECT_EQ(tx.outstanding, 2U);
    testing::Mock::VerifyAndClearExpectations(&model);
    ON_CALL(model, service()).WillByDefault(Return(NX_ERROR_BUSY));
    EXPECT_EQ(example_audio_acquisition_step(&app, 12U), NX_ERROR_CANCELLED);
    EXPECT_FALSE(app.active);
    EXPECT_EQ(tx.outstanding, 0U);
}

TEST_F(AudioPlayback, SameStreamCannotOwnBothAudioDirections) {
    EXPECT_CALL(model, start(_)).Times(0);
    EXPECT_EQ(example_audio_acquisition_start_streams(&app, &port, &playback,
                                                      &tx, 8U, 1U, 10U, 100U),
              NX_ERROR_INVALID);
}

TEST_F(AudioPlayback, PayloadCannotOverwriteOptionalTransmitControl) {
    tx_slots[0].data = reinterpret_cast<uint8_t*>(&playback.blocks);
    tx_slots[0].capacity = sizeof(playback.blocks);
    EXPECT_CALL(model, layout(_)).Times(0);
    EXPECT_CALL(model, start(_)).Times(0);
    EXPECT_EQ(play(false), NX_ERROR_INVALID);
    EXPECT_FALSE(app.active);
}

TEST_F(AudioPlayback, DuplexDirectionsCannotShareAnyPayloadReservation) {
    tx_slots[0].data = slots[0].data;
    EXPECT_CALL(model, layout(_)).Times(0);
    EXPECT_CALL(model, start(_)).Times(0);
    EXPECT_EQ(play(true), NX_ERROR_INVALID);
    EXPECT_FALSE(app.active);
}
} /* namespace */

extern "C" nx_result_t
example_test_block_release(nx_stream_t* stream,
                           const nx_stream_block_t* block) {
    return releases->release(stream, block);
}
