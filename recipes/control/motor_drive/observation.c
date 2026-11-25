/**
 * \file            observation.c
 * \brief           Bounded single-producer publication and Task consumption
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-24
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "observation.h"
#include "nexus/arch/atomic.h"

_Static_assert(__GCC_ATOMIC_INT_LOCK_FREE == 2 &&
                   sizeof(uint32_t) == sizeof(unsigned),
               "Drive observation requires lock-free 32-bit publication");

void example_motor_observation_publish(
    example_motor_observations_t* observations,
    const nx_power_stage_sample_t* sample, const nx_power_stage_plan_t* plan,
    nx_result_t result) {
    const uint32_t head = nx_atomic_u32_load_relaxed(&observations->published);
    const uint32_t tail = nx_atomic_u32_load_acquire(&observations->consumed);
    if ((uint32_t)(head - tail) == EXAMPLE_MOTOR_OBSERVATION_DEPTH) {
        const uint32_t lost =
            nx_atomic_u32_load_relaxed(&observations->dropped);
        if (lost != UINT32_MAX) {
            nx_atomic_u32_store_release(&observations->dropped, lost + 1U);
        }
        return;
    }
    example_motor_observation_t record = {0};
    record.sample = *sample;
    record.sample.dc = NULL;
#if defined(EXAMPLE_MOTOR_DC_OBSERVATION)
    if (sample->dc != NULL) {
        record.dc_capture = *sample->dc;
        record.dc_capture_valid = true;
    }
#endif
    if (result == NX_SUCCESS) {
        record.plan = *plan;
        record.plan.dc = NULL;
#if defined(EXAMPLE_MOTOR_DC_OBSERVATION)
        if (plan->dc != NULL) {
            record.dc_plan = *plan->dc;
            record.dc_plan_valid = true;
        }
#endif
    }
    record.result = result;
    observations->records[head % EXAMPLE_MOTOR_OBSERVATION_DEPTH] = record;
    nx_atomic_u32_store_release(&observations->published, head + 1U);
}

nx_result_t
example_motor_observation_drain(example_motor_observations_t* observations,
                                example_motor_observation_sink_t sink,
                                void* context, uint32_t budget,
                                uint32_t* delivered, uint32_t* dropped) {
    if (observations == NULL || sink == NULL || budget == 0 ||
        delivered == NULL || dropped == NULL) {
        return NX_ERROR_INVALID;
    }
    *delivered = 0;
    nx_result_t status = NX_SUCCESS;
    while (*delivered < budget) {
        const uint32_t tail =
            nx_atomic_u32_load_relaxed(&observations->consumed);
        const uint32_t head =
            nx_atomic_u32_load_acquire(&observations->published);
        if (head == tail) {
            status = NX_ERROR_EMPTY;
            break;
        }
        const example_motor_observation_t record =
            observations->records[tail % EXAMPLE_MOTOR_OBSERVATION_DEPTH];
        status = sink(context, &record);
        if (status != NX_SUCCESS) {
            break;
        }
        nx_atomic_u32_store_release(&observations->consumed, tail + 1U);
        ++*delivered;
    }
    *dropped = nx_atomic_u32_load_acquire(&observations->dropped);
    return status;
}
