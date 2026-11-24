/**
 * \file            bus.c
 * \brief           Bounded copied DC-bus observation handoff
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-24
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "bus.h"
#include "nexus/arch/atomic.h"
#include <math.h>

/** \brief Known caller-owned spans must not alias channel metadata. */
static bool overlap(const void* a, size_t an, const void* b, size_t bn) {
    const uintptr_t first = (uintptr_t)a;
    const uintptr_t second = (uintptr_t)b;
    return an > UINTPTR_MAX - first || bn > UINTPTR_MAX - second ||
           (first < second + bn && second < first + an);
}

nx_result_t example_bus_queue_initialize(example_bus_queue_t* queue,
                                         uint32_t clock_domain, uint64_t epoch,
                                         uint32_t source_id) {
    if (queue == NULL ||
        (uintptr_t)queue % _Alignof(example_bus_queue_t) != 0U ||
        clock_domain == 0U || epoch == 0U || source_id == 0U) {
        return NX_ERROR_INVALID;
    }
    const example_bus_queue_t next = {.epoch = epoch,
                                      .clock_domain = clock_domain,
                                      .source_id = source_id,
                                      .initialized = true};
    *queue = next;
    return NX_SUCCESS;
}

nx_result_t example_bus_fault(example_bus_queue_t* queue, nx_result_t reason) {
    if (queue == NULL || !queue->initialized || reason <= NX_SUCCESS ||
        reason > NX_ERROR_CONTEXT) {
        return NX_ERROR_INVALID;
    }
    uint32_t expected = NX_SUCCESS;
    (void)nx_atomic_u32_compare_exchange_acq_rel(&queue->fault, &expected,
                                                 (uint32_t)reason);
    return (nx_result_t)nx_atomic_u32_load_acquire(&queue->fault);
}

nx_result_t example_bus_publish(example_bus_queue_t* queue,
                                const example_bus_sample_t* sample) {
    if (queue == NULL || sample == NULL || !queue->initialized ||
        overlap(queue, sizeof(*queue), sample, sizeof(*sample)) ||
        sample->clock_domain != queue->clock_domain ||
        sample->epoch != queue->epoch ||
        sample->source_id != queue->source_id || sample->sequence == 0U ||
        sample->sequence <= queue->last_sequence ||
        sample->observed_at == NX_DEADLINE_NEVER ||
        sample->valid_until == NX_DEADLINE_NEVER ||
        sample->sampled_at > sample->observed_at ||
        sample->observed_at >= sample->valid_until ||
        (queue->last_sequence != 0U &&
         (sample->sampled_at <= queue->last_published_at ||
          sample->observed_at <= queue->last_observed_at)) ||
        !isfinite(sample->voltage_v) || sample->voltage_v <= 0.0F) {
        return NX_ERROR_INVALID;
    }
    const uint32_t fault = nx_atomic_u32_load_acquire(&queue->fault);
    if (fault != NX_SUCCESS) {
        return (nx_result_t)fault;
    }
    const uint32_t produced = nx_atomic_u32_load_acquire(&queue->produced);
    const uint32_t consumed = nx_atomic_u32_load_acquire(&queue->consumed);
    if (produced - consumed > 2U) {
        return example_bus_fault(queue, NX_ERROR_STATE);
    }
    if (produced - consumed == 2U) {
        return NX_ERROR_BUSY;
    }
    queue->slots[produced & 1U] = *sample;
    queue->last_sequence = sample->sequence;
    queue->last_published_at = sample->sampled_at;
    queue->last_observed_at = sample->observed_at;
    nx_atomic_u32_store_release(&queue->produced, produced + 1U);
    return NX_SUCCESS;
}

nx_result_t example_bus_take(example_bus_queue_t* queue, nx_time_us_t now,
                             uint32_t clock_domain, uint64_t epoch,
                             uint32_t source_id, example_bus_sample_t* sample) {
    if (queue == NULL || sample == NULL || !queue->initialized ||
        overlap(queue, sizeof(*queue), sample, sizeof(*sample)) ||
        now == NX_DEADLINE_NEVER) {
        return NX_ERROR_INVALID;
    }
    const uint32_t fault = nx_atomic_u32_load_acquire(&queue->fault);
    if (fault != NX_SUCCESS) {
        return (nx_result_t)fault;
    }
    if (clock_domain != queue->clock_domain || epoch != queue->epoch ||
        source_id != queue->source_id || now < queue->last_taken_at) {
        return example_bus_fault(queue, NX_ERROR_STATE);
    }
    const uint32_t consumed = nx_atomic_u32_load_acquire(&queue->consumed);
    const uint32_t produced = nx_atomic_u32_load_acquire(&queue->produced);
    if (produced - consumed > 2U) {
        return example_bus_fault(queue, NX_ERROR_STATE);
    }
    if (produced != consumed) {
        queue->cached = queue->slots[consumed & 1U];
        queue->have_cached = true;
        nx_atomic_u32_store_release(&queue->consumed, consumed + 1U);
    }
    queue->last_taken_at = now;
    if (!queue->have_cached) {
        return NX_ERROR_EMPTY;
    }
    if (now < queue->cached.observed_at) {
        return example_bus_fault(queue, NX_ERROR_STATE);
    }
    if (now >= queue->cached.valid_until) {
        return example_bus_fault(queue, NX_ERROR_TIMEOUT);
    }
    const uint32_t final_fault = nx_atomic_u32_load_acquire(&queue->fault);
    if (final_fault != NX_SUCCESS) {
        return (nx_result_t)final_fault;
    }
    *sample = queue->cached;
    return NX_SUCCESS;
}
