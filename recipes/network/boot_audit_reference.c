/**
 * \file            boot_audit_reference.c
 * \brief           Durable update intention and actual authenticated outcome
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-22
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "boot_audit_reference.h"
#include "nexus/algorithms/endian.h"
#include "nexus/components/persistent_record.h"
#include <string.h>

#define AUDIT_SCHEMA      UINT32_C(0x55424431)
#define AUDIT_BODY_BYTES  12U
#define AUDIT_IMAGE_BYTES (NX_PERSISTENT_RECORD_HEADER_BYTES + AUDIT_BODY_BYTES)

/** \brief Read one checked product record without retaining a vendor pointer.
 */
static nx_result_t read_record(const nx_flashdb_operation_t* operation,
                               const char* key, uint8_t* image, size_t* bytes,
                               nx_persistent_record_view_t* view) {
    nx_result_t result = nx_flashdb_kv_get(operation, key, image, bytes);
    return result == NX_SUCCESS
               ? nx_persistent_record_decode(AUDIT_SCHEMA, image, *bytes, view)
               : result;
}
/** \brief Skip identical durable records and preserve unknown old formats. */
static nx_result_t write_record(const nx_flashdb_operation_t* operation,
                                const char* key, uint64_t identifier,
                                const void* body, size_t bytes) {
    uint8_t image[AUDIT_IMAGE_BYTES];
    size_t image_bytes = sizeof(image);
    nx_result_t result = nx_persistent_record_encode(
        AUDIT_SCHEMA, identifier, body, bytes, image, &image_bytes);
    if (result != NX_SUCCESS) {
        return result;
    }
    uint8_t prior[AUDIT_IMAGE_BYTES];
    size_t prior_bytes = sizeof(prior);
    nx_persistent_record_view_t view;
    result = read_record(operation, key, prior, &prior_bytes, &view);
    if (result != NX_SUCCESS && result != NX_ERROR_EMPTY) {
        return result;
    }
    if (result == NX_SUCCESS) {
        if (view.payload_bytes != bytes) {
            return NX_ERROR_IO;
        }
        if (prior_bytes == image_bytes &&
            memcmp(prior, image, image_bytes) == 0) {
            return NX_SUCCESS;
        }
    }
    return nx_flashdb_kv_set(operation, key, image, image_bytes);
}

nx_result_t example_update_track(const nx_flashdb_operation_t* operation,
                                 uint64_t identifier) {
    if (identifier == 0U) {
        return NX_ERROR_INVALID;
    }
    return write_record(operation, "update_expected", identifier, NULL, 0U);
}

nx_result_t example_update_audit_boot(const nx_flashdb_operation_t* operation,
                                      const example_boot_audit_ports_t* ports,
                                      example_boot_audit_report_t* report) {
    if (operation == NULL || ports == NULL || report == NULL ||
        operation->deadline == NX_DEADLINE_NEVER ||
        ports->authenticate == NULL || ports->image_bytes == 0U ||
        ports->image_bytes > ports->running.size ||
        nx_flash_region_validate(&ports->running) != NX_SUCCESS) {
        return NX_ERROR_INVALID;
    }
    uint8_t image[AUDIT_IMAGE_BYTES];
    size_t image_bytes = sizeof(image);
    nx_persistent_record_view_t view;
    nx_result_t result =
        read_record(operation, "update_expected", image, &image_bytes, &view);
    if (result != NX_SUCCESS) {
        return result;
    }
    if (view.payload_bytes != 0U) {
        return NX_ERROR_IO;
    }
    example_boot_audit_report_t next = {.expected = view.identifier};
    next.authentication =
        ports->authenticate(ports->context, &ports->running, ports->image_bytes,
                            operation->deadline, &next.observed);
    if (next.authentication == NX_ERROR_BUSY) {
        return NX_ERROR_BUSY;
    }
    if (next.authentication == NX_SUCCESS && next.observed == 0U) {
        return NX_ERROR_STATE;
    }
    if (next.authentication != NX_SUCCESS) {
        next.observed = 0U;
    }
    next.matches =
        next.authentication == NX_SUCCESS && next.expected == next.observed;
    uint8_t body[AUDIT_BODY_BYTES];
    result = nx_endian_write(body, 4U, true, (uint32_t)next.authentication);
    if (result == NX_SUCCESS) {
        result = nx_endian_write(body + 4U, 8U, true, next.observed);
    }
    if (result == NX_SUCCESS) {
        result = write_record(operation, "update_outcome", next.expected, body,
                              sizeof(body));
    }
    next.persisted = result == NX_SUCCESS;
    *report = next;
    return result;
}

nx_result_t example_update_boot_report(const nx_flashdb_operation_t* operation,
                                       example_boot_audit_report_t* report) {
    if (report == NULL) {
        return NX_ERROR_INVALID;
    }
    uint8_t image[AUDIT_IMAGE_BYTES];
    size_t image_bytes = sizeof(image);
    nx_persistent_record_view_t view;
    nx_result_t result =
        read_record(operation, "update_outcome", image, &image_bytes, &view);
    if (result != NX_SUCCESS) {
        return result;
    }
    if (view.payload_bytes != AUDIT_BODY_BYTES) {
        return NX_ERROR_IO;
    }
    uint64_t authentication;
    uint64_t observed;
    result = nx_endian_read(view.payload, 4U, true, &authentication);
    if (result == NX_SUCCESS) {
        result = nx_endian_read(view.payload + 4U, 8U, true, &observed);
    }
    if (result != NX_SUCCESS || authentication > NX_ERROR_CONTEXT ||
        (authentication == NX_SUCCESS && observed == 0U) ||
        (authentication != NX_SUCCESS && observed != 0U)) {
        return NX_ERROR_IO;
    }
    *report = (example_boot_audit_report_t){
        .expected = view.identifier,
        .observed = observed,
        .authentication = (nx_result_t)authentication,
        .matches = authentication == NX_SUCCESS && view.identifier == observed,
        .persisted = true};
    return NX_SUCCESS;
}
