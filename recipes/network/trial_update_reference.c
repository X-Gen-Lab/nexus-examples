/**
 * \file            trial_update_reference.c
 * \brief           Scratch TEST intent and authenticated, healthy confirmation
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-25
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "trial_update_reference.h"

static bool valid_operation(const nx_flashdb_operation_t* operation) {
    return operation != NULL && operation->database != NULL &&
           operation->database->opened &&
           operation->database->kind == NX_FLASHDB_KV &&
           operation->deadline != NX_DEADLINE_NEVER;
}

static bool output_disjoint(const example_trial_boot_report_t* report,
                            const void* input, size_t bytes) {
    uintptr_t a = (uintptr_t)report;
    uintptr_t b = (uintptr_t)input;
    return a <= b ? b - a >= sizeof(*report) : a - b >= bytes;
}

static nx_result_t time_remaining(const nx_clock_t* clock, nx_time_us_t* prior,
                                  nx_time_us_t deadline) {
    nx_time_us_t now = clock->read(clock->context);
    if (now == NX_DEADLINE_NEVER || now < *prior) {
        return NX_ERROR_STATE;
    }
    *prior = now;
    return now >= deadline ? NX_ERROR_TIMEOUT : NX_SUCCESS;
}

nx_result_t
example_update_request_trial(const nx_flashdb_operation_t* operation,
                             uint64_t identifier) {
    if (!valid_operation(operation) || identifier == 0U) {
        return NX_ERROR_INVALID;
    }
    nx_mcuboot_action_t action;
    nx_result_t result = nx_mcuboot_swap_query(&action);
    if (result != NX_SUCCESS) {
        return result;
    }
    if (action != NX_MCUBOOT_ACTION_NONE) {
        return NX_ERROR_STATE;
    }
    result = example_update_track(operation, identifier);
    return result == NX_SUCCESS
               ? nx_mcuboot_swap_request(NX_MCUBOOT_REQUEST_TEST)
               : result;
}

static nx_result_t evaluate(const nx_flashdb_operation_t* operation,
                            const example_boot_audit_ports_t* ports,
                            const nx_clock_t* clock,
                            nx_result_t (*check_health)(void*, nx_time_us_t),
                            void* health_context,
                            example_trial_boot_report_t* report) {
    nx_time_us_t prior = 0U;
    nx_time_us_t deadline = operation->deadline;
    nx_result_t result = time_remaining(clock, &prior, deadline);
    if (result != NX_SUCCESS) {
        return result;
    }
    result = example_update_audit_boot(operation, ports, &report->image);
    if (result != NX_SUCCESS) {
        return result;
    }
    if (!report->image.matches) {
        return report->image.authentication == NX_SUCCESS
                   ? NX_ERROR_PERMISSION
                   : report->image.authentication;
    }
    result = time_remaining(clock, &prior, deadline);
    if (result != NX_SUCCESS) {
        return result;
    }
    nx_mcuboot_action_t action;
    result = nx_mcuboot_swap_query(&action);
    if (result != NX_SUCCESS) {
        return result;
    }
    if (action == NX_MCUBOOT_ACTION_NONE) {
        result = nx_mcuboot_swap_confirmed(&report->confirmed);
        return result != NX_SUCCESS || report->confirmed ? result
                                                         : NX_ERROR_STATE;
    }
    if (action != NX_MCUBOOT_ACTION_REVERT) {
        return NX_ERROR_STATE;
    }
    result = time_remaining(clock, &prior, deadline);
    if (result != NX_SUCCESS) {
        return result;
    }
    report->health = check_health(health_context, deadline);
    report->health_checked = true;
    if (report->health != NX_SUCCESS) {
        return report->health;
    }
    result = time_remaining(clock, &prior, deadline);
    if (result != NX_SUCCESS) {
        return result;
    }
    report->confirm_attempted = true;
    result = nx_mcuboot_swap_confirm();
    report->confirmed = result == NX_SUCCESS;
    return result;
}

nx_result_t example_update_confirm_trial(
    const nx_flashdb_operation_t* operation,
    const example_boot_audit_ports_t* ports, const nx_clock_t* clock,
    nx_result_t (*check_health)(void* context, nx_time_us_t deadline),
    void* health_context, example_trial_boot_report_t* report) {
    if (!valid_operation(operation) || ports == NULL || clock == NULL ||
        clock->read == NULL || check_health == NULL || report == NULL ||
        !output_disjoint(report, operation, sizeof(*operation)) ||
        !output_disjoint(report, operation->database,
                         sizeof(*operation->database)) ||
        !output_disjoint(report, ports, sizeof(*ports)) ||
        !output_disjoint(report, clock, sizeof(*clock))) {
        return NX_ERROR_INVALID;
    }
    example_trial_boot_report_t next = {0};
    nx_result_t result =
        evaluate(operation, ports, clock, check_health, health_context, &next);
    *report = next;
    return result;
}
