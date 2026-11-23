/**
 * \file            adc_acquisition.c
 * \brief           Bounded ADC block consumption with explicit source join
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-23
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "adc_acquisition.h"
#include "acquisition_storage.h"
#include <string.h>

/** \brief Keep the first operation failure while cleanup remains retryable. */
static void fail(example_adc_acquisition_t* app, nx_result_t result) {
    if (app->result == NX_SUCCESS) {
        app->result = result;
    }
    app->stopping = true;
}

nx_result_t example_adc_acquisition_start(example_adc_acquisition_t* app,
                                          const nx_adc_port_t* port,
                                          nx_stream_t* stream,
                                          uint32_t trigger_hz,
                                          size_t block_limit, nx_time_us_t now,
                                          uint64_t duration_us) {
    if (!example_acquisition_span_valid(app, sizeof(*app),
                                        _Alignof(example_adc_acquisition_t)) ||
        !example_acquisition_span_valid(port, sizeof(*port),
                                        _Alignof(nx_adc_port_t)) ||
        example_acquisition_overlap(app, sizeof(*app), port, sizeof(*port)) ||
        stream == NULL || trigger_hz == 0U || block_limit == 0U) {
        return NX_ERROR_INVALID;
    }
    if (!example_acquisition_span_valid(port->ops, sizeof(*port->ops),
                                        _Alignof(nx_adc_ops_t))) {
        return NX_ERROR_INVALID;
    }
    const example_acquisition_span_t controls[] = {
        {app, sizeof(*app), true},
        {port, sizeof(*port), false},
        {port->ops, sizeof(*port->ops), false}};
    if (!example_acquisition_storage_valid(controls, 3U, port->context, stream,
                                           NULL)) {
        return NX_ERROR_INVALID;
    }
    if (app->active || stream->outstanding != 0U) {
        return NX_ERROR_BUSY;
    }
    if (stream->stopping) {
        return NX_ERROR_STATE;
    }
    nx_time_us_t deadline;
    nx_result_t result = nx_deadline_finite_after(now, duration_us, &deadline);
    if (result != NX_SUCCESS) {
        return result;
    }
    nx_adc_info_t info;
    result = nx_adc_port_info(port, &info);
    if (result != NX_SUCCESS) {
        return result;
    }
    if (info.channel_count == 0U ||
        info.channel_count > SIZE_MAX / sizeof(uint16_t)) {
        return NX_ERROR_INVALID;
    }
    result = nx_adc_port_stream_start(port, stream, trigger_hz);
    if (result == NX_SUCCESS) {
        *app = (example_adc_acquisition_t){.port = port,
                                           .stream = stream,
                                           .deadline = deadline,
                                           .last_now = now,
                                           .scan_bytes = info.channel_count *
                                                         sizeof(uint16_t),
                                           .block_limit = block_limit,
                                           .result = NX_SUCCESS,
                                           .active = true};
    }
    return result;
}

/** \brief Process or discard one exact token, retrying failed release first. */
static nx_result_t consume(example_adc_acquisition_t* app) {
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
            size_t scans = block->length / app->scan_bytes;
            if (block->data == NULL || block->length == 0U ||
                block->length % app->scan_bytes != 0U ||
                (block->flags &
                 (NX_STREAM_BOUNDARY_ERROR | NX_STREAM_BOUNDARY_LOSS)) != 0U) {
                fail(app, NX_ERROR_IO);
            } else if (app->blocks == SIZE_MAX ||
                       scans > SIZE_MAX - app->scans) {
                fail(app, NX_ERROR_EXHAUSTED);
            } else {
                memcpy(&app->last_count,
                       block->data + block->length - sizeof(uint16_t),
                       sizeof(app->last_count));
                ++app->blocks;
                app->scans += scans;
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

nx_result_t example_adc_acquisition_step(example_adc_acquisition_t* app,
                                         nx_time_us_t now) {
    if (app == NULL || app->port == NULL || app->stream == NULL ||
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
        nx_result_t result = nx_adc_port_stream_service(app->port);
        if (result != NX_SUCCESS && result != NX_ERROR_BUSY) {
            fail(app, result);
        }
    }
    nx_result_t progress = consume(app);
    if (!app->stopping) {
        return NX_ERROR_BUSY;
    }
    if (!app->source_stopped) {
        nx_result_t result = nx_adc_port_stream_stop(app->port);
        if (result != NX_SUCCESS) {
            if (result != NX_ERROR_BUSY) {
                fail(app, result);
            }
            return result;
        }
        app->source_stopped = true;
    }
    nx_result_t joined = nx_stream_stop(app->stream);
    if (progress != NX_SUCCESS) {
        return progress;
    }
    if (joined != NX_SUCCESS) {
        return joined;
    }
    app->active = false;
    return app->result;
}

nx_result_t example_adc_acquisition_cancel(example_adc_acquisition_t* app) {
    if (app == NULL || !app->active) {
        return NX_ERROR_STATE;
    }
    fail(app, NX_ERROR_CANCELLED);
    return NX_SUCCESS;
}
