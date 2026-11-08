/**
 * \file            flash_application.h
 * \brief           Explicitly authorized application Flash maintenance
 * \author          Nexus Team
 */
#ifndef EXAMPLE_FLASH_APPLICATION_H
#define EXAMPLE_FLASH_APPLICATION_H

#include "nexus/io/flash.h"
#ifdef __cplusplus
extern "C" {
#endif
/**
 * \brief One finite transaction's copied successful-return observations.
 * \note False does not prove Flash unchanged: a failed operation can have
 *       completed a pulse or partly erased/programmed. No atomicity is implied.
 */
typedef struct {
    bool erased;
    bool programmed;
    bool verified;
} example_flash_report_t;
/**
 * \brief           Erase one selected sector, program and verify 32 bytes.
 * \param[in]       region: Reserved writable sector; program unit divides 32.
 * \param[in]       authorized: Explicit product maintenance admission.
 * \param[in]       deadline: Absolute live Core deadline, never a duration.
 * \param[in,out]   payload: Separate caller-owned 32-byte writable RAM buffer.
 * \param[out]      report: Separate copied observations, not a recovery token.
 * \return          SUCCESS or the first rejected/failed operation.
 * \note            Serialized Thread only. The caller joins every other master
 *                  and owns execution, voltage, watchdog and power-loss policy.
 *                  Returning Flash calls settle their pulse; a stuck physical
 *                  pulse may never return. There is no automatic retry.
 *                  Rejected authorization performs no Flash operation.
 *                  The selected provider owns physical buffer admission. Keep
 *                  the separate caller RAM alive until this function returns.
 */
nx_result_t example_flash_maintenance(const nx_flash_region_t* region,
                                      bool authorized, nx_time_us_t deadline,
                                      uint8_t payload[32],
                                      example_flash_report_t* report);
#ifdef __cplusplus
}
#endif
#endif /* EXAMPLE_FLASH_APPLICATION_H */
