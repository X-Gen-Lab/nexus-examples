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
 * \brief           Erase one selected sector, program and verify one flashword.
 * \param[in]       region: Application-reserved writable sector; 32-byte unit.
 * \param[in]       authorized: Explicit product maintenance admission.
 * \param[in]       deadline: Absolute live Core deadline, never a duration.
 * \param[in,out]   payload: Separate caller-owned 32-byte writable RAM buffer.
 * \param[out]      report: Separate copied observations, not a recovery token.
 * \return          SUCCESS or the first rejected/failed operation.
 * \note            Serialized Thread only. The caller joins every other master
 *                  and owns execution, voltage, watchdog and power-loss policy.
 *                  Returning Flash calls settle their pulse; a stuck physical
 *                  pulse may never return. No retry programs an ECC word twice.
 *                  Rejected authorization performs no Flash operation.
 *                  The exact H743 provider rejects payload inside physical
 *                  Flash or its private RAM pod; keep the separate caller
 *                  buffer alive until this synchronous function returns.
 */
nx_result_t example_flash_maintenance(const nx_flash_region_t* region,
                                      bool authorized, nx_time_us_t deadline,
                                      uint8_t payload[32],
                                      example_flash_report_t* report);
#ifdef __cplusplus
}
#endif
#endif /* EXAMPLE_FLASH_APPLICATION_H */
