/**
 * \file            as5047p_rotor.c
 * \brief           Optional absolute-angle acquisition and explicit source
 * qualification \author          Nexus Team \version         1.0.0 \date
 * 2026-11-24 \copyright       Copyright (c) 2026 Nexus Team
 */
#include "as5047p_rotor.h"
#include "nexus/arch/atomic.h"
#include <math.h>

#define EXAMPLE_ROTOR_TAU 6.28318530717958647692F

nx_result_t example_rotor_reader_initialize(example_rotor_reader_t* reader,
                                            nx_as5047p_t* device,
                                            example_rotor_queue_t* queue,
                                            bool reversed, float zero_rad,
                                            uint32_t maximum_step_counts,
                                            uint32_t maximum_gap_us) {
    if (reader == NULL || device == NULL || queue == NULL ||
        !queue->initialized || !device->initialized || device->uncertain ||
        device->config == NULL || maximum_gap_us == 0U ||
        maximum_step_counts == 0U ||
        maximum_step_counts >= NX_AS5047P_COUNTS_PER_REVOLUTION / 2U ||
        device->config->clock_domain != queue->clock_domain ||
        device->config->clock_epoch != queue->epoch || !isfinite(zero_rad) ||
        fabsf(zero_rad) > EXAMPLE_ROTOR_TAU / 2.0F) {
        return NX_ERROR_INVALID;
    }
    if (nx_atomic_u32_load_acquire(&queue->fault) != NX_SUCCESS ||
        nx_atomic_u32_load_acquire(&queue->produced) != 0U ||
        nx_atomic_u32_load_acquire(&queue->consumed) != 0U ||
        queue->last_sequence != 0U) {
        return NX_ERROR_STATE;
    }
    example_rotor_reader_t next = {.device = device,
                                   .queue = queue,
                                   .maximum_gap_us = maximum_gap_us,
                                   .zero_rad = zero_rad,
                                   .reversed = reversed,
                                   .initialized = true};
    const nx_result_t status =
        nx_encoder_tracker_init(&next.tracker, NX_AS5047P_COUNTS_PER_REVOLUTION,
                                maximum_step_counts, 0);
    if (status != NX_SUCCESS) {
        return status;
    }
    *reader = next;
    return NX_SUCCESS;
}

nx_result_t example_rotor_poll(example_rotor_reader_t* reader,
                               nx_time_us_t deadline) {
    if (reader == NULL || !reader->initialized ||
        deadline == NX_DEADLINE_NEVER) {
        return NX_ERROR_INVALID;
    }
    const uint32_t fault = nx_atomic_u32_load_acquire(&reader->queue->fault);
    if (fault != NX_SUCCESS) {
        return (nx_result_t)fault;
    }
    nx_as5047p_sample_t raw;
    nx_result_t result = nx_as5047p_read(reader->device, &raw, deadline);
    if (result == NX_ERROR_BUSY) {
        return result;
    }
    if (result != NX_SUCCESS) {
        return example_rotor_fault(reader->queue, result);
    }
    const nx_io_timestamp_t* observed = &raw.observed;
    if (observed->domain != reader->queue->clock_domain ||
        observed->epoch != reader->queue->epoch ||
        observed->ticks_per_second != 1000000U ||
        observed->source != NX_IO_TIMESTAMP_SERVICE_OBSERVATION ||
        observed->ticks == NX_DEADLINE_NEVER ||
        (raw.diagnostics & 0x0c00U) != 0U ||
        (reader->anchored && observed->ticks <= reader->last_at)) {
        return example_rotor_fault(reader->queue, NX_ERROR_STATE);
    }
    const nx_time_us_t elapsed =
        reader->anchored ? observed->ticks - reader->last_at : 1U;
    if (elapsed > reader->maximum_gap_us) {
        return example_rotor_fault(reader->queue, NX_ERROR_TIMEOUT);
    }
    const float scale =
        (reader->reversed ? -EXAMPLE_ROTOR_TAU : EXAMPLE_ROTOR_TAU) /
        (float)NX_AS5047P_COUNTS_PER_REVOLUTION;
    nx_encoder_estimate_t estimate;
    result = nx_encoder_tracker_step(&reader->tracker, raw.raw_angle, scale,
                                     (float)elapsed * 0.000001F, &estimate);
    if (result != NX_SUCCESS) {
        return example_rotor_fault(reader->queue, result);
    }
    reader->last_at = observed->ticks;
    reader->anchored = true;
    if (!estimate.velocity_valid) {
        return NX_ERROR_BUSY;
    }
    /* A bounded local mechanical range is explicit. Long-lived machines keep
     * integer position and establish another origin before float resolution
     * leaves the selected application error budget. Integer position remains
     * authoritative; electrical angle uses the independent raw modulo. */
    if (estimate.position_counts < -INT64_C(16777216) ||
        estimate.position_counts > INT64_C(16777216) ||
        reader->sequence == UINT32_MAX) {
        return example_rotor_fault(reader->queue, NX_ERROR_EXHAUSTED);
    }
    float angle;
    result = nx_angle_wrap_f32(reader->zero_rad + (float)raw.raw_angle * scale,
                               &angle);
    if (result != NX_SUCCESS) {
        return example_rotor_fault(reader->queue, result);
    }
    const example_rotor_sample_t sample = {
        .angle_at = observed->ticks,
        .epoch = observed->epoch,
        .position_counts = reader->reversed ? -estimate.position_counts
                                            : estimate.position_counts,
        .mechanical_angle_rad = angle,
        .mechanical_position_rad = (float)estimate.position_counts * scale,
        .mechanical_velocity_rad_s = estimate.velocity,
        .clock_domain = observed->domain,
        .sequence = ++reader->sequence};
    return example_rotor_publish(reader->queue, &sample);
}
