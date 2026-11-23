/**
 * \file            acquisition_storage.h
 * \brief           Cold checks for application-owned stream reservations
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-23
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#ifndef EXAMPLE_ACQUISITION_STORAGE_H
#define EXAMPLE_ACQUISITION_STORAGE_H

#include "nexus/io/stream.h"

/** \brief Known storage only; opaque provider context has no public size. */
typedef struct {
    const void* data;
    size_t bytes;
    bool writable;
} example_acquisition_span_t;

/** \brief Reject null, misalignment and wrapping before inspecting storage. */
static inline bool example_acquisition_span_valid(const void* data,
                                                  size_t bytes,
                                                  size_t alignment) {
    uintptr_t first = (uintptr_t)data;
    return data != NULL && bytes != 0U && alignment != 0U &&
           (alignment & (alignment - 1U)) == 0U && first % alignment == 0U &&
           bytes <= UINTPTR_MAX - first;
}

/** \brief Compare half-open ranges after both complete spans were checked. */
static inline bool example_acquisition_overlap(const void* left,
                                               size_t left_bytes,
                                               const void* right,
                                               size_t right_bytes) {
    uintptr_t a = (uintptr_t)left;
    uintptr_t b = (uintptr_t)right;
    return a < b + right_bytes && b < a + left_bytes;
}

/** \brief Writable controls cannot cover another known control or interface. */
static inline bool
example_acquisition_controls_valid(const example_acquisition_span_t* controls,
                                   size_t count) {
    for (size_t i = 0U; i < count; ++i) {
        if (!example_acquisition_span_valid(controls[i].data, controls[i].bytes,
                                            1U)) {
            return false;
        }
        for (size_t j = 0U; j < i; ++j) {
            if ((controls[i].writable || controls[j].writable) &&
                example_acquisition_overlap(controls[i].data, controls[i].bytes,
                                            controls[j].data,
                                            controls[j].bytes)) {
                return false;
            }
        }
    }
    return true;
}

/** \brief Validate known controls and both pools without touching payloads.
 * \note Cold single-executor check only. Initial stream/slot objects must be
 *       genuine initialized objects. Their declared extents cannot prove
 *       arbitrary pointers mapped. Context start must be independent of
 *       writable owned storage; its unknown full extent remains the provider
 *       admission and caller contract. Read-only face/ops may share storage.
 */
static inline bool
example_acquisition_storage_valid(const example_acquisition_span_t* fixed,
                                  size_t fixed_count, const void* context,
                                  nx_stream_t* receive, nx_stream_t* transmit) {
    example_acquisition_span_t controls[8];
    nx_stream_t* streams[2] = {receive, transmit};
    if (fixed_count > 4U ||
        !example_acquisition_controls_valid(fixed, fixed_count) ||
        !example_acquisition_span_valid(context, 1U, 1U)) {
        return false;
    }
    size_t count = fixed_count;
    for (size_t i = 0U; i < count; ++i) {
        controls[i] = fixed[i];
    }
    for (size_t direction = 0U; direction < 2U; ++direction) {
        nx_stream_t* stream = streams[direction];
        if (stream == NULL) {
            continue;
        }
        if (!example_acquisition_span_valid(stream, sizeof(*stream),
                                            _Alignof(nx_stream_t))) {
            return false;
        }
        controls[count++] =
            (example_acquisition_span_t){stream, sizeof(*stream), true};
        if (!example_acquisition_controls_valid(controls, count)) {
            return false;
        }
        if (stream->count == 0U ||
            stream->count > SIZE_MAX / sizeof(nx_stream_slot_t) ||
            !example_acquisition_span_valid(
                stream->slots, stream->count * sizeof(nx_stream_slot_t),
                _Alignof(nx_stream_slot_t))) {
            return false;
        }
        controls[count++] = (example_acquisition_span_t){
            stream->slots, stream->count * sizeof(nx_stream_slot_t), true};
        if (!example_acquisition_controls_valid(controls, count)) {
            return false;
        }
    }
    for (size_t i = 0U; i < count; ++i) {
        if (controls[i].writable &&
            example_acquisition_overlap(context, 1U, controls[i].data,
                                        controls[i].bytes)) {
            return false;
        }
    }
    for (size_t direction = 0U; direction < 2U; ++direction) {
        nx_stream_t* stream = streams[direction];
        if (stream == NULL) {
            continue;
        }
        for (size_t i = 0U; i < stream->count; ++i) {
            const nx_stream_slot_t* slot = &stream->slots[i];
            if (!example_acquisition_span_valid(slot->data, slot->capacity,
                                                1U) ||
                example_acquisition_overlap(slot->data, slot->capacity, context,
                                            1U)) {
                return false;
            }
            for (size_t j = 0U; j < count; ++j) {
                if (example_acquisition_overlap(slot->data, slot->capacity,
                                                controls[j].data,
                                                controls[j].bytes)) {
                    return false;
                }
            }
            for (size_t previous = 0U; previous <= direction; ++previous) {
                const nx_stream_t* other = streams[previous];
                if (other == NULL) {
                    continue;
                }
                size_t end = previous == direction ? i : other->count;
                for (size_t j = 0U; j < end; ++j) {
                    const nx_stream_slot_t* old = &other->slots[j];
                    if (example_acquisition_overlap(slot->data, slot->capacity,
                                                    old->data, old->capacity)) {
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

#endif
