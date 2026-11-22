/**
 * \file            fault_export.h
 * \brief           Durable fault capture and bounded raw export in task context
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-22
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#ifndef EXAMPLE_DIAGNOSTICS_FAULT_EXPORT_H
#define EXAMPLE_DIAGNOSTICS_FAULT_EXPORT_H
#include "nexus/components/fault_storage.h"
#include "nexus/core/time.h"
#ifdef __cplusplus
extern "C" {
#endif

/** \brief One fixed validated snapshot; the persistent source is never erased.
 * \note Zero before use. One normal task owns the entire object through all
 *       sink callbacks. Offset counts synchronous local sink acceptance only.
 */
typedef struct {
    nx_fault_record_t record;
    size_t offset;
    bool ready;
} example_fault_export_t;

/**
 * \brief A synchronous sink reports an exact copied prefix, including errors.
 * \note Task-only. SUCCESS requires all bytes; BUSY may copy a shorter prefix.
 *       The source and accepted pointer are borrowed only until return.
 *       No callback, DMA or queue may retain either reference. Local acceptance
 *       does not establish host receipt, durable delivery or authorization.
 */
typedef nx_result_t (*example_fault_write_t)(void* context, const void* source,
                                             size_t bytes, size_t* accepted,
                                             nx_time_us_t deadline);

/**
 * \brief Persist retained capture, then validate a copied durable raw record.
 * \param[in,out] state: Unused or completed exclusive export snapshot.
 * \param[in,out] retained: Quiescent valid boot-retained record, never NULL.
 * \param[in,out] storage: Open product-selected exclusive two-bank Storage.
 * \param[in] expected_build_id: Nonzero 16-byte identity of the chosen ELF.
 * \return OK, NOT_FOUND, CORRUPT, UNSUPPORTED for identity/version mismatch,
 *         INVALID or original Storage failure. Rejection keeps state unchanged.
 * \note Normal startup/task only after Storage initialization and appropriate
 *       maintenance admission. Actual nx_fault_record_persist saves before ack;
 *       failed mutation preserves retention and may require Storage reopen.
 *       If no retained record exists, load the previous durable record. No
 *       formatting, Flash reservation, retry, overwrite policy or fault entry
 *       is created. Persistent data survives export, failure and reboot.
 * Source, state, Storage workspace and expected identity extents are disjoint.
 */
nx_storage_status_t example_fault_export_begin(
    example_fault_export_t* state, volatile nx_fault_record_t* retained,
    nx_storage_t* storage, const uint8_t expected_build_id[16]);

/**
 * \brief Copy at most maximum_bytes from the same validated raw snapshot.
 * \param[in,out] state: Live sole-owner snapshot; callback must not reenter it.
 * \param[in,out] context: Product sink context, never retained here.
 * \param[in] write: Synchronous bounded callback with exact prefix reporting.
 * \param[in] maximum_bytes: Explicit per-call bound, from 1 through 160.
 * \param[in] deadline: Finite absolute sink-clock deadline, passed unchanged.
 * \return SUCCESS when all bytes locally accepted, BUSY with work remaining,
 *         STATE before begin, INVALID, or exact sink error.
 * \note No busy retry, storage write, record acknowledgment or implicit clock.
 *       Errors preserve the snapshot; reported copied prefixes advance offset.
 *       Persisted data is never discarded on local acceptance. The receiver
 *       saves exactly 160 raw bytes and runs the existing fault_decode.py with
 *       the ELF selected by the captured build ID before trusting symbols.
 */
nx_result_t example_fault_export_step(example_fault_export_t* state,
                                      void* context,
                                      example_fault_write_t write,
                                      size_t maximum_bytes,
                                      nx_time_us_t deadline);

#ifdef __cplusplus
}
#endif
#endif /* EXAMPLE_DIAGNOSTICS_FAULT_EXPORT_H */
