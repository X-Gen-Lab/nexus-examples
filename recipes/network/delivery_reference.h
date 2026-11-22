/**
 * \file            delivery_reference.h
 * \brief           Product-owned durable telemetry and remote admission
 * \author          Nexus Team
 */
#ifndef NEXUS_EXAMPLE_DELIVERY_REFERENCE_H
#define NEXUS_EXAMPLE_DELIVERY_REFERENCE_H
#include "nexus/components/flashdb.h"
#ifdef __cplusplus
extern "C" {
#endif
#define EXAMPLE_OUTBOX_PAYLOAD_BYTES 96U
/** \brief One copied record; fields private between serialized calls. */
typedef struct {
    nx_flashdb_t* database;
    uint64_t identifier;
    size_t bytes;
    uint8_t payload[EXAMPLE_OUTBOX_PAYLOAD_BYTES];
    bool pending;
    bool loaded;
    bool faulted;
} example_outbox_t;
/**
 * \brief Load the product's single-slot outbox from an already open KVDB.
 * \param[out] outbox: Unused/quiescent caller metadata, disjoint from database.
 * \param[in] operation: Exact active same-owner FlashDB scope and deadline.
 * \return SUCCESS after valid load or missing key; actual read/format error.
 * \note Task-only. Consumer supplies nx_flashdb_operation_get as usual. The
 *       fixed key "telemetry" and schema belong to this example. Existing
 *       corruption never becomes an empty queue. Reopen/reload follows actual
 *       physical recovery; no format, erase or replay occurs here.
 */
nx_result_t example_outbox_load(example_outbox_t* outbox,
                                const nx_flashdb_operation_t* operation);
/**
 * \brief Persist one copied record before it can be offered to a transport.
 * \param[in,out] outbox: Loaded same-owner metadata; no live readers.
 * \param[in] operation: Active scope for the original database.
 * \param[in] identifier: Nonzero business identity greater than the last ID.
 * \param[in] payload: Disjoint readable bytes, retained only until return.
 * \param[in] bytes: 1..96 bytes; include business identity in wire encoding.
 * \return SUCCESS, BUSY if pending, STATE for reused ID or uncertain media,
 *         INVALID or actual persistent error.
 * \note An uncertain mutation closes admission until recovery and load. IDs
 *       never wrap or reset on an ordinary reboot. Consumer enforces remote
 *       deduplication; a database ID alone provides no exactly-once delivery.
 */
nx_result_t example_outbox_enqueue(example_outbox_t* outbox,
                                   const nx_flashdb_operation_t* operation,
                                   uint64_t identifier, const void* payload,
                                   size_t bytes);
/**
 * \brief Copy the complete pending record without consuming it.
 * \param[in] outbox: Loaded healthy metadata in its serialized task domain.
 * \param[out] identifier: Disjoint business identity, unchanged on failure.
 * \param[out] payload: Disjoint caller storage, never retained.
 * \param[in,out] bytes: Input capacity; required size on SUCCESS/EXHAUSTED.
 * \return SUCCESS, EMPTY, EXHAUSTED, STATE or INVALID; no truncation success.
 * \note No I/O. Publication, retry and local cleanup do not acknowledge data.
 */
nx_result_t example_outbox_read(const example_outbox_t* outbox,
                                uint64_t* identifier, void* payload,
                                size_t* bytes);
/**
 * \brief Persist an observed remote ACK before making the slot reusable.
 * \param[in,out] outbox: Original loaded serialized metadata.
 * \param[in] operation: Exact active database scope and finite deadline.
 * \param[in] identifier: Exact pending business ID confirmed by the peer.
 * \return SUCCESS for durable ACK, STATE for stale ID/uncertain state, INVALID
 *         or actual persistent error. Same durable ACK is an I/O-free success.
 * \note Caller correlates an actual peer ACK with this ID; MQTT local send is
 *       insufficient. Failed ACK persistence may require retransmission after
 *       reload. Payload space is logically reclaimed on the next newer enqueue;
 *       the persisted ACK retains the high-water ID. This does not GC media or
 *       release any transport/session storage.
 */
nx_result_t example_outbox_acknowledge(example_outbox_t* outbox,
                                       const nx_flashdb_operation_t* operation,
                                       uint64_t identifier);
/** \brief Bounded copied metadata; payload is borrowed only during admission.
 */
typedef struct {
    uint32_t epoch;
    uint64_t sequence;
    nx_time_us_t valid_until;
    const uint8_t* payload;
    size_t bytes;
} example_command_t;
/** \brief Product admission state; initialize before publishing to callers. */
typedef struct {
    uint32_t epoch;
    uint64_t last_sequence;
    nx_time_us_t last_time;
    size_t max_bytes;
    bool initialized;
} example_command_gate_t;
/**
 * \brief Bind one product session and bounded command size without I/O.
 * \param[out] gate: Unused or joined caller storage.
 * \param[in] epoch: Nonzero session identity, not reused with stale senders.
 * \param[in] max_bytes: Positive explicit payload bound.
 * \return SUCCESS or INVALID, unchanged on invalid input.
 * \note One serialized task owns init/admit. Product authenticates the sender,
 *       validates its payload schema and supplies the live monotonic domain.
 */
nx_result_t example_command_gate_init(example_command_gate_t* gate,
                                      uint32_t epoch, size_t max_bytes);
/**
 * \brief Admit fresh command metadata after product authorization/validation.
 * \param[in,out] gate: Initialized exclusive state in the fixed session.
 * \param[in] command: Disjoint immutable descriptor and readable payload.
 * \param[in] now: Finite same-domain monotonic snapshot before valid_until.
 * \return SUCCESS, INVALID, STATE or TIMEOUT; rejection changes no gate state.
 * \note No pointer retained, worker, callback or physical output. Success means
 *       admission only; product copies/applies the command using its actual
 *       control port and preserves that operation's lifetime. Regression checks
 *       compare admitted snapshots, not every clock read or epoch transition.
 */
nx_result_t example_command_admit(example_command_gate_t* gate,
                                  const example_command_t* command,
                                  nx_time_us_t now);
#ifdef __cplusplus
}
#endif
#endif
