/**
 * \file            msc_handoff.h
 * \brief           Product-owned exclusive local-filesystem and USB media
 * handoff \author          Nexus Team \version         1.0.0 \date 2026-11-21
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#ifndef NEXUS_EXAMPLE_MSC_HANDOFF_H
#define NEXUS_EXAMPLE_MSC_HANDOFF_H
#include "nexus/core/time.h"
#ifdef __cplusplus
extern "C" {
#endif
/** \brief Stable synchronous product boundaries; every success proves join. */
typedef struct {
    nx_result_t (*local_unmount)(void* context, nx_time_us_t deadline);
    nx_result_t (*local_mount)(void* context, nx_time_us_t deadline);
    nx_result_t (*usb_start)(void* context, nx_time_us_t deadline);
    nx_result_t (*usb_stop)(void* context, nx_time_us_t deadline);
    nx_result_t (*media_sync)(void* context, nx_time_us_t deadline);
    void* context;
} example_msc_ports_t;
/** \brief CLEANUP states retain the attempted owner's full responsibility. */
typedef enum {
    EXAMPLE_MEDIA_LOCAL,
    EXAMPLE_MEDIA_NONE,
    EXAMPLE_MEDIA_USB,
    EXAMPLE_MEDIA_USB_CLEANUP,
    EXAMPLE_MEDIA_LOCAL_CLEANUP
} example_media_owner_t;
typedef struct {
    example_msc_ports_t ports;
    example_media_owner_t owner;
    bool active;
} example_msc_handoff_t;
/**
 * \brief           Bind an already mounted local filesystem and shared media.
 * \param[out]      handoff: Unused caller state.
 * \param[in]       ports: Stable copied callbacks; contexts retained through
 * join. \return          SUCCESS or INVALID, no hardware access. \note One
 * serialized task owns all local-FS and MSC users. Unmount must flush and join
 * open files; stop must join USB requests, DMA and callbacks. No concurrent
 * filesystem/MSC writes.
 */
nx_result_t example_msc_handoff_init(example_msc_handoff_t* handoff,
                                     const example_msc_ports_t* ports);
/**
 * \brief           Withdraw local filesystem before starting selected ST MSC.
 * \param[in,out]   handoff: Serialized task owner, all producers joined.
 * \param[in]       deadline: Finite same-domain deadline forwarded unchanged.
 * \return          SUCCESS or actual failure, retaining cleanup responsibility.
 * \note            Failure never authorizes concurrent local and USB access.
 *                  Failed USB start/stop requires request_local to stop/join
 *                  it. Failed local unmount retains LOCAL_CLEANUP.
 */
nx_result_t example_msc_request_usb(example_msc_handoff_t* handoff,
                                    nx_time_us_t deadline);
/**
 * \brief           Join USB, sync media, then mount the local filesystem.
 * \param[in,out]   handoff: Serialized owner with retained stable ports.
 * \param[in]       deadline: Finite same-domain deadline forwarded unchanged.
 * \return          SUCCESS or actual failure; no storage is reclaimed on
 * timeout. \note            Failed local mount retains LOCAL_CLEANUP; a later
 * request must unmount/join its partial state before another owner.
 */
nx_result_t example_msc_request_local(example_msc_handoff_t* handoff,
                                      nx_time_us_t deadline);
#ifdef __cplusplus
}
#endif
#endif
