/**
 * \file            gateway_delivery.c
 * \brief           Copied durable identity, real publication and joined ACK
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-22
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "gateway_delivery.h"
#include "nexus/algorithms/endian.h"
#include "nexus/components/persistent_record.h"

/** \brief Exclude output/control aliases before persistent or transport I/O. */
static bool overlap(const void* a, size_t an, const void* b, size_t bn) {
    uintptr_t x = (uintptr_t)a;
    uintptr_t y = (uintptr_t)b;
    return an > UINTPTR_MAX - x || bn > UINTPTR_MAX - y ||
           (an != 0U && bn != 0U && x < y + bn && y < x + an);
}
/** \brief Storage release precedes independent durable acknowledgement. */
static nx_result_t finish(example_gateway_delivery_t* delivery,
                          const nx_flashdb_operation_t* operation,
                          nx_result_t primary,
                          example_gateway_delivery_report_t* report) {
    if (delivery->report.network.local_sessions_released) {
        delivery->active = false;
        if (delivery->report.network.delivered &&
            !delivery->report.durable_ack) {
            delivery->report.persistence = example_outbox_acknowledge(
                delivery->outbox, operation, delivery->report.identifier);
            delivery->report.durable_ack =
                delivery->report.persistence == NX_SUCCESS;
            if (primary == NX_SUCCESS) {
                primary = delivery->report.persistence;
            }
        }
    }
    *report = delivery->report;
    return primary;
}

nx_result_t
example_gateway_delivery_run(example_gateway_delivery_t* delivery,
                             example_outbox_t* outbox,
                             const nx_flashdb_operation_t* operation,
                             const example_gateway_config_t* config,
                             example_gateway_delivery_report_t* report) {
    if (delivery == NULL || outbox == NULL || operation == NULL ||
        config == NULL || report == NULL ||
        operation->database != outbox->database ||
        operation->database == NULL || !operation->database->opened ||
        operation->database->kind != NX_FLASHDB_KV ||
        operation->deadline == NX_DEADLINE_NEVER ||
        overlap(delivery, sizeof(*delivery), outbox, sizeof(*outbox)) ||
        overlap(delivery, sizeof(*delivery), operation, sizeof(*operation)) ||
        overlap(delivery, sizeof(*delivery), config, sizeof(*config)) ||
        overlap(delivery, sizeof(*delivery), report, sizeof(*report)) ||
        overlap(outbox, sizeof(*outbox), report, sizeof(*report))) {
        return NX_ERROR_INVALID;
    }
    if (delivery->active) {
        return NX_ERROR_STATE;
    }
    size_t bytes = sizeof(delivery->wire) - 8U;
    uint64_t identifier;
    nx_result_t result =
        example_outbox_read(outbox, &identifier, delivery->wire + 8U, &bytes);
    if (result != NX_SUCCESS) {
        return result;
    }
    result = nx_endian_write(delivery->wire, 8U, true, identifier);
    if (result != NX_SUCCESS) {
        return result;
    }
    delivery->outbox = outbox;
    delivery->report = (example_gateway_delivery_report_t){
        .network = {.local_sessions_released = true}, .identifier = identifier};
    delivery->active = true;
    example_gateway_config_t selected = *config;
    selected.payload = delivery->wire;
    selected.payload_bytes = bytes + 8U;
    result = example_gateway_run(&delivery->application, &selected,
                                 &delivery->report.network);
    return finish(delivery, operation, result, report);
}

nx_result_t
example_gateway_delivery_cleanup(example_gateway_delivery_t* delivery,
                                 const nx_flashdb_operation_t* operation,
                                 nx_time_us_t deadline,
                                 example_gateway_delivery_report_t* report) {
    if (delivery == NULL || operation == NULL || report == NULL ||
        deadline == NX_DEADLINE_NEVER || delivery->outbox == NULL ||
        operation->database != delivery->outbox->database ||
        operation->deadline == NX_DEADLINE_NEVER ||
        overlap(delivery, sizeof(*delivery), operation, sizeof(*operation)) ||
        overlap(delivery, sizeof(*delivery), report, sizeof(*report))) {
        return NX_ERROR_INVALID;
    }
    if (!delivery->active) {
        return NX_ERROR_STATE;
    }
    nx_result_t result = example_gateway_cleanup(
        &delivery->application, deadline, &delivery->report.network);
    return finish(delivery, operation, result, report);
}

nx_result_t example_gateway_credentials_try(
    example_gateway_application_t* application,
    const example_gateway_config_t* config,
    const nx_flashdb_operation_t* operation, uint64_t candidate,
    example_gateway_report_t* report, bool* selected) {
    if (application == NULL || config == NULL || operation == NULL ||
        operation->database == NULL || report == NULL || selected == NULL ||
        candidate == 0U || operation->deadline == NX_DEADLINE_NEVER ||
        !operation->database->opened ||
        operation->database->kind != NX_FLASHDB_KV ||
        overlap(application, sizeof(*application), operation,
                sizeof(*operation)) ||
        overlap(application, sizeof(*application), operation->database,
                sizeof(*operation->database)) ||
        overlap(report, sizeof(*report), operation, sizeof(*operation)) ||
        overlap(report, sizeof(*report), operation->database,
                sizeof(*operation->database)) ||
        overlap(application, sizeof(*application), selected,
                sizeof(*selected)) ||
        overlap(config, sizeof(*config), selected, sizeof(*selected)) ||
        overlap(operation, sizeof(*operation), selected, sizeof(*selected)) ||
        overlap(operation->database, sizeof(*operation->database), selected,
                sizeof(*selected)) ||
        overlap(report, sizeof(*report), selected, sizeof(*selected))) {
        return NX_ERROR_INVALID;
    }
    *selected = false;
    uint8_t image[NX_PERSISTENT_RECORD_HEADER_BYTES];
    size_t bytes = sizeof(image);
    nx_result_t result = nx_persistent_record_encode(
        UINT32_C(0x43524531), candidate, NULL, 0U, image, &bytes);
    if (result == NX_SUCCESS) {
        result = example_gateway_run(application, config, report);
    }
    if (result != NX_SUCCESS) {
        return result;
    }
    if (!report->authenticated || !report->local_sessions_released) {
        return NX_ERROR_STATE;
    }
    result = nx_flashdb_kv_set(operation, "tls_active", image, bytes);
    *selected = result == NX_SUCCESS;
    return result;
}
