/**
 * \file            audio_acquisition.c
 * \brief           Finite-window audio RX with explicit session and loan join
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-23
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "audio_acquisition.h"
#include "acquisition_storage.h"
#include <string.h>

/** \brief Preserve the first failure while continuing the required cleanup. */
static void fail(example_audio_acquisition_t* app, nx_result_t result) {
    if (app->result == NX_SUCCESS) {
        app->result = result;
    }
    app->stopping = true;
}

/** \brief Count selected stored slots, not disabled wire padding. */
static size_t frame_bytes(const nx_audio_layout_t* layout) {
    if ((layout->container_bits != 8U && layout->container_bits != 16U &&
         layout->container_bits != 32U) ||
        layout->sample_bits == 0U ||
        layout->sample_bits > layout->container_bits || layout->slots == 0U ||
        layout->slots > 16U || layout->active_slots == 0U ||
        ((uint32_t)layout->active_slots >> layout->slots) != 0U) {
        return 0U;
    }
    size_t slots = 0U;
    for (uint16_t mask = layout->active_slots; mask != 0U; mask >>= 1U) {
        slots += mask & 1U;
    }
    return slots * (layout->container_bits / 8U);
}

nx_result_t example_audio_acquisition_start_streams(
    example_audio_acquisition_t* app, const nx_audio_port_t* port,
    example_audio_playback_t* playback, nx_stream_t* stream, size_t block_bytes,
    size_t block_limit, nx_time_us_t now, uint64_t duration_us) {
    if (!example_acquisition_span_valid(
            app, sizeof(*app), _Alignof(example_audio_acquisition_t)) ||
        !example_acquisition_span_valid(port, sizeof(*port),
                                        _Alignof(nx_audio_port_t)) ||
        example_acquisition_overlap(app, sizeof(*app), port, sizeof(*port)) ||
        (playback != NULL &&
         !example_acquisition_span_valid(playback, sizeof(*playback),
                                         _Alignof(example_audio_playback_t)))) {
        return NX_ERROR_INVALID;
    }
    if (!example_acquisition_span_valid(port->ops, sizeof(*port->ops),
                                        _Alignof(nx_audio_ops_t))) {
        return NX_ERROR_INVALID;
    }
    example_acquisition_span_t controls[4] = {
        {app, sizeof(*app), true},
        {port, sizeof(*port), false},
        {port->ops, sizeof(*port->ops), false}};
    size_t control_count = 3U;
    if (playback != NULL) {
        controls[control_count++] =
            (example_acquisition_span_t){playback, sizeof(*playback), true};
    }
    if (!example_acquisition_controls_valid(controls, control_count)) {
        return NX_ERROR_INVALID;
    }
    nx_stream_t* tx = playback == NULL ? NULL : playback->stream;
    if (block_limit == 0U || (stream == NULL && tx == NULL) ||
        (playback != NULL && tx == NULL) || (stream != NULL && stream == tx)) {
        return NX_ERROR_INVALID;
    }
    if (!example_acquisition_storage_valid(controls, control_count,
                                           port->context, stream, tx)) {
        return NX_ERROR_INVALID;
    }
    if (app->active || (stream != NULL && stream->outstanding != 0U) ||
        (playback != NULL && (playback->has_fill || playback->has_pending))) {
        return NX_ERROR_BUSY;
    }
    if ((stream != NULL && stream->stopping) || (tx != NULL && tx->stopping)) {
        return NX_ERROR_STATE;
    }
    nx_time_us_t deadline;
    nx_result_t result = nx_deadline_finite_after(now, duration_us, &deadline);
    if (result != NX_SUCCESS) {
        return result;
    }
    nx_audio_layout_t layout = {0};
    result = nx_audio_port_layout(port, &layout);
    if (result != NX_SUCCESS) {
        return result;
    }
    size_t bytes = frame_bytes(&layout);
    if (bytes == 0U || block_bytes == 0U || block_bytes % bytes != 0U) {
        return NX_ERROR_INVALID;
    }
    if (tx != NULL) {
        /* Every initial slot is READY. A later free reservation therefore
         * observes a provider-detached block, never unused startup capacity.
         */
        for (size_t index = 0U; index < tx->count; ++index) {
            const nx_stream_slot_t* slot = &tx->slots[index];
            if (slot->state != NX_STREAM_SLOT_READY || slot->data == NULL ||
                slot->length != block_bytes || slot->capacity < block_bytes ||
                (slot->flags &
                 (NX_STREAM_BOUNDARY_ERROR | NX_STREAM_BOUNDARY_LOSS)) != 0U) {
                return NX_ERROR_STATE;
            }
        }
    }
    if (!app->initialized) {
        nx_request_initialize(&app->request.base);
        app->initialized = true;
    }
    result = nx_request_prepare(&app->request.base, deadline);
    if (result != NX_SUCCESS) {
        return result;
    }
    app->request.tx = tx;
    app->request.rx = stream;
    app->request.block_bytes = block_bytes;
    result = nx_audio_port_start_blocks(port, &app->request);
    if (result != NX_SUCCESS) {
        return result;
    }
    app->port = port;
    app->stream = stream;
    app->playback = playback;
    if (playback != NULL) {
        playback->blocks = 0U;
    }
    app->deadline = deadline;
    app->last_now = now;
    app->layout = layout;
    app->frame_bytes = bytes;
    app->block_bytes = block_bytes;
    app->block_limit = block_limit;
    app->blocks = 0U;
    app->frames = 0U;
    app->result = NX_SUCCESS;
    app->active = true;
    app->stopping = false;
    app->source_stopped = false;
    app->has_pending = false;
    memset(app->first_container, 0, sizeof(app->first_container));
    return NX_SUCCESS;
}

nx_result_t example_audio_acquisition_start(
    example_audio_acquisition_t* app, const nx_audio_port_t* port,
    nx_stream_t* stream, size_t block_bytes, size_t block_limit,
    nx_time_us_t now, uint64_t duration_us) {
    return example_audio_acquisition_start_streams(
        app, port, NULL, stream, block_bytes, block_limit, now, duration_us);
}

/** \brief Explicitly replay one detached pattern without touching DMA loans. */
static nx_result_t replay(example_audio_acquisition_t* app) {
    example_audio_playback_t* playback = app->playback;
    if (playback == NULL || app->stopping ||
        !nx_stream_can_reserve(playback->stream)) {
        return NX_SUCCESS;
    }
    nx_result_t result = nx_stream_reserve(playback->stream, &playback->fill);
    if (result != NX_SUCCESS) {
        fail(app, result);
        return result;
    }
    playback->has_fill = true;
    if (playback->blocks == SIZE_MAX) {
        fail(app, NX_ERROR_EXHAUSTED);
    } else {
        ++playback->blocks;
        if (app->stream == NULL) {
            size_t frames = app->block_bytes / app->frame_bytes;
            if (app->blocks == SIZE_MAX || frames > SIZE_MAX - app->frames) {
                fail(app, NX_ERROR_EXHAUSTED);
            } else {
                ++app->blocks;
                app->frames += frames;
                memcpy(app->first_container, playback->fill.data,
                       app->layout.container_bits / 8U);
                app->stopping = app->blocks == app->block_limit;
            }
        }
    }
    /* The fresh producer loan has never been handed to the audio provider.
     * Abort can therefore prove its own memory quiescence independently.
     */
    result = app->stopping
                 ? nx_stream_abort(playback->stream, &playback->fill, true)
                 : nx_stream_publish(playback->stream, &playback->fill,
                                     app->block_bytes, 0U);
    if (result == NX_SUCCESS) {
        playback->has_fill = false;
    } else {
        fail(app, result);
    }
    return result;
}

/** \brief Consume at most one complete RX block or retry its exact release. */
static nx_result_t consume(example_audio_acquisition_t* app) {
    if (app->stream == NULL) {
        return NX_SUCCESS;
    }
    if (!app->has_pending) {
        nx_result_t result = nx_stream_acquire(app->stream, &app->pending);
        if (result == NX_ERROR_EMPTY) {
            return NX_SUCCESS;
        }
        if (result != NX_SUCCESS) {
            fail(app, result);
            return result;
        }
        app->has_pending = true;
        if (!app->stopping) {
            const nx_stream_block_t* block = &app->pending;
            size_t frames = block->length / app->frame_bytes;
            if (block->data == NULL || block->length != app->block_bytes ||
                (block->flags &
                 (NX_STREAM_BOUNDARY_ERROR | NX_STREAM_BOUNDARY_LOSS)) != 0U) {
                fail(app, NX_ERROR_IO);
            } else if (app->blocks == SIZE_MAX ||
                       frames > SIZE_MAX - app->frames) {
                fail(app, NX_ERROR_EXHAUSTED);
            } else {
                memcpy(app->first_container, block->data,
                       app->layout.container_bits / 8U);
                ++app->blocks;
                app->frames += frames;
                app->stopping = app->blocks == app->block_limit;
            }
        }
    }
    nx_result_t result = nx_stream_release(app->stream, &app->pending);
    if (result == NX_SUCCESS) {
        app->has_pending = false;
    } else {
        fail(app, result);
    }
    return result;
}

/** \brief Discard at most one unplayed TX block after the owner has joined. */
static nx_result_t drain_playback(example_audio_acquisition_t* app) {
    example_audio_playback_t* playback = app->playback;
    if (playback == NULL) {
        return NX_SUCCESS;
    }
    if (playback->has_fill) {
        nx_result_t result =
            nx_stream_abort(playback->stream, &playback->fill, true);
        if (result != NX_SUCCESS) {
            fail(app, result);
            return result;
        }
        playback->has_fill = false;
    }
    if (!playback->has_pending) {
        nx_result_t result =
            nx_stream_acquire(playback->stream, &playback->pending);
        if (result == NX_ERROR_EMPTY) {
            return NX_SUCCESS;
        }
        if (result != NX_SUCCESS) {
            fail(app, result);
            return result;
        }
        playback->has_pending = true;
    }
    nx_result_t result =
        nx_stream_release(playback->stream, &playback->pending);
    if (result == NX_SUCCESS) {
        playback->has_pending = false;
    } else {
        fail(app, result);
    }
    return result;
}

/** \brief Observe provider settlement without inventing a new result. */
static void observe_settled(example_audio_acquisition_t* app) {
    nx_result_t result;
    size_t transferred;
    if (nx_request_result(&app->request.base, &result, &transferred) !=
        NX_SUCCESS) {
        return;
    }
    if (result != NX_SUCCESS &&
        !(app->stopping && result == NX_ERROR_CANCELLED)) {
        fail(app, result);
    } else if (!app->stopping && app->blocks < app->block_limit) {
        fail(app, NX_ERROR_IO);
    }
    app->stopping = true;
}

nx_result_t example_audio_acquisition_step(example_audio_acquisition_t* app,
                                           nx_time_us_t now) {
    if (app == NULL || app->port == NULL ||
        (app->stream == NULL && app->playback == NULL) ||
        now == NX_DEADLINE_NEVER || now < app->last_now) {
        return NX_ERROR_INVALID;
    }
    if (!app->active) {
        return app->source_stopped ? app->result : NX_ERROR_STATE;
    }
    app->last_now = now;
    if (!app->stopping && nx_deadline_expired(app->deadline, now)) {
        fail(app, NX_ERROR_TIMEOUT);
    }
    if (!app->source_stopped) {
        nx_result_t result = nx_audio_port_service(app->port);
        if (result != NX_SUCCESS && result != NX_ERROR_BUSY) {
            fail(app, result);
        }
        if (nx_request_state(&app->request.base) == NX_REQUEST_QUARANTINED) {
            fail(app, NX_ERROR_IO);
        }
        observe_settled(app);
    }
    nx_result_t progress = replay(app);
    nx_result_t received = consume(app);
    if (progress == NX_SUCCESS) {
        progress = received;
    }
    if (!app->stopping) {
        return NX_ERROR_BUSY;
    }
    if (!app->source_stopped) {
        nx_result_t result = nx_audio_port_stop(app->port);
        if (result != NX_SUCCESS) {
            if (result != NX_ERROR_BUSY) {
                fail(app, result);
            }
            return result;
        }
        if (nx_request_state(&app->request.base) != NX_REQUEST_SETTLED) {
            return NX_ERROR_BUSY;
        }
        observe_settled(app);
        app->source_stopped = true;
    }
    nx_result_t drained = drain_playback(app);
    nx_result_t joined =
        app->stream == NULL ? NX_SUCCESS : nx_stream_stop(app->stream);
    nx_result_t tx_joined = app->playback == NULL
                                ? NX_SUCCESS
                                : nx_stream_stop(app->playback->stream);
    if (progress != NX_SUCCESS) {
        return progress;
    }
    if (drained != NX_SUCCESS) {
        return drained;
    }
    if (joined != NX_SUCCESS) {
        return joined;
    }
    if (tx_joined != NX_SUCCESS) {
        return tx_joined;
    }
    app->active = false;
    return app->result;
}

nx_result_t example_audio_acquisition_cancel(example_audio_acquisition_t* app) {
    if (app == NULL || !app->active) {
        return NX_ERROR_STATE;
    }
    fail(app, NX_ERROR_CANCELLED);
    return NX_SUCCESS;
}
