/**
 * \file            delivery_reference.c
 * \brief           One durable business record and explicit command freshness
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-22
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "delivery_reference.h"
#include "nexus/components/persistent_record.h"
#include <string.h>

#define OUTBOX_SCHEMA  UINT32_C(0x4f425831)
#define OUTBOX_PENDING 1U
#define OUTBOX_ACKED   2U
#define OUTBOX_IMAGE_BYTES                                                     \
    (NX_PERSISTENT_RECORD_HEADER_BYTES + 1U + EXAMPLE_OUTBOX_PAYLOAD_BYTES)

/** \brief Reject overflow as well as known aliases before copying state. */
static bool overlap(const void* a, size_t an, const void* b, size_t bn) {
    uintptr_t x = (uintptr_t)a;
    uintptr_t y = (uintptr_t)b;
    return an > UINTPTR_MAX - x || bn > UINTPTR_MAX - y ||
           (an != 0U && bn != 0U && x < y + bn && y < x + an);
}
/** \brief Keep all mutations in the original database and serialized scope. */
static bool scope_valid(const example_outbox_t* outbox,
                        const nx_flashdb_operation_t* op) {
    return outbox != NULL && op != NULL && op->database != NULL &&
           op->database->kind == NX_FLASHDB_KV &&
           op->deadline != NX_DEADLINE_NEVER &&
           !overlap(outbox, sizeof(*outbox), op, sizeof(*op)) &&
           !overlap(outbox, sizeof(*outbox), op->database,
                    sizeof(*op->database));
}
/** \brief A valid loaded state must retain the same actual database owner. */
static bool ready(const example_outbox_t* outbox,
                  const nx_flashdb_operation_t* op) {
    return outbox->loaded && !outbox->faulted && op->database->opened &&
           outbox->database == op->database;
}
/** \brief Encode state and payload in one independently committed KV value. */
static nx_result_t persist(example_outbox_t* outbox,
                           const nx_flashdb_operation_t* op,
                           uint64_t identifier, uint8_t status,
                           const void* payload, size_t bytes) {
    uint8_t body[1U + EXAMPLE_OUTBOX_PAYLOAD_BYTES];
    uint8_t image[OUTBOX_IMAGE_BYTES];
    body[0] = status;
    if (bytes != 0U) {
        memcpy(body + 1U, payload, bytes);
    }
    size_t image_bytes = sizeof(image);
    nx_result_t result = nx_persistent_record_encode(
        OUTBOX_SCHEMA, identifier, body, bytes + 1U, image, &image_bytes);
    if (result == NX_SUCCESS) {
        result = nx_flashdb_kv_set(op, "telemetry", image, image_bytes);
        if (result != NX_SUCCESS) {
            outbox->faulted = true;
        }
    }
    return result;
}

nx_result_t example_outbox_load(example_outbox_t* outbox,
                                const nx_flashdb_operation_t* op) {
    if (!scope_valid(outbox, op)) {
        return NX_ERROR_INVALID;
    }
    if (!op->database->opened) {
        return NX_ERROR_STATE;
    }
    uint8_t image[OUTBOX_IMAGE_BYTES];
    size_t image_bytes = sizeof(image);
    nx_result_t result =
        nx_flashdb_kv_get(op, "telemetry", image, &image_bytes);
    example_outbox_t next = {0};
    next.database = op->database;
    if (result == NX_ERROR_EMPTY) {
        next.loaded = true;
        *outbox = next;
        return NX_SUCCESS;
    }
    nx_persistent_record_view_t view = {0};
    if (result == NX_SUCCESS) {
        result = nx_persistent_record_decode(OUTBOX_SCHEMA, image, image_bytes,
                                             &view);
    }
    if (result == NX_SUCCESS) {
        if (view.payload_bytes == 0U ||
            (view.payload[0] == OUTBOX_PENDING && view.payload_bytes < 2U) ||
            (view.payload[0] == OUTBOX_ACKED && view.payload_bytes != 1U) ||
            (view.payload[0] != OUTBOX_PENDING &&
             view.payload[0] != OUTBOX_ACKED)) {
            result = NX_ERROR_IO;
        } else {
            next.identifier = view.identifier;
            next.pending = view.payload[0] == OUTBOX_PENDING;
            next.bytes = view.payload_bytes - 1U;
            memcpy(next.payload, view.payload + 1U, next.bytes);
            next.loaded = true;
        }
    }
    next.faulted = result != NX_SUCCESS;
    *outbox = next;
    return result;
}

nx_result_t example_outbox_enqueue(example_outbox_t* outbox,
                                   const nx_flashdb_operation_t* op,
                                   uint64_t identifier, const void* payload,
                                   size_t bytes) {
    if (!scope_valid(outbox, op) || identifier == 0U || payload == NULL ||
        bytes == 0U || bytes > EXAMPLE_OUTBOX_PAYLOAD_BYTES ||
        overlap(outbox, sizeof(*outbox), payload, bytes)) {
        return NX_ERROR_INVALID;
    }
    if (!ready(outbox, op)) {
        return NX_ERROR_STATE;
    }
    if (outbox->pending) {
        return NX_ERROR_BUSY;
    }
    if (identifier <= outbox->identifier) {
        return NX_ERROR_STATE;
    }
    nx_result_t result =
        persist(outbox, op, identifier, OUTBOX_PENDING, payload, bytes);
    if (result == NX_SUCCESS) {
        memcpy(outbox->payload, payload, bytes);
        outbox->bytes = bytes;
        outbox->identifier = identifier;
        outbox->pending = true;
    }
    return result;
}

nx_result_t example_outbox_read(const example_outbox_t* outbox,
                                uint64_t* identifier, void* payload,
                                size_t* bytes) {
    if (outbox == NULL || identifier == NULL || payload == NULL ||
        bytes == NULL ||
        overlap(outbox, sizeof(*outbox), bytes, sizeof(*bytes)) ||
        overlap(outbox, sizeof(*outbox), identifier, sizeof(*identifier)) ||
        overlap(bytes, sizeof(*bytes), identifier, sizeof(*identifier))) {
        return NX_ERROR_INVALID;
    }
    if (!outbox->loaded || outbox->faulted) {
        return NX_ERROR_STATE;
    }
    if (!outbox->pending) {
        return NX_ERROR_EMPTY;
    }
    if (overlap(outbox, sizeof(*outbox), payload, *bytes) ||
        overlap(bytes, sizeof(*bytes), payload, *bytes) ||
        overlap(identifier, sizeof(*identifier), payload, *bytes)) {
        return NX_ERROR_INVALID;
    }
    if (*bytes < outbox->bytes) {
        *bytes = outbox->bytes;
        return NX_ERROR_EXHAUSTED;
    }
    memcpy(payload, outbox->payload, outbox->bytes);
    *identifier = outbox->identifier;
    *bytes = outbox->bytes;
    return NX_SUCCESS;
}

nx_result_t example_outbox_acknowledge(example_outbox_t* outbox,
                                       const nx_flashdb_operation_t* op,
                                       uint64_t identifier) {
    if (!scope_valid(outbox, op) || identifier == 0U) {
        return NX_ERROR_INVALID;
    }
    if (!ready(outbox, op) || identifier != outbox->identifier) {
        return NX_ERROR_STATE;
    }
    if (!outbox->pending) {
        return NX_SUCCESS;
    }
    nx_result_t result =
        persist(outbox, op, identifier, OUTBOX_ACKED, NULL, 0U);
    if (result == NX_SUCCESS) {
        outbox->pending = false;
        outbox->bytes = 0U;
        memset(outbox->payload, 0, sizeof(outbox->payload));
    }
    return result;
}

nx_result_t example_command_gate_init(example_command_gate_t* gate,
                                      uint32_t epoch, size_t max_bytes) {
    if (gate == NULL || epoch == 0U || max_bytes == 0U) {
        return NX_ERROR_INVALID;
    }
    *gate = (example_command_gate_t){
        .epoch = epoch, .max_bytes = max_bytes, .initialized = true};
    return NX_SUCCESS;
}

nx_result_t example_command_admit(example_command_gate_t* gate,
                                  const example_command_t* command,
                                  nx_time_us_t now) {
    if (gate == NULL || command == NULL || command->payload == NULL ||
        command->bytes == 0U || command->bytes > gate->max_bytes ||
        command->sequence == 0U || now == NX_DEADLINE_NEVER ||
        command->valid_until == NX_DEADLINE_NEVER ||
        overlap(gate, sizeof(*gate), command, sizeof(*command)) ||
        overlap(gate, sizeof(*gate), command->payload, command->bytes) ||
        overlap(command, sizeof(*command), command->payload, command->bytes)) {
        return NX_ERROR_INVALID;
    }
    if (!gate->initialized || command->epoch != gate->epoch ||
        command->sequence <= gate->last_sequence || now < gate->last_time) {
        return NX_ERROR_STATE;
    }
    if (now >= command->valid_until) {
        return NX_ERROR_TIMEOUT;
    }
    gate->last_sequence = command->sequence;
    gate->last_time = now;
    return NX_SUCCESS;
}
