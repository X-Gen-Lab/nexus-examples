/**
 * \file            bus_ina226.h
 * \brief           Optional Task-side INA226 DC-bus source
 * \author          Nexus Team
 */
#ifndef EXAMPLE_MOTOR_BUS_INA226_H
#define EXAMPLE_MOTOR_BUS_INA226_H

#include "bus.h"
#include "nexus/components/ina226.h"

#ifdef __cplusplus
extern "C" {
#endif

/** \brief Task-only adapter; the fast callback never accesses this device.
 * \note The INA226 component retains its own configuration/recovery duties.
 *       Its current 32-bit epoch must exactly represent the channel's epoch;
 *       no truncation or invented source time is permitted.
 */
typedef struct {
    nx_ina226_t* device;
    example_bus_queue_t* queue;
    uint32_t lifetime_us;
    uint32_t sequence;
    bool initialized;
} example_bus_ina226_t;

/** \brief Bind a live serialized sensor to a fresh qualified bus channel.
 * \param[out] reader: Joined independent Task-owned storage.
 * \param[in,out] device: Initialized certain INA226 with stable binding.
 * \param[in,out] queue: Fresh channel of the same exact domain and epoch.
 * \param[in] lifetime_us: Positive allowed age from conversion-start bound.
 * \return SUCCESS, INVALID or STATE; no I2C or actuation is performed.
 * \note Task only. Caller qualifies the selected voltage range, wiring and
 *       error/latency budget. Source identity comes from the channel.
 */
nx_result_t example_bus_ina226_initialize(example_bus_ina226_t* reader,
                                          nx_ina226_t* device,
                                          example_bus_queue_t* queue,
                                          uint32_t lifetime_us);

/** \brief Explicitly start one source measurement with its finite budget.
 * \param[in,out] reader: Same sole Task owner of the INA226 source.
 * \param[in] deadline: Finite absolute sensor-operation deadline.
 * \return SUCCESS, BUSY or exact fault, also latched in the bus channel.
 * \note Task only. No hidden worker, periodic loop or wait is created.
 */
nx_result_t example_bus_ina226_begin(example_bus_ina226_t* reader,
                                     nx_time_us_t deadline);

/** \brief Complete one INA226 measurement and attempt copied publication.
 * \param[in,out] reader: Same Task after a successful begin.
 * \return SUCCESS when consumed, BUSY before ready, or retained source fault.
 * \note Conversion started_at is a conservative measurement-time lower bound;
 *       observed remains software read completion. Expiry is started_at plus
 *       lifetime, never poll time plus lifetime. Invalid or stale results
 *       close cached consumption. A full queue drops only the new record and
 *       still completes this measurement. After SUCCESS, begin a new one;
 *       do not retry poll as though the completed conversion were pending.
 */
nx_result_t example_bus_ina226_poll(example_bus_ina226_t* reader);

#ifdef __cplusplus
}
#endif
#endif /* EXAMPLE_MOTOR_BUS_INA226_H */
