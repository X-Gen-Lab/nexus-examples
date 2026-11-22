/**
 * \file            gateway_delivery.h
 * \brief           Explicit durable outbox composition over the real gateway
 * \author          Nexus Team
 */
#ifndef NEXUS_EXAMPLE_GATEWAY_DELIVERY_H
#define NEXUS_EXAMPLE_GATEWAY_DELIVERY_H
#include "delivery_reference.h"
#include "gateway_application.h"
#ifdef __cplusplus
extern "C" {
#endif
/** \brief Transport observations and persistent ACK are independent facts. */
typedef struct {
    example_gateway_report_t network;
    uint64_t identifier;
    nx_result_t persistence;
    bool durable_ack;
} example_gateway_delivery_report_t;
/** \brief Exact caller storage; fields private throughout run and cleanup. */
typedef struct {
    example_gateway_application_t application;
    example_gateway_delivery_report_t report;
    example_outbox_t* outbox;
    uint8_t wire[EXAMPLE_GATEWAY_PAYLOAD_BYTES];
    bool active;
} example_gateway_delivery_t;
/**
 * \brief Publish one durable record through actual TCP/TLS/coreMQTT workflow.
 * \param[in,out] delivery: Zeroed or fully cleaned exclusive caller metadata.
 * \param[in,out] outbox: Loaded single-slot record, stable through cleanup.
 * \param[in] operation: Exact active outbox database scope for ACK persistence.
 * \param[in] config: Ordinary gateway resources/budgets; payload is replaced.
 * \param[out] report: Disjoint copied network and persistence observations.
 * \return Gateway/cleanup error first, otherwise actual ACK persistence error;
 *         EXHAUSTED before network I/O if payload exceeds 88 application bytes.
 * \note One serialized task. Wire payload is uint64 little-endian business ID
 *       followed by copied bytes. Consumer deduplicates that ID. Successful
 *       PUBACK is persisted only after local session users join; retained
 *       cleanup keeps delivery, wire, outbox and lower storage alive. Failure
 *       does not dispose the record or infer a safe Flash maintenance window.
 */
nx_result_t
example_gateway_delivery_run(example_gateway_delivery_t* delivery,
                             example_outbox_t* outbox,
                             const nx_flashdb_operation_t* operation,
                             const example_gateway_config_t* config,
                             example_gateway_delivery_report_t* report);
/**
 * \brief Complete retained gateway cleanup before committing an observed ACK.
 * \param[in,out] delivery: Original active metadata, every dependency live.
 * \param[in] operation: Same database's active finite ACK operation scope.
 * \param[in] deadline: Finite same-clock local cleanup deadline.
 * \param[out] report: Disjoint copied observations, unchanged on admission
 *                     failure.
 * \return Actual gateway cleanup or persistence error; BUSY retains storage.
 * \note No publish/replay occurs. Failed persistent ACK closes outbox admission
 *       until real recovery/reload; after transport release, delivery itself
 *       has no remaining network borrow. Persistence uses its separate explicit
 *       budget and does not renew any existing network deadline.
 */
nx_result_t
example_gateway_delivery_cleanup(example_gateway_delivery_t* delivery,
                                 const nx_flashdb_operation_t* operation,
                                 nx_time_us_t deadline,
                                 example_gateway_delivery_report_t* report);
/**
 * \brief Try a candidate credential profile before persisting its selector.
 * \param[in,out] application: Quiescent ordinary gateway storage, kept through
 *                           any retained cleanup.
 * \param[in] config: Explicit candidate credentials and ordinary finite job.
 * \param[in] operation: Active open KVDB scope for the independent selector.
 * \param[in] candidate: Nonzero product version identifying retained material.
 * \param[out] report: Disjoint actual gateway observations.
 * \param[out] selected: Disjoint flag, true only after selector write success.
 * \return Actual gateway/persistence error, or STATE if success lacks an
 *         authenticated and locally released session.
 * \note Same serialized task. Product authorizes candidate trust roots and
 *       stores both old and candidate material before this call. It retains
 *       both through failure/recovery; this writes only "tls_active" metadata,
 *       never erases a credential or replays a failed write. Partial selector
 *       persistence remains uncertain until database recovery/reload. This
 *       function uses the actual gateway workflow and adds no TLS owner.
 */
nx_result_t example_gateway_credentials_try(
    example_gateway_application_t* application,
    const example_gateway_config_t* config,
    const nx_flashdb_operation_t* operation, uint64_t candidate,
    example_gateway_report_t* report, bool* selected);
#ifdef __cplusplus
}
#endif
#endif
