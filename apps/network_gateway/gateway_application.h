/**
 * \file            gateway_application.h
 * \brief           One product-owned TCP/TLS/MQTT telemetry workflow
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-02
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#ifndef NEXUS_EXAMPLE_GATEWAY_APPLICATION_H
#define NEXUS_EXAMPLE_GATEWAY_APPLICATION_H
#include "gateway_policy.h"
#include "nexus/io/watchdog.h"
#include "nexus/mqtt/client.h"
#include "nexus/network/lwip_tcp.h"
#include "nexus/tls/tls.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \brief Explicit borrowed resources and finite budgets for one gateway run.
 * \note One serialized NO_SYS owner calls this application; no task, socket,
 *       stack, entropy domain, worker or platform is created here. Start the
 *       network and crypto domain first. All pointers outlive the call and any
 *       retained cleanup. TCP/TLS/MQTT storage must be cold and exclusively
 *       owned. TLS transport must be NULL; this application connects it to the
 *       actual admitted TCP face. Credentials and UTC policy stay external.
 *       Wait is bounded, nonreentrant and uses the same monotonic clock; it
 *       cannot extend the supplied deadline. Stop is an optional bounded
 *       product predicate, not a cross-task storage-reclamation permission.
 *       The optional watchdog is already enabled and exclusively handed off;
 *       its reported minimum must cover this entire run and cleanup budget.
 */
typedef struct {
    nx_lwip_tcp_t* tcp;
    nx_tls_session_t* tls;
    nx_mqtt_t* mqtt;
    const nx_mqtt_memory_t* mqtt_memory;
    nx_lwip_tcp_config_t tcp_config;
    nx_tls_config_t tls_config;
    nx_mqtt_connect_t identity;
    nx_clock_t clock;
    nx_result_t (*wait)(void* context, nx_time_us_t deadline);
    bool (*stop)(void* context);
    void* wait_context;
    const nx_watchdog_port_t* watchdog;
    const char* topic;
    size_t topic_bytes;
    const uint8_t* payload;
    size_t payload_bytes;
    example_gateway_policy_config_t policy;
    nx_time_us_t cleanup_budget_us;
    uint32_t mqtt_io_budget;
    uint32_t progress_turns;
} example_gateway_config_t;

/** \brief Copied observations; publication and eventual delivery differ. */
typedef struct {
    nx_result_t primary;
    nx_result_t cleanup;
    nx_result_t first_attempt_failure;
    uint32_t attempts;
    uint32_t turns;
    uint32_t mqtt_events;
    uint32_t watchdog_feeds;
    nx_tls_diagnostic_t tls_before_cleanup;
    bool tls_snapshot_valid;
    bool authenticated;
    bool delivered;
    bool echoed;
    bool graceful_close;
    bool local_sessions_released;
} example_gateway_report_t;

/**
 * \brief Exact product storage, alive through every retained cleanup.
 * \note Fields are owned by the application functions. The policy's payload
 *       is the sole semantic retry record within one job. That slot is never
 *       replaced before a matching PUBACK during this job's retries. A new run
 *       after all local users joined is an explicit new job and disposes the
 *       former RAM record. There is no persistent outbox promise. Reconnect
 *       uses clean MQTT sessions and may duplicate a delivered record; the
 *       external consumer owns idempotence. Local session cleanup never stops
 *       the shared network or crypto domain.
 */
typedef struct {
    example_gateway_config_t config;
    example_gateway_policy_t policy;
    example_gateway_attempt_t attempt;
    example_gateway_report_t report;
    uint32_t remaining_turns;
    uint16_t subscribed;
    uint16_t published;
    uint16_t unsubscribed;
    size_t expected_payload_bytes;
    bool running;
    bool tcp_owned;
    bool tls_owned;
    bool mqtt_owned;
    bool event_failed;
} example_gateway_application_t;

/**
 * \brief Run one finite copied telemetry job using direct public Nexus APIs.
 * \param[in,out] application: Zeroed or fully cleaned caller-owned metadata.
 * \param[in] config: Stable resources and fixed same-clock deadline/budgets.
 * \param[out] report: Nonaliased final observations, no concurrent readers.
 * \return SUCCESS after confirmed telemetry and local cleanup; otherwise the
 *         actual primary/cleanup error. local_sessions_released distinguishes
 *         a BUSY admission refusal from BUSY retaining an owned session.
 * \note Rejected static configuration fields admit no network I/O. Runtime
 *       supplier parsing and transport failures may require cleanup. Each
 *       retry follows actual
 *       MQTT deinit, TLS abort and TCP withdrawal. No hidden replay, runtime
 *       registry, allocation, worker or scheduler is added. A finite turn
 *       budget also bounds nonprogressing clocks and always-ready waits.
 *       Monotonic unchanged time is a product precondition. The guard checks
 *       phase-baseline regression and finite deadlines; it is not a complete
 *       per-read clock rollback or epoch detector.
 *       Network/MAC, detached FIN/TIME_WAIT, global lwIP timers, PSA runtime
 *       and their clock domains remain the external product's responsibility.
 */
nx_result_t example_gateway_run(example_gateway_application_t* application,
                                const example_gateway_config_t* config,
                                example_gateway_report_t* report);
/**
 * \brief Retry explicit local cleanup after all synchronous users have joined.
 * \param[in,out] application: Persistent metadata from the original run.
 * \param[in] deadline: Finite same-clock cleanup deadline, never an I/O replay.
 * \param[out] report: Nonaliased copied observations, written after the call.
 * \return SUCCESS when all local borrows ended, BUSY while retained, or error.
 * \note Existing TLS/TCP operation deadlines are never extended. A failed or
 *       busy upper-layer cleanup prevents reclaiming its lower transport.
 *       Confirmed delivery remains a fact even if later cleanup fails.
 */
nx_result_t example_gateway_cleanup(example_gateway_application_t* application,
                                    nx_time_us_t deadline,
                                    example_gateway_report_t* report);

#ifdef __cplusplus
}
#endif
#endif /* NEXUS_EXAMPLE_GATEWAY_APPLICATION_H */
