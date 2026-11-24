/**
 * \file            bus_ina226.c
 * \brief           Explicit optional INA226 measurement-to-control adapter
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-24
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "bus_ina226.h"
#include "nexus/arch/atomic.h"

nx_result_t example_bus_ina226_initialize(example_bus_ina226_t* reader,
                                          nx_ina226_t* device,
                                          example_bus_queue_t* queue,
                                          uint32_t lifetime_us) {
    if (reader == NULL || device == NULL || queue == NULL ||
        lifetime_us == 0U || !device->initialized || device->uncertain ||
        device->config == NULL || !queue->initialized ||
        device->config->clock_domain != queue->clock_domain ||
        (uint64_t)device->config->clock_epoch != queue->epoch) {
        return NX_ERROR_INVALID;
    }
    if (device->measuring || device->entered || queue->last_sequence != 0U ||
        nx_atomic_u32_load_acquire(&queue->produced) != 0U ||
        nx_atomic_u32_load_acquire(&queue->consumed) != 0U ||
        nx_atomic_u32_load_acquire(&queue->fault) != NX_SUCCESS) {
        return NX_ERROR_STATE;
    }
    const example_bus_ina226_t next = {.device = device,
                                       .queue = queue,
                                       .lifetime_us = lifetime_us,
                                       .initialized = true};
    *reader = next;
    return NX_SUCCESS;
}

nx_result_t example_bus_ina226_begin(example_bus_ina226_t* reader,
                                     nx_time_us_t deadline) {
    if (reader == NULL || !reader->initialized) {
        return NX_ERROR_INVALID;
    }
    const uint32_t fault = nx_atomic_u32_load_acquire(&reader->queue->fault);
    if (fault != NX_SUCCESS) {
        return (nx_result_t)fault;
    }
    const nx_result_t result = nx_ina226_begin(reader->device, deadline);
    return result == NX_SUCCESS || result == NX_ERROR_BUSY
               ? result
               : example_bus_fault(reader->queue, result);
}

nx_result_t example_bus_ina226_poll(example_bus_ina226_t* reader) {
    if (reader == NULL || !reader->initialized) {
        return NX_ERROR_INVALID;
    }
    const uint32_t fault = nx_atomic_u32_load_acquire(&reader->queue->fault);
    if (fault != NX_SUCCESS) {
        return (nx_result_t)fault;
    }
    nx_ina226_sample_t raw;
    nx_result_t result = nx_ina226_poll(reader->device, &raw);
    if (result == NX_ERROR_BUSY) {
        return result;
    }
    if (result != NX_SUCCESS) {
        return example_bus_fault(reader->queue, result);
    }
    if (raw.observed.domain != reader->queue->clock_domain ||
        (uint64_t)raw.observed.epoch != reader->queue->epoch ||
        raw.observed.ticks_per_second != 1000000U ||
        raw.observed.source != NX_IO_TIMESTAMP_SERVICE_OBSERVATION ||
        raw.observed.ticks < raw.started_at || raw.bus_microvolts == 0U) {
        return example_bus_fault(reader->queue, NX_ERROR_STATE);
    }
    nx_time_us_t expiry;
    result =
        nx_deadline_finite_after(raw.started_at, reader->lifetime_us, &expiry);
    if (result != NX_SUCCESS) {
        return example_bus_fault(reader->queue, result);
    }
    if (raw.observed.ticks >= expiry) {
        return example_bus_fault(reader->queue, NX_ERROR_TIMEOUT);
    }
    if (reader->sequence == UINT32_MAX) {
        return example_bus_fault(reader->queue, NX_ERROR_EXHAUSTED);
    }
    const example_bus_sample_t sample = {
        .sampled_at = raw.started_at,
        .observed_at = raw.observed.ticks,
        .valid_until = expiry,
        .epoch = reader->queue->epoch,
        .clock_domain = reader->queue->clock_domain,
        .source_id = reader->queue->source_id,
        .sequence = ++reader->sequence,
        .voltage_v = (float)raw.bus_microvolts * 0.000001F};
    result = example_bus_publish(reader->queue, &sample);
    /* The conversion is consumed even if this new record cannot be queued. */
    return result == NX_SUCCESS || result == NX_ERROR_BUSY
               ? NX_SUCCESS
               : example_bus_fault(reader->queue, result);
}
