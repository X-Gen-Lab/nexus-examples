/**
 * \file            audio_acquisition.h
 * \brief           Caller-owned finite-window audio block application
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-23
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#ifndef EXAMPLE_AUDIO_ACQUISITION_H
#define EXAMPLE_AUDIO_ACQUISITION_H
#include "nexus/io/audio.h"
#ifdef __cplusplus
extern "C" {
#endif
/** \brief Optional TX producer and independent post-join consumer token.
 *
 * \note Caller initializes stream and publishes every slot READY with exact
 *       complete-frame patterns before start. The application explicitly
 *       replays each detached slot's existing pattern; it inserts no silence
 *       or conversion. blocks counts observed returned storage, not wire ACKs.
 *       This state, its stream and payloads are separate from RX/app storage.
 */
typedef struct {
    nx_stream_t* stream;
    nx_stream_block_t pending;
    nx_stream_fill_t fill;
    size_t blocks;
    bool has_pending;
    bool has_fill;
} example_audio_playback_t;
/** \brief Stable unique audio session with copied storage observations.
 *
 * \note Initially zeroed. stream is RX or NULL for TX-only; playback is an
 *       optional caller-owned TX sidecar, not a default maximum pool. blocks
 *       and frames count RX when selected, otherwise detached TX storage.
 *       first_container copies raw stored bytes, preserving the queried layout
 *       and high-halfword order; it is not a converted PCM integer. All state
 *       remains live until active is false and independent observers join.
 */
typedef struct {
    const nx_audio_port_t* port;
    nx_stream_t* stream;
    example_audio_playback_t* playback;
    nx_audio_blocks_request_t request;
    nx_audio_layout_t layout;
    nx_stream_block_t pending;
    nx_time_us_t deadline;
    nx_time_us_t last_now;
    size_t frame_bytes;
    size_t block_bytes;
    size_t block_limit;
    size_t blocks;
    size_t frames;
    uint8_t first_container[4];
    nx_result_t result;
    bool initialized;
    bool active;
    bool stopping;
    bool source_stopped;
    bool has_pending;
} example_audio_acquisition_t;

/**
 * \brief Start the RX-only complete-frame application window.
 *
 * \param[in,out] app: Stable, independent, first-zeroed or fully joined state.
 *
 * \param[in] port: Prepared continuous RX audio owner, with no other executor.
 *
 * \param[in,out] stream: Genuine initialized FREE RX stream and exact pool.
 *
 * \param[in] block_bytes: Positive complete-frame bytes per hardware block.
 *
 * \param[in] block_limit: Positive complete blocks to consume before stopping.
 *
 * \param[in] now: Finite ready snapshot in the owner's clock domain.
 *
 * \param[in] duration_us: Representable startup-inclusive relative budget.
 *
 * \return SUCCESS admits; other results retain no new session/stream borrow.
 *
 * \note Task-only shorthand for start_streams without TX. Full app, known
 *       controls and payload reservations must be independent; malformed spans
 *       reject before port callbacks or writes. Provider admission still owns
 *       private context sizes, DMA accessibility and serializer capabilities.
 */
nx_result_t example_audio_acquisition_start(
    example_audio_acquisition_t* app, const nx_audio_port_t* port,
    nx_stream_t* stream, size_t block_bytes, size_t block_limit,
    nx_time_us_t now, uint64_t duration_us);

/**
 * \brief Start TX-only, RX-only or duplex under one continuous audio owner.
 *
 * \param[in,out] app: Stable first-zeroed or fully joined application storage.
 *
 * \param[in] port: Prepared selected audio owner; same serialized task methods.
 *
 * \param[in,out] playback: Optional explicit TX state and fully READY pattern
 *       stream. Caller cedes its producer role while active; no hidden refill.
 *
 * \param[in,out] receive: Optional FREE RX stream, distinct from TX and all
 *       control/slot/payload reservations; at least one direction is required.
 *
 * \param[in] block_bytes: Positive whole stored frames, identical per
 * direction.
 *
 * \param[in] block_limit: Positive RX block target, or returned TX
 * block target for TX-only. Unplayed queued TX blocks never advance the target.
 *
 * \param[in] now: Finite ready snapshot in the provider's live clock domain.
 *
 * \param[in] duration_us: Representable startup-inclusive relative budget.
 *
 * \return SUCCESS admits the original embedded request through SETTLED;
 *       rejected admission creates no borrow. Span/alias errors are INVALID,
 *       without callbacks or app writes; deadline overflow is EXHAUSTED.
 *
 * \note Task-only cold call. The caller supplies genuine initialized storage;
 *       extent checks cannot prove arbitrary memory mapped or infer the full
 *       opaque provider context. Query layout governs sample/container/slots;
 *       no PCM conversion or physical sample-rate qualification is implied.
 *       External slave clocks must remain available for required withdrawal.
 */
nx_result_t example_audio_acquisition_start_streams(
    example_audio_acquisition_t* app, const nx_audio_port_t* port,
    example_audio_playback_t* playback, nx_stream_t* receive,
    size_t block_bytes, size_t block_limit, nx_time_us_t now,
    uint64_t duration_us);

/**
 * \brief Advance one bounded application cycle and both independent exits.
 *
 * \param[in,out] app: Same unique executor's stable session and sidecars.
 *
 * \param[in] now: Nondecreasing snapshot in the original live clock domain.
 *
 * \return BUSY while work remains, otherwise operation/progress error or final
 *       result. Errors and deadlines do not permit reclaim while active.
 *
 * \note At most one provider service, one TX replay, one RX block and one
 *       provider stop per call. After owner SETTLED, discard at most one queued
 *       TX block. A failed TX or RX release preserves its separate exact token.
 *       active becomes false only after request settlement and both streams
 *       fully join. It does not join external observers or stop Platform.
 */
nx_result_t example_audio_acquisition_step(example_audio_acquisition_t* app,
                                           nx_time_us_t now);

/**
 * \brief Record cancellation without ending either direction's borrow.
 *
 * \param[in,out] app: Active graph under the same serialized task executor.
 *
 * \return SUCCESS records intent; STATE for absent/inactive storage.
 *
 * \note Continue step and retain every request, pool and sidecar while active.
 *       This does not reset QUARANTINED hardware or substitute for observer
 *       joins. It provides no cross-task or asynchronous cancellation API.
 */
nx_result_t example_audio_acquisition_cancel(example_audio_acquisition_t* app);
#ifdef __cplusplus
}
#endif
#endif
