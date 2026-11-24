/**
 * \file            rotor.c
 * \brief           Explicit absolute-angle observation and Task-to-IRQ handoff
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-24
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "rotor.h"
#include "nexus/arch/atomic.h"
#include <math.h>

/** \brief Known metadata spans are disjoint; opaque users remain caller-owned.
 */
static bool overlap(const void* a, size_t an, const void* b, size_t bn) {
    const uintptr_t first = (uintptr_t)a;
    const uintptr_t second = (uintptr_t)b;
    return an > UINTPTR_MAX - first || bn > UINTPTR_MAX - second ||
           (first < second + bn && second < first + an);
}

nx_result_t example_rotor_queue_initialize(example_rotor_queue_t* queue,
                                           uint32_t clock_domain,
                                           uint64_t epoch,
                                           uint32_t maximum_age_us) {
    if (queue == NULL ||
        (uintptr_t)queue % _Alignof(example_rotor_queue_t) != 0U ||
        clock_domain == 0U || epoch == 0U || maximum_age_us == 0U) {
        return NX_ERROR_INVALID;
    }
    const example_rotor_queue_t next = {.epoch = epoch,
                                        .clock_domain = clock_domain,
                                        .maximum_age_us = maximum_age_us,
                                        .initialized = true};
    *queue = next;
    return NX_SUCCESS;
}

nx_result_t example_rotor_fault(example_rotor_queue_t* queue,
                                nx_result_t reason) {
    if (queue == NULL || !queue->initialized || reason <= NX_SUCCESS ||
        reason > NX_ERROR_CONTEXT) {
        return NX_ERROR_INVALID;
    }
    uint32_t expected = NX_SUCCESS;
    (void)nx_atomic_u32_compare_exchange_acq_rel(&queue->fault, &expected,
                                                 (uint32_t)reason);
    return (nx_result_t)nx_atomic_u32_load_acquire(&queue->fault);
}

nx_result_t example_rotor_publish(example_rotor_queue_t* queue,
                                  const example_rotor_sample_t* sample) {
    if (queue == NULL || sample == NULL || !queue->initialized ||
        overlap(queue, sizeof(*queue), sample, sizeof(*sample)) ||
        sample->clock_domain != queue->clock_domain ||
        sample->epoch != queue->epoch || sample->sequence == 0U ||
        sample->sequence <= queue->last_sequence ||
        sample->angle_at == NX_DEADLINE_NEVER ||
        (queue->last_sequence != 0U &&
         sample->angle_at <= queue->last_published_at) ||
        !isfinite(sample->mechanical_angle_rad) ||
        !isfinite(sample->mechanical_position_rad) ||
        !isfinite(sample->mechanical_velocity_rad_s)) {
        return NX_ERROR_INVALID;
    }
    const uint32_t fault = nx_atomic_u32_load_acquire(&queue->fault);
    if (fault != NX_SUCCESS) {
        return (nx_result_t)fault;
    }
    const uint32_t produced = nx_atomic_u32_load_acquire(&queue->produced);
    const uint32_t consumed = nx_atomic_u32_load_acquire(&queue->consumed);
    if (produced - consumed > 2U) {
        return example_rotor_fault(queue, NX_ERROR_STATE);
    }
    if (produced - consumed == 2U) {
        return NX_ERROR_BUSY;
    }
    queue->slots[produced & 1U] = *sample;
    queue->last_sequence = sample->sequence;
    queue->last_published_at = sample->angle_at;
    nx_atomic_u32_store_release(&queue->produced, produced + 1U);
    return NX_SUCCESS;
}

nx_result_t example_rotor_take(example_rotor_queue_t* queue, nx_time_us_t now,
                               uint32_t clock_domain, uint64_t epoch,
                               example_rotor_sample_t* sample) {
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
        now < queue->last_taken_at) {
        return example_rotor_fault(queue, NX_ERROR_STATE);
    }
    const uint32_t consumed = nx_atomic_u32_load_acquire(&queue->consumed);
    const uint32_t produced = nx_atomic_u32_load_acquire(&queue->produced);
    if (produced - consumed > 2U) {
        return example_rotor_fault(queue, NX_ERROR_STATE);
    }
    if (produced != consumed) {
        queue->cached = queue->slots[consumed & 1U];
        queue->have_cached = true;
        nx_atomic_u32_store_release(&queue->consumed, consumed + 1U);
    }
    if (!queue->have_cached) {
        return NX_ERROR_EMPTY;
    }
    if (now < queue->cached.angle_at) {
        return example_rotor_fault(queue, NX_ERROR_STATE);
    }
    if (now - queue->cached.angle_at >= queue->maximum_age_us) {
        return example_rotor_fault(queue, NX_ERROR_TIMEOUT);
    }
    const uint32_t final_fault = nx_atomic_u32_load_acquire(&queue->fault);
    if (final_fault != NX_SUCCESS) {
        return (nx_result_t)final_fault;
    }
    *sample = queue->cached;
    queue->last_taken_at = now;
    return NX_SUCCESS;
}
