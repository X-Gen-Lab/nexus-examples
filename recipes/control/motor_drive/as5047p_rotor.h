/**
 * \file            as5047p_rotor.h
 * \brief           Optional Task-side AS5047P angle source
 * \author          Nexus Team
 */
#ifndef EXAMPLE_AS5047P_ROTOR_H
#define EXAMPLE_AS5047P_ROTOR_H
#include "nexus/algorithms/motion.h"
#include "nexus/components/as5047p.h"
#include "rotor.h"
#ifdef __cplusplus
extern "C" {
#endif
/** \brief Task-only reader state; never borrowed by the power-stage callback.
 */
typedef struct {
    nx_as5047p_t* device;
    example_rotor_queue_t* queue;
    nx_encoder_tracker_t tracker;
    nx_time_us_t last_at;
    uint32_t maximum_gap_us;
    uint32_t sequence;
    float zero_rad;
    bool reversed;
    bool initialized;
    bool anchored;
} example_rotor_reader_t;

/** \brief Bind an initialized AS5047P and a fresh joined angle channel.
 * \param[out] reader: Joined independent Task-owned state.
 * \param[in,out] device: Stable initialized serialized sensor.
 * \param[in,out] queue: Fresh channel with no history, cache or fault.
 * \param[in] reversed: Normalize direction into the motor-shaft side.
 * \param[in] zero_rad: Mechanical angle correction, not electrical alignment.
 * \param[in] maximum_step_counts: Positive delta below half a turn.
 * \param[in] maximum_gap_us: Positive maximum sensor observation interval.
 * \return SUCCESS, INVALID or STATE; no transfer or output enable.
 * \note Task only. Join both reader and fast consumer before resetting the
 *       queue or changing the mapping. Product qualifies wiring and latency.
 */
nx_result_t example_rotor_reader_initialize(example_rotor_reader_t* reader,
                                            nx_as5047p_t* device,
                                            example_rotor_queue_t* queue,
                                            bool reversed, float zero_rad,
                                            uint32_t maximum_step_counts,
                                            uint32_t maximum_gap_us);

/** \brief Read real sensor frames and publish one complete bounded observation.
 * \param[in,out] reader: Same Task owner, disjoint from all IRQ state.
 * \param[in] deadline: Finite original SPI-operation deadline.
 * \return BUSY for initial anchoring/full queue, SUCCESS, or retained fault.
 * \note Task only. First observation anchors velocity and is not published.
 *       Bad parity/diagnostics/time closes consumption; no implicit recovery.
 *       BUSY never overwrites a slot. Caller explicitly joins before reset.
 */
nx_result_t example_rotor_poll(example_rotor_reader_t* reader,
                               nx_time_us_t deadline);

#ifdef __cplusplus
}
#endif
#endif /* EXAMPLE_AS5047P_ROTOR_H */
