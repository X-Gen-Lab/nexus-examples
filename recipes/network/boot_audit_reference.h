/**
 * \file            boot_audit_reference.h
 * \brief           Product update intention and authenticated boot observation
 * \author          Nexus Team
 */
#ifndef NEXUS_EXAMPLE_BOOT_AUDIT_REFERENCE_H
#define NEXUS_EXAMPLE_BOOT_AUDIT_REFERENCE_H
#include "nexus/components/flashdb.h"
#ifdef __cplusplus
extern "C" {
#endif
/** \brief Actual running image and product authentication, never a boot flag.
 */
typedef struct {
    nx_flash_region_t running;
    uint32_t image_bytes;
    nx_result_t (*authenticate)(void* context, const nx_flash_region_t* running,
                                uint32_t image_bytes, nx_time_us_t deadline,
                                uint64_t* identifier);
    void* context;
} example_boot_audit_ports_t;
/** \brief Observed boot result and its separate persistence outcome. */
typedef struct {
    uint64_t expected;
    uint64_t observed;
    nx_result_t authentication;
    bool matches;
    bool persisted;
} example_boot_audit_report_t;
/**
 * \brief Persist the product's intended image before requesting an upgrade.
 * \param[in] operation: Exact active open KVDB scope and finite deadline.
 * \param[in] identifier: Nonzero ID authenticated from the staged candidate.
 * \return SUCCESS or actual storage error; failure must block upgrade request.
 * \note Task-only. Product authorizes replacing the previous intention and
 *       assigns IDs that identify authenticated image contents. The fixed key
 *       belongs to this recipe. This does not modify MCUboot trailers or claim
 *       that an upgrade request/boot copy succeeded.
 */
nx_result_t example_update_track(const nx_flashdb_operation_t* operation,
                                 uint64_t identifier);
/**
 * \brief Inspect actual running bytes and persist the next-boot observation.
 * \param[in] operation: Active open KVDB scope for update intention and report.
 * \param[in] ports: Disjoint stable read region and product authentication.
 * \param[out] report: Disjoint copied observations; untouched on admission
 *                     failure; persisted is false after a failed write.
 * \return SUCCESS means the observation was recorded, not a healthy update;
 *         EMPTY without inspection if no intention, BUSY for ongoing auth,
 *         or the actual record/persistence error.
 * \note One serialized task after storage starts. authenticate reads and
 *       authenticates actual running bytes, checks product target/version and
 *       publishes a nonzero content ID only on SUCCESS. It borrows its inputs
 *       only until return and uses the same deadline. A different authenticated
 *       image and an authentication error are recordable failure observations.
 *       This adds no trial, confirmation, rollback, reset or power-cut proof.
 */
nx_result_t example_update_audit_boot(const nx_flashdb_operation_t* operation,
                                      const example_boot_audit_ports_t* ports,
                                      example_boot_audit_report_t* report);
/**
 * \brief Read the last complete observation for bounded telemetry/export.
 * \param[in] operation: Exact active same-owner KVDB scope and deadline.
 * \param[out] report: Disjoint copied report, unchanged on invalid record.
 * \return SUCCESS, EMPTY or actual format/read error; no implicit erase.
 * \note The report identifies its expected image independently of a newer
 *       intention. Product assigns retention and remote ACK policy.
 */
nx_result_t example_update_boot_report(const nx_flashdb_operation_t* operation,
                                       example_boot_audit_report_t* report);
#ifdef __cplusplus
}
#endif
#endif
