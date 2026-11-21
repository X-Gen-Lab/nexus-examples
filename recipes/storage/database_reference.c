/**
 * \file            database_reference.c
 * \brief           Explicit product KV, event and snapshot persistence policy
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-21
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "database_reference.h"
#include "nexus/algorithms/endian.h"
#include "nexus/components/persistent_record.h"

/** \brief This product example has one executor, excluding other DB users. */
static const nx_flashdb_operation_t* active_operation;

/** \brief Product-owned scope is valid only inside the complete recipe call. */
const nx_flashdb_operation_t* nx_flashdb_operation_get(void) {
    return active_operation;
}

/** \brief Keep the existing Settings status vocabulary at its API boundary. */
static nx_result_t settings_result(nx_storage_status_t result) {
    if (result == NX_STORAGE_OK) {
        return NX_SUCCESS;
    }
    if (result == NX_STORAGE_INVALID) {
        return NX_ERROR_INVALID;
    }
    if (result == NX_STORAGE_NO_SPACE) {
        return NX_ERROR_EXHAUSTED;
    }
    return result == NX_STORAGE_UNSUPPORTED ? NX_ERROR_UNSUPPORTED
                                            : NX_ERROR_IO;
}

/** \brief Demonstrate distinct commit points without claiming an atomic group.
 */
nx_result_t example_storage_publish(example_storage_plan_t* plan,
                                    bool allow_create, bool commit_snapshot,
                                    uint32_t period_ms, int64_t timestamp,
                                    uint64_t identifier, nx_time_us_t deadline,
                                    example_storage_report_t* report) {
    if (plan == NULL || report == NULL || plan->key_values == NULL ||
        plan->time_series == NULL || plan->settings == NULL ||
        !plan->settings->opened || plan->record == NULL ||
        plan->record_capacity < NX_PERSISTENT_RECORD_HEADER_BYTES + 4U ||
        period_ms == 0U || timestamp <= 0 || identifier == 0U ||
        deadline == NX_DEADLINE_NEVER) {
        return NX_ERROR_INVALID;
    }
    if (active_operation != NULL) {
        return NX_ERROR_BUSY;
    }
    if (plan->settings->schema != plan->schema) {
        return NX_ERROR_UNSUPPORTED;
    }
    *report = (example_storage_report_t){0};
    nx_flashdb_operation_t operation = {plan->key_values, deadline};
    active_operation = &operation;
    nx_flashdb_open_policy_t policy =
        allow_create ? NX_FLASHDB_CREATE_IF_ERASED : NX_FLASHDB_EXISTING_ONLY;
    nx_result_t result = NX_SUCCESS;
    if (!plan->key_values->opened) {
        result = nx_flashdb_open(&operation, policy);
        if (result != NX_SUCCESS) {
            goto finished;
        }
    }
    report->kv_opened = true;
    operation.database = plan->time_series;
    if (!plan->time_series->opened) {
        result = nx_flashdb_open(&operation, policy);
        if (result != NX_SUCCESS) {
            goto finished;
        }
    }
    report->ts_opened = true;
    uint8_t encoded_period[4];
    result = nx_endian_write(encoded_period, sizeof(encoded_period), true,
                             period_ms);
    if (result != NX_SUCCESS) {
        goto finished;
    }
    result = settings_result(nx_settings_set(
        plan->settings, 42U, encoded_period, sizeof(encoded_period)));
    if (result != NX_SUCCESS) {
        goto finished;
    }
    report->staged = true;
    operation.database = plan->key_values;
    result = nx_flashdb_kv_set(&operation, "last_period", encoded_period,
                               sizeof(encoded_period));
    if (result != NX_SUCCESS) {
        goto finished;
    }
    report->kv_saved = true;
    size_t record_bytes = plan->record_capacity;
    result = nx_persistent_record_encode(plan->schema, identifier,
                                         encoded_period, sizeof(encoded_period),
                                         plan->record, &record_bytes);
    if (result != NX_SUCCESS) {
        goto finished;
    }
    operation.database = plan->time_series;
    result =
        nx_flashdb_ts_append(&operation, timestamp, plan->record, record_bytes);
    if (result != NX_SUCCESS) {
        goto finished;
    }
    report->record_appended = true;
    if (commit_snapshot) {
        result = settings_result(nx_settings_commit(plan->settings));
        if (result != NX_SUCCESS) {
            goto finished;
        }
        report->snapshot_committed = true;
    }
finished:
    active_operation = NULL;
    return result;
}
