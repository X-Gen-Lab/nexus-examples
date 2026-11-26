/**
 * \file            update_reference.h
 * \brief           Explicit staging, image authorization and upgrade sequence
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-21
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#ifndef NEXUS_EXAMPLE_UPDATE_REFERENCE_H
#define NEXUS_EXAMPLE_UPDATE_REFERENCE_H
#include "nexus/io/flash.h"
#ifdef __cplusplus
extern "C" {
#endif
/** \brief Product ports, serialized task callbacks with no retained input. */
typedef struct {
    nx_flash_region_t staging;
    nx_clock_t clock;
    nx_result_t (*authorize)(void* context, const nx_flash_region_t* staging,
                             uint32_t image_bytes, nx_time_us_t deadline);
    nx_result_t (*request_upgrade)(void* context, nx_time_us_t deadline);
    nx_result_t (*quiesce)(void* context, nx_time_us_t deadline);
    void* context;
} example_update_ports_t;
/** \brief Staging lifecycle; errors may follow persistent modification. */
typedef enum {
    EXAMPLE_UPDATE_COLD,
    EXAMPLE_UPDATE_RECEIVING,
    EXAMPLE_UPDATE_AUTHENTICATED,
    EXAMPLE_UPDATE_REQUESTED,
    EXAMPLE_UPDATE_READY_TO_RESET,
    EXAMPLE_UPDATE_FAILED
} example_update_state_t;
/** \brief Caller-owned product state; fields private after begin. */
typedef struct {
    example_update_ports_t ports;
    nx_time_us_t deadline;
    nx_time_us_t last_time;
    uint32_t image_bytes;
    uint32_t written;
    nx_result_t failure;
    example_update_state_t state;
    bool active;
} example_update_t;
/**
 * \brief           Begin an explicitly authorized/pre-erased staging session.
 * \param[out]      update: Unused/quiescent caller state.
 * \param[in]       ports: Stable product ports; values copied, contexts
 * retained. \param[in]       image_bytes: Positive program-aligned length
 * within region. \param[in]       deadline: Finite absolute deadline shared by
 * the whole flow. \return          SUCCESS or admission failure, with no I/O on
 * rejection. \note            Caller owns prior staging erase and Flash
 * maintenance entry. Region excludes boot metadata/trailer and all live images.
 */
nx_result_t example_update_begin(example_update_t* update,
                                 const example_update_ports_t* ports,
                                 uint32_t image_bytes, nx_time_us_t deadline);
/**
 * \brief           Program the next sequential staging bytes through Nexus.
 * \param[in,out]   update: Serialized RECEIVING state, maintenance still
 * active. \param[in]       bytes: Disjoint program-aligned input, expires on
 * return. \param[in]       count: Positive bytes, no more than remaining image
 * bytes. \return          SUCCESS or latched actual failure; no automatic
 * replay. \note            Failure can follow partial programming. It never
 * authenticates an image or authorizes retry without provider recovery.
 */
nx_result_t example_update_write(example_update_t* update, const void* bytes,
                                 size_t count);
/**
 * \brief           Authenticate complete bytes, then request the selected
 * upgrade. \param[in,out]   update: Fully staged serialized RECEIVING state.
 * \return          SUCCESS or latched failure; authorize runs before request.
 * \note            authorize must authenticate actual staged bytes and enforce
 *                  product version/target policy. CRC/download success alone
 *                  cannot satisfy it. request_upgrade binds the selected
 *                  MCUboot profile and its exact Flash operation deadline.
 */
nx_result_t example_update_request(example_update_t* update);
/**
 * \brief           Quiesce every producer, DMA, callback and platform borrower.
 * \param[in,out]   update: REQUESTED state and all callback storage still
 * alive. \return          SUCCESS only after actual quiesce; BUSY/error retain
 * REQUESTED and may be retried with the same original finite deadline. \note No
 * reset occurs here. Product resets only after success and independently
 * records the next boot's authenticated outcome.
 */
nx_result_t example_update_prepare_reset(example_update_t* update);
#ifdef __cplusplus
}
#endif
#endif
