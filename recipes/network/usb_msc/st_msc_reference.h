/**
 * \file            st_msc_reference.h
 * \brief           Private single-device ST MSC product reference binding
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-21
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#ifndef NEXUS_EXAMPLE_ST_MSC_REFERENCE_H
#define NEXUS_EXAMPLE_ST_MSC_REFERENCE_H
#include "nexus/io/block.h"
#include "usbd_msc.h"
#ifdef __cplusplus
extern "C" {
#endif
/** \brief Selected software reference: one USB Device, one 512-byte-sector LUN.
 * \note Product-private binding, not a general platform registry. Serial task
 *       context only. Port/table/context and clock epoch stay alive until ST
 *       class deinit, USB DCD drain and explicit unbind have all succeeded.
 */
nx_result_t example_st_msc_bind(const nx_block_port_t* block, nx_clock_t clock,
                                uint32_t operation_budget_us);
/**
 * \brief           Authorize MSC after local filesystem unmount and media sync.
 * \return          SUCCESS or STATE; no controller or worker is started.
 * \note            Called by the product's exclusive usb_start boundary before
 *                  ST class initialize. Failure leaves the local owner retired.
 */
nx_result_t example_st_msc_activate(void);
/**
 * \brief           Release binding after class deinit and real DCD withdrawal.
 * \return          SUCCESS, BUSY while class allocation/callback is retained,
 *                  or STATE for an unbound profile.
 * \note            Does not stop USB, revoke DMA, reset media or mount a
 * filesystem.
 */
nx_result_t example_st_msc_unbind(void);
/** \brief Read first storage error; SUCCESS says nothing about peer delivery.
 */
nx_result_t example_st_msc_fault(void);
/** \brief Private official records passed to the existing Nexus ST bridge. */
extern USBD_StorageTypeDef example_st_msc_storage;
extern USBD_DescriptorsTypeDef example_st_msc_descriptors;
/** \brief Exact selected class allocation; no heap or maximum-size pool. */
void* example_st_msc_allocate(uint32_t bytes);
/** \brief Release exact class storage only after its users have joined. */
void example_st_msc_free(void* storage);
#ifdef __cplusplus
}
#endif
#endif
