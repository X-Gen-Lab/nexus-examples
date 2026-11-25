/**
 * \file            trial_update_reference.h
 * \brief           Product trial intent and authenticated boot confirmation
 * \author          Nexus Team
 */
#ifndef NEXUS_EXAMPLE_TRIAL_UPDATE_REFERENCE_H
#define NEXUS_EXAMPLE_TRIAL_UPDATE_REFERENCE_H
#include "boot_audit_reference.h"
#include "nexus/components/mcuboot_swap_control.h"

#ifdef __cplusplus
extern "C" {
#endif

/** \brief Independent facts copied after the boot evaluation. */
typedef struct {
    example_boot_audit_report_t image;
    nx_result_t health;
    bool health_checked;
    bool confirm_attempted;
    bool confirmed;
} example_trial_boot_report_t;

/**
 * \brief Record authenticated staged identity, then request a scratch TEST.
 * \param[in] operation: Active finite update-intent database operation.
 * \param[in] identifier: Nonzero identity from actual authenticated staging.
 * \return Actual persistence/MCUboot error or SUCCESS after TEST readback.
 * \note Call from the staging recipe's request callback after authorizing
 *       the full image. The product supplies the official immutable
 *       three-area swap scope for the same deadline and serializes Flash use.
 *       An already armed update or a pending revert preserves its earlier ID.
 *       Failure may modify persistent storage. Inspect it before any retry.
 *       This function does not quiesce or reset the device.
 */
nx_result_t
example_update_request_trial(const nx_flashdb_operation_t* operation,
                             uint64_t identifier);

/**
 * \brief Audit the running image and confirm a healthy matching trial once.
 * \param[in] operation: Active finite update/audit database operation.
 * \param[in] ports: Authentication port for the actual running image.
 * \param[in] clock: Same monotonic domain as the swap and database scopes.
 * \param[in] check_health: Bounded product self-test of the running image.
 * \param[in] health_context: Stable optional callback context.
 * \param[out] report: Independent copied image, health and confirmation facts.
 * \return SUCCESS after official confirmation/readback, PERMISSION for a
 *         different authenticated image, or actual authentication, health,
 *         persistence, deadline or trailer error.
 * \note Only an officially pending REVERT can trigger a new confirmation.
 *       An already confirmed matching image reports that observation without
 *       another write. A confirmation failure may have persisted effects.
 *       Product performs provider recovery before repeating an uncertain
 *       mutation. A failed check leaves the trial unconfirmed for MCUboot to
 *       revert on a separately authorized subsequent boot.
 */
nx_result_t example_update_confirm_trial(
    const nx_flashdb_operation_t* operation,
    const example_boot_audit_ports_t* ports, const nx_clock_t* clock,
    nx_result_t (*check_health)(void* context, nx_time_us_t deadline),
    void* health_context, example_trial_boot_report_t* report);

#ifdef __cplusplus
}
#endif
#endif
