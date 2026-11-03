/**
 * \file            gateway_policy.h
 * \brief           Product retry and single-message policy for the gateway
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-02
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#ifndef NEXUS_EXAMPLE_GATEWAY_POLICY_H
#define NEXUS_EXAMPLE_GATEWAY_POLICY_H
#include "nexus/core/time.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EXAMPLE_GATEWAY_PAYLOAD_BYTES 96U
#define EXAMPLE_GATEWAY_MAX_ATTEMPTS  8U

typedef enum {
    EXAMPLE_GATEWAY_COLD = 0,
    EXAMPLE_GATEWAY_READY,
    EXAMPLE_GATEWAY_CONNECTING,
    EXAMPLE_GATEWAY_ONLINE,
    EXAMPLE_GATEWAY_DRAINING,
    EXAMPLE_GATEWAY_BACKOFF,
    EXAMPLE_GATEWAY_STOPPED,
    EXAMPLE_GATEWAY_FAILED
} example_gateway_state_t;

/** \brief One finite monotonic domain; overall deadline never advances. */
typedef struct {
    nx_time_us_t overall_deadline;
    nx_time_us_t attempt_budget_us;
    nx_time_us_t retry_delay_us;
    uint32_t attempts_limit;
} example_gateway_policy_config_t;

/** \brief Attempt identity copied into the synchronous MQTT event context. */
typedef struct {
    nx_time_us_t deadline;
    uint32_t session;
} example_gateway_attempt_t;

/**
 * \brief Exact application storage, exclusively owned by one task.
 * \note This policy performs no I/O and creates no worker or queue. State
 *       describes session admission only; it cannot establish MAC/global
 *       lwIP timer withdrawal. Those resources outlive the final session.
 *       Payload remains until a matching PUBACK or explicit product disposal
 *       after all users joined. Reconnection may duplicate an earlier message;
 *       its payload must carry a stable application idempotence identifier.
 */
typedef struct {
    example_gateway_policy_config_t config;
    example_gateway_state_t state;
    nx_result_t fault;
    nx_time_us_t last_time;
    nx_time_us_t attempt_deadline;
    nx_time_us_t next_attempt;
    uint32_t attempts;
    uint16_t packet_id;
    size_t payload_size;
    uint8_t payload[EXAMPLE_GATEWAY_PAYLOAD_BYTES];
    bool stop_requested;
} example_gateway_policy_t;

/**
 * \brief Initialize cold caller storage without admitting any network I/O.
 * \param[out] policy: Zeroed cold storage or explicitly stopped final owner.
 * \param[in] config: Finite future deadline, nonzero budgets, 1..8 attempts.
 * \param[in] now: Current finite monotonic microseconds in the fixed domain.
 * \return SUCCESS, INVALID or STATE; rejection preserves policy bytes.
 * \note Live policies cannot be rebound. A new run after STOPPED preserves
 *       the clock epoch and explicitly disposes its former unacknowledged
 *       record only after the product joined every observer and session.
 */
nx_result_t
example_gateway_policy_init(example_gateway_policy_t* policy,
                            const example_gateway_policy_config_t* config,
                            nx_time_us_t now);
/**
 * \brief Admit one attempt only after the previous session fully withdrew.
 * \param[in,out] policy: Initialized serialized task-owned policy.
 * \param[in] now: Same-domain finite time; cannot move backwards.
 * \param[out] attempt: Nonaliased output written only on successful admission.
 * \return SUCCESS, BUSY before backoff, STATE, TIMEOUT or EXHAUSTED.
 * \note A bounded attempt uses min(now + budget, overall deadline).
 *       Every admitted attempt must report failure/stop and actual cleanup.
 */
nx_result_t example_gateway_begin(example_gateway_policy_t* policy,
                                  nx_time_us_t now,
                                  example_gateway_attempt_t* attempt);
/**
 * \brief Observe verified TCP/TLS/MQTT establishment for the admitted attempt.
 * \param[in,out] policy: Current CONNECTING policy.
 * \param[in] now: Finite unchanged-domain time before both locked deadlines.
 * \return SUCCESS, STATE or TIMEOUT; failure does not release session storage.
 */
nx_result_t example_gateway_online(example_gateway_policy_t* policy,
                                   nx_time_us_t now);
/**
 * \brief Copy one telemetry record; never overwrite an unacknowledged record.
 * \param[in,out] policy: Active policy outside stop/drain/failure.
 * \param[in] payload: Nonaliased bytes carrying the product idempotence ID.
 * \param[in] bytes: 1..96 copied bytes, no pointer retained.
 * \return SUCCESS, INVALID, BUSY for an occupied slot, or STATE.
 */
nx_result_t example_gateway_queue(example_gateway_policy_t* policy,
                                  const uint8_t* payload, size_t bytes);
/**
 * \brief Correlate a successful local QoS1 publish without claiming delivery.
 * \param[in,out] policy: ONLINE policy with one copied unsent record.
 * \param[in] packet_id: Nonzero ID returned by the current MQTT publish.
 * \return SUCCESS, INVALID or STATE; payload remains retained.
 */
nx_result_t example_gateway_published(example_gateway_policy_t* policy,
                                      uint16_t packet_id);
/**
 * \brief Release only the record confirmed by this session's matching PUBACK.
 * \param[in,out] policy: ONLINE policy with a locally published record.
 * \param[in] session: Identity copied from the admitted attempt.
 * \param[in] packet_id: Matching nonzero packet identifier.
 * \return SUCCESS, INVALID or STATE; stale ACKs preserve the pending record.
 */
nx_result_t example_gateway_ack(example_gateway_policy_t* policy,
                                uint32_t session, uint16_t packet_id);
/**
 * \brief Close admission after an actual session fault; retain every loan.
 * \param[in,out] policy: CONNECTING or ONLINE policy.
 * \param[in] reason: Actual error, excluding SUCCESS and would-block BUSY.
 * \return SUCCESS after recording the first fault, INVALID or STATE.
 */
nx_result_t example_gateway_failed(example_gateway_policy_t* policy,
                                   nx_result_t reason);
/**
 * \brief Acknowledge actual joined MQTT/TLS/TCP cleanup before any retry.
 * \param[in,out] policy: DRAINING policy with persistent storage still live.
 * \param[in] cleanup: Actual aggregate cleanup result; BUSY/error retain state.
 * \param[in] now: Same-domain finite time; reversed time closes retries.
 * \return SUCCESS for released session, or actual cleanup/clock error.
 * \note SUCCESS releases local session admission only. It does not certify
 *       lower MAC, detached FIN/TIME_WAIT, global stack or clock withdrawal.
 */
nx_result_t example_gateway_cleaned(example_gateway_policy_t* policy,
                                    nx_result_t cleanup, nx_time_us_t now);
/**
 * \brief Request explicit shutdown without I/O or implicit message disposal.
 * \param[in,out] policy: Initialized serialized owner policy.
 * \return SUCCESS or INVALID; active sessions enter DRAINING until cleaned.
 */
nx_result_t example_gateway_stop(example_gateway_policy_t* policy);

#ifdef __cplusplus
}
#endif
#endif /* NEXUS_EXAMPLE_GATEWAY_POLICY_H */
