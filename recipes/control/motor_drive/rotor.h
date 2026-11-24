/**
 * \file            rotor.h
 * \brief           Bounded Task-to-fast-loop rotor observation exchange
 * \author          Nexus Team
 */
#ifndef EXAMPLE_MOTOR_ROTOR_H
#define EXAMPLE_MOTOR_ROTOR_H

#include "nexus/core/time.h"

#ifdef __cplusplus
extern "C" {
#endif

/** \brief A software observation, never a physical aperture claim.
 * \note position_counts and mechanical_position_rad are signed displacement
 *       from this reader's first observation, not a machine-home assertion.
 */
typedef struct {
    nx_time_us_t angle_at;
    uint64_t epoch;
    int64_t position_counts;
    float mechanical_angle_rad;
    float mechanical_position_rad;
    float mechanical_velocity_rad_s;
    uint32_t clock_domain;
    uint32_t sequence;
} example_rotor_sample_t;

/** \brief Exactly two SPSC handoff slots; no device access from the consumer.
 * \note Task owns publication history; IRQ owns cached and have_cached.
 *       Indices/fault use Arch atomics. Initialization requires both users and
 *       hardware callbacks joined. Metadata remains immutable while live.
 */
typedef struct {
    example_rotor_sample_t slots[2];
    example_rotor_sample_t cached;
    uint64_t epoch;
    nx_time_us_t last_published_at;
    nx_time_us_t last_taken_at;
    uint32_t last_sequence;
    uint32_t clock_domain;
    uint32_t maximum_age_us;
    uint32_t produced;
    uint32_t consumed;
    uint32_t fault;
    bool initialized;
    bool have_cached;
} example_rotor_queue_t;

/** \brief Initialize joined handoff storage without touching the sensor.
 * \param[out] queue: Zeroed or fully joined independent storage.
 * \param[in] clock_domain: Nonzero observation clock domain.
 * \param[in] epoch: Nonzero immutable observation epoch.
 * \param[in] maximum_age_us: Positive accepted software-observation age.
 * \return SUCCESS or INVALID; rejection preserves storage.
 * \note Task before publication. Physical sensor/transport uncertainty needs
 *       a separate external error budget; age alone does not qualify it.
 */
nx_result_t example_rotor_queue_initialize(example_rotor_queue_t* queue,
                                           uint32_t clock_domain,
                                           uint64_t epoch,
                                           uint32_t maximum_age_us);

/** \brief Publish one whole source observation without overwriting a slot.
 * \param[in,out] queue: Same sole Task producer; initialized stable storage.
 * \param[in] sample: Complete newer same-domain observation, copied on success.
 * \return SUCCESS, BUSY, INVALID or STATE; rejection preserves publication.
 * \note Task only. Sequence is monotonic, not a continuous ADC-cycle promise.
 */
nx_result_t example_rotor_publish(example_rotor_queue_t* queue,
                                  const example_rotor_sample_t* sample);

/** \brief Close angle admission independently of handoff capacity.
 * \param[in,out] queue: Stable initialized channel shared by its owners.
 * \param[in] reason: Non-success result retained until joined reset.
 * \return Retained first fault, or INVALID for invalid arguments.
 * \note Task/IRQ atomic publication; no sensor access or hardware stop.
 */
nx_result_t example_rotor_fault(example_rotor_queue_t* queue,
                                nx_result_t reason);

/** \brief Take at most one record, or reuse only a fresh cached observation.
 * \param[in,out] queue: Same sole fast consumer; no SPI access is performed.
 * \param[in] now: Finite monotonic observation time in the selected domain.
 * \param[in] clock_domain: Exact current observation domain.
 * \param[in] epoch: Exact current observation epoch.
 * \param[out] sample: Separate output, unchanged on EMPTY/error.
 * \return SUCCESS, EMPTY, TIMEOUT or exact retained source fault.
 * \note IRQ/Task serialized fast owner. The returned angle_at is the sensor
 *       driver's software observation; it is not relabeled as ADC sample time.
 */
nx_result_t example_rotor_take(example_rotor_queue_t* queue, nx_time_us_t now,
                               uint32_t clock_domain, uint64_t epoch,
                               example_rotor_sample_t* sample);

#ifdef __cplusplus
}
#endif
#endif /* EXAMPLE_MOTOR_ROTOR_H */
