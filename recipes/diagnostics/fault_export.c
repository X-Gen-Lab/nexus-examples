/**
 * \file            fault_export.c
 * \brief           Durable-before-ack capture and one bounded export step
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-22
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "fault_export.h"
#include <string.h>

/** \brief Preserve exact public read errors at the Storage composition edge. */
static nx_storage_status_t read_status(nx_result_t result) {
    if (result == NX_ERROR_UNSUPPORTED)
        return NX_STORAGE_UNSUPPORTED;
    return result == NX_ERROR_INVALID ? NX_STORAGE_INVALID : NX_STORAGE_CORRUPT;
}

nx_storage_status_t example_fault_export_begin(
    example_fault_export_t* state, volatile nx_fault_record_t* retained,
    nx_storage_t* storage, const uint8_t expected_build_id[16]) {
    if (state == NULL || retained == NULL || storage == NULL ||
        expected_build_id == NULL ||
        (state->ready && state->offset < NX_FAULT_RECORD_BYTES))
        return NX_STORAGE_INVALID;
    bool has_identity = false;
    for (size_t i = 0U; i < NX_FAULT_BUILD_ID_BYTES; ++i)
        has_identity = has_identity || expected_build_id[i] != 0U;
    if (!has_identity)
        return NX_STORAGE_INVALID;
    nx_fault_capture_t facts;
    nx_result_t read = nx_fault_record_read(retained, &facts);
    if (read != NX_SUCCESS && read != NX_ERROR_EMPTY)
        return read_status(read);
    if (read == NX_SUCCESS) {
        if (memcmp(facts.build_id, expected_build_id,
                   NX_FAULT_BUILD_ID_BYTES) != 0)
            return NX_STORAGE_UNSUPPORTED;
        nx_storage_status_t saved = nx_fault_record_persist(retained, storage);
        if (saved != NX_STORAGE_OK)
            return saved;
    }
    nx_fault_record_t copy;
    size_t bytes = sizeof(copy);
    nx_storage_status_t result = nx_storage_load(storage, &copy, &bytes);
    if (result != NX_STORAGE_OK)
        return result == NX_STORAGE_NO_SPACE ? NX_STORAGE_CORRUPT : result;
    if (bytes != sizeof(copy))
        return NX_STORAGE_CORRUPT;
    read = nx_fault_record_read(&copy, &facts);
    if (read != NX_SUCCESS)
        return read_status(read);
    if (memcmp(facts.build_id, expected_build_id, NX_FAULT_BUILD_ID_BYTES) != 0)
        return NX_STORAGE_UNSUPPORTED;
    *state = (example_fault_export_t){.record = copy, .ready = true};
    return NX_STORAGE_OK;
}

nx_result_t example_fault_export_step(example_fault_export_t* state,
                                      void* context,
                                      example_fault_write_t write,
                                      size_t maximum_bytes,
                                      nx_time_us_t deadline) {
    if (state == NULL || write == NULL || maximum_bytes == 0U ||
        maximum_bytes > NX_FAULT_RECORD_BYTES || deadline == NX_DEADLINE_NEVER)
        return NX_ERROR_INVALID;
    if (!state->ready || state->offset > NX_FAULT_RECORD_BYTES)
        return NX_ERROR_STATE;
    size_t count = NX_FAULT_RECORD_BYTES - state->offset;
    if (count == 0U)
        return NX_SUCCESS;
    if (count > maximum_bytes)
        count = maximum_bytes;
    size_t accepted = 0U;
    nx_result_t result =
        write(context, (const uint8_t*)&state->record + state->offset, count,
              &accepted, deadline);
    if (accepted > count)
        return NX_ERROR_IO;
    state->offset += accepted;
    if (result != NX_SUCCESS)
        return result;
    if (accepted != count)
        return NX_ERROR_IO;
    return state->offset == NX_FAULT_RECORD_BYTES ? NX_SUCCESS : NX_ERROR_BUSY;
}
