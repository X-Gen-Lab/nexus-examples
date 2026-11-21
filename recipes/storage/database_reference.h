/**
 * \file            database_reference.h
 * \brief           Product example contrasting immediate KV and staged
 * snapshots \author          Nexus Team
 */
#ifndef EXAMPLE_STORAGE_DATABASE_REFERENCE_H
#define EXAMPLE_STORAGE_DATABASE_REFERENCE_H
#include "nexus/components/flashdb.h"
#include "nexus/components/settings.h"
#ifdef __cplusplus
extern "C" {
#endif
/** \brief All regions, private workspaces and schema choices belong to product.
 */
typedef struct {
    nx_flashdb_t* key_values;
    nx_flashdb_t* time_series;
    nx_settings_t* settings;
    uint8_t* record;
    size_t record_capacity;
    uint32_t schema;
} example_storage_plan_t;
/** \brief Successful-return observations never imply failed media is unchanged.
 */
typedef struct {
    bool kv_opened;
    bool ts_opened;
    bool staged;
    bool kv_saved;
    bool record_appended;
    bool snapshot_committed;
} example_storage_report_t;
/**
 * \brief Publish one example period setting and event under an explicit scope.
 * \param[in,out] plan: Bound independent KV/TS regions, open Settings and
 * scratch. \param[in] allow_create: Explicit permission to initialize wholly
 * erased DBs. \param[in] commit_snapshot: Separately authorize Settings durable
 * commit. \param[in] period_ms: Example business value, encoded explicitly
 * little-endian. \param[in] timestamp: Strictly increasing TS timestamp
 * selected by product. \param[in] identifier: Nonzero event deduplication
 * identity selected by product. \param[in] deadline: One finite absolute budget
 * covering the entire operation. \param[out] report: Disjoint copied
 * observations; unchanged on argument failure. \return SUCCESS or the first
 * error; does not format, recover or replay writes. \note Single product
 * executor, excluding every other FlashDB caller while the recipe's scope
 * getter is active. All inputs/workspaces/providers remain live until return.
 * KV and TS may survive even if snapshot commit fails; this function is not a
 * cross-database transaction or reliable outbox.
 */
nx_result_t example_storage_publish(example_storage_plan_t* plan,
                                    bool allow_create, bool commit_snapshot,
                                    uint32_t period_ms, int64_t timestamp,
                                    uint64_t identifier, nx_time_us_t deadline,
                                    example_storage_report_t* report);
#ifdef __cplusplus
}
#endif
#endif
