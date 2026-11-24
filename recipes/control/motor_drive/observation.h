/**
 * \file            observation.h
 * \brief           Fixed four-record IRQ-to-Task drive observation channel
 * \author          Nexus Team
 */
#ifndef EXAMPLE_MOTOR_DRIVE_OBSERVATION_H
#define EXAMPLE_MOTOR_DRIVE_OBSERVATION_H
#include "nexus/io/power_stage.h"
#ifdef __cplusplus
extern "C" {
#endif
#define EXAMPLE_MOTOR_OBSERVATION_DEPTH 4U
/** \brief Source facts and next plan; never a physical application receipt. */
typedef struct {
    nx_power_stage_sample_t sample;
    nx_power_stage_plan_t plan;
    nx_result_t result;
} example_motor_observation_t;
/** \brief One IRQ producer, one Task consumer; zero before publication.
 * \note Naturally aligned words use public acquire/release atomics. Never
 *       reset until the provider is SETTLED and the consumer has drained and
 *       joined. Unsigned head/tail wrap is intentional: distance never exceeds
 *       four. Drop count saturates, so UINT32_MAX means at least that many.
 */
typedef struct {
    example_motor_observation_t records[EXAMPLE_MOTOR_OBSERVATION_DEPTH];
    uint32_t published;
    uint32_t consumed;
    uint32_t dropped;
} example_motor_observations_t;
/** \brief IRQ-only bounded copy; full drops this record without blocking.
 * \param[in,out] observations: Separate stable SPSC channel, not borrowed
 *                              algorithm context.
 * \param[in] sample: Callback-local source facts copied before return.
 * \param[in] plan: Complete plan on SUCCESS; otherwise NULL.
 * \param[in] result: Computation outcome; failure has no valid plan.
 * \note No I/O, retries, logging or allocation; producer never overwrites.
 */
void example_motor_observation_publish(
    example_motor_observations_t* observations,
    const nx_power_stage_sample_t* sample, const nx_power_stage_plan_t* plan,
    nx_result_t result);
/** \brief Task sink atomically accepts one complete record before return.
 * \note SUCCESS means copied/consumed completely. BUSY or any error retains
 *       the record for retry. A byte-stream adapter owns its partial-write
 *       offset; it must not report SUCCESS on a short write. Never retain the
 *       callback-local record pointer. No sink runs in the control IRQ.
 */
typedef nx_result_t (*example_motor_observation_sink_t)(
    void* context, const example_motor_observation_t* record);
/** \brief Drain at most budget records on the single consumer Task.
 * \param[in,out] observations: Live separate channel; safe during operation.
 * \param[in] sink: Whole-record Task sink, with no recursive drain/reset.
 * \param[in,out] context: Sink-owned storage, independent of borrowed context.
 * \param[in] budget: Positive maximum sink calls, never an unbounded loop.
 * \param[out] delivered: Records accepted during this call, even on BUSY.
 * \param[out] dropped: Cumulative saturated producer loss, even when empty.
 * \return SUCCESS at budget, EMPTY when drained, or exact sink error.
 * \note Sink/context/outputs are disjoint from the complete channel. Task
 *       never reads callback context. Final EMPTY is stable only after
 *       provider SETTLED and external consumer join.
 */
nx_result_t
example_motor_observation_drain(example_motor_observations_t* observations,
                                example_motor_observation_sink_t sink,
                                void* context, uint32_t budget,
                                uint32_t* delivered, uint32_t* dropped);
#ifdef __cplusplus
}
#endif
#endif
