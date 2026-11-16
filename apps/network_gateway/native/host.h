/**
 * \file            host.h
 * \brief           Private Native process owner for the gateway reference
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-16
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#ifndef NEXUS_EXAMPLE_GATEWAY_HOST_H
#define NEXUS_EXAMPLE_GATEWAY_HOST_H
/* POSIX types precede lwIP compatibility aliases in every Native consumer. */
#include <stdalign.h>
#include <sys/types.h>

#include "../gateway_application.h"
#include "host_peer.h"
#include "nexus/mqtt/core_mqtt_storage.h"
#include "nexus/network/lwip_tcp_storage.h"
#include "nexus/tls/storage.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CASE_CLEAN,
    CASE_BACKPRESSURE,
    CASE_FAULT,
    CASE_DEADLINE,
    CASE_MISSING_CLIENT
} gateway_case_t;

/* Both stacks, their detached FIN/TIME_WAIT and their clocks remain alive
 * through process exit. No storage is relocated after admission. */
typedef struct {
    example_gateway_application_t application;
    example_gateway_report_t report;
    example_gateway_native_frames_t frames;
    nx_lwip_adapter_t network;
    nx_eth_fragment_t receive_view;
    uint8_t transmit[NX_ETH_FRAME_MAX];
    nx_lwip_tcp_t tcp;
    nx_tls_runtime_t crypto;
    nx_tls_session_t tls;
    alignas(max_align_t) uint8_t arena[NX_TLS_ARENA_BYTES];
    uint8_t stage[NX_TLS_TX_STAGE_BYTES];
    nx_mqtt_t mqtt;
    uint8_t mqtt_buffer[1024];
    MQTTPubAckInfo_t outgoing[2];
    MQTTPubAckInfo_t incoming[2];
    nx_mqtt_control_t controls[2];
    nx_mqtt_memory_t mqtt_memory;
    uint8_t ca[NX_TLS_DER_MAX];
    uint8_t certificate[NX_TLS_DER_MAX];
    uint8_t key[NX_TLS_DER_MAX];
    size_t ca_bytes;
    size_t certificate_bytes;
    size_t key_bytes;
    example_gateway_peer_report_t peer_report;
    pid_t peer;
    int frame_descriptor;
    int control_descriptor;
    gateway_case_t selected;
    nx_result_t control_error;
    bool injected;
    bool network_live;
    bool crypto_live;
    bool peer_report_valid;
    bool peer_joined;
} example_gateway_host_t;

/**
 * \brief Establish this process's waitable-child policy and start its peer.
 * \param[in,out] state: Zeroed caller-owned Native storage, used by one owner.
 * \param[in] broker_port: Explicit loopback broker port.
 * \return SUCCESS after fork, otherwise IO without retaining new descriptors.
 * \note The process owns SIGCHLD reaping through join. No concurrent waiter,
 *       thread or signal handler may reap this child. Both stacks start after
 *       fork; retained stack storage remains alive through process exit.
 */
nx_result_t example_gateway_host_start_peer(example_gateway_host_t* state,
                                            uint16_t broker_port);
/**
 * \brief Stop and reap the peer created by this serialized process owner.
 * \param[in,out] state: Retained peer PID, control descriptor and report
 * storage. \return SUCCESS only after clean exit and a copied local-release
 * report; IO after forced withdrawal, ECHILD or an invalid release report.
 * \note A reaped or auto-reaped PID is never signaled. This call does not
 *       release the parent network or its borrowed frame descriptor.
 */
nx_result_t example_gateway_host_join(example_gateway_host_t* state);
/**
 * \brief Compose one bounded job from the selected Native fixture case.
 * \param[in,out] state: Serialized exact storage with live network and crypto.
 * \return Application result; borrowed sessions retain their actual lifetime.
 * \note Case budgets use the host monotonic domain and never create a worker.
 */
nx_result_t example_gateway_host_run_job(example_gateway_host_t* state);
/**
 * \brief Run the Native command-line composition with caller-owned storage.
 * \param[in,out] state: Zeroed static storage retained through process exit.
 * \param[in] argc: Argument count including the program name.
 * \param[in] argv: Program, loopback port, fixture case and DER directory.
 * \return Zero only for the expected case and complete local release; 2 for
 *         invalid arguments or 1 after startup, execution or cleanup failure.
 * \note One run per process owns its signal policy, lwIP and PSA domains. A
 *       failed network stop retains its borrowed descriptor through exit.
 */
int example_gateway_host_run(example_gateway_host_t* state, int argc,
                             char** argv);
#ifdef __cplusplus
}
#endif
#endif /* NEXUS_EXAMPLE_GATEWAY_HOST_H */
