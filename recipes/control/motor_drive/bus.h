/**
 * \file            bus.h
 * \brief           Qualified Task-to-fast-loop DC-bus observations
 * \author          Nexus Team
 */
#ifndef EXAMPLE_MOTOR_BUS_H
#define EXAMPLE_MOTOR_BUS_H

#include "nexus/core/time.h"

#ifdef __cplusplus
extern "C" {
#endif

/** \brief One copied physical-voltage observation with original authority.
 * \note sampled_at preserves the qualified source time. For a sequential
 *       monitor, a conservative conversion-start lower bound is permitted;
 *       it must not be relabeled as a measured aperture. observed_at is when
 *       the Task obtained the complete value. Expiry is never extended by
 *       publication, caching or consumption. Source identity is local and
 *       caller-qualified, not authentication.
 */
typedef struct {
    nx_time_us_t sampled_at;
    nx_time_us_t observed_at;
    nx_time_us_t valid_until;
    uint64_t epoch;
    uint32_t clock_domain;
    uint32_t source_id;
    uint32_t sequence;
    float voltage_v;
} example_bus_sample_t;

/** \brief Exactly two caller-owned SPSC slots plus a fast-consumer cache.
 * \note Task owns publication history; one fast executor owns the cache and
 *       take history. Only Arch atomic helpers access indices/fault live.
 *       Both owners and every hardware callback join before reinitialization.
 *       A dual-axis application uses separate channels or an explicitly
 *       serialized fanout; two independent consumers cannot share this SPSC.
 */
typedef struct {
    example_bus_sample_t slots[2];
    example_bus_sample_t cached;
    uint64_t epoch;
    nx_time_us_t last_published_at;
    nx_time_us_t last_observed_at;
    nx_time_us_t last_taken_at;
    uint32_t clock_domain;
    uint32_t source_id;
    uint32_t last_sequence;
    uint32_t produced;
    uint32_t consumed;
    uint32_t fault;
    bool initialized;
    bool have_cached;
} example_bus_queue_t;

/** \brief Initialize joined source handoff without hardware or allocation.
 * \param[out] queue: Zeroed or fully joined independent caller storage.
 * \param[in] clock_domain: Nonzero local monotonic microsecond domain.
 * \param[in] epoch: Nonzero complete epoch, immutable while users run.
 * \param[in] source_id: Nonzero selected measurement-chain identity.
 * \return SUCCESS or INVALID; rejection leaves storage unchanged.
 * \note Task before publishing. No default queue or sensor is allocated.
 */
nx_result_t example_bus_queue_initialize(example_bus_queue_t* queue,
                                         uint32_t clock_domain, uint64_t epoch,
                                         uint32_t source_id);

/** \brief Publish one complete newer measurement without replacing a slot.
 * \param[in,out] queue: Same sole Task producer of this selected source.
 * \param[in] sample: Separate complete same-identity finite voltage record.
 * \return SUCCESS, BUSY, INVALID or retained fault; rejection publishes none.
 * \note Task only. Full returns BUSY without refreshing any old measurement.
 *       Sequence increases without wrap; both measurement and observation
 *       times retain their original meaning and must advance consistently.
 */
nx_result_t example_bus_publish(example_bus_queue_t* queue,
                                const example_bus_sample_t* sample);

/** \brief Latch source failure independently of queue capacity.
 * \param[in,out] queue: Stable initialized channel shared with its fast owner.
 * \param[in] reason: Non-success result retained until joined reset.
 * \return Retained first reason, or INVALID for invalid arguments.
 * \note Task/IRQ atomic publication. It never stops hardware or performs I/O.
 */
nx_result_t example_bus_fault(example_bus_queue_t* queue, nx_result_t reason);

/** \brief Take at most one record or reuse only a still-qualified cached one.
 * \param[in,out] queue: Same sole fast consumer; no device call occurs.
 * \param[in] now: Finite monotonic current observation time.
 * \param[in] clock_domain: Exact current domain, not an implicit conversion.
 * \param[in] epoch: Exact complete current epoch.
 * \param[in] source_id: Exact expected measurement-chain identity.
 * \param[out] sample: Separate output, unchanged on EMPTY or any error.
 * \return SUCCESS, EMPTY, TIMEOUT, STATE or exact retained source failure.
 * \note IRQ/Task serialized fast owner. Equality with valid_until is expired.
 *       Future observations, clock discontinuity and expiry close admission;
 *       cached values cannot revive after a fault. The drive separately
 *       requires validity through its planned output boundary.
 */
nx_result_t example_bus_take(example_bus_queue_t* queue, nx_time_us_t now,
                             uint32_t clock_domain, uint64_t epoch,
                             uint32_t source_id, example_bus_sample_t* sample);

#ifdef __cplusplus
}
#endif
#endif /* EXAMPLE_MOTOR_BUS_H */
