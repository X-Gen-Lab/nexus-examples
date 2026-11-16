/**
 * \file            host_peer.h
 * \brief           Explicit Native raw-TCP peer for the gateway broker relay
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-02
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#ifndef NEXUS_EXAMPLE_GATEWAY_HOST_PEER_H
#define NEXUS_EXAMPLE_GATEWAY_HOST_PEER_H
#include "lwip/tcp.h"
#include "native_frames.h"
#include "nexus/network/lwip_storage.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    EXAMPLE_GATEWAY_PEER_RESET = 1,
    EXAMPLE_GATEWAY_PEER_BLACKHOLE = 2,
    EXAMPLE_GATEWAY_PEER_RESTORE = 3,
    EXAMPLE_GATEWAY_PEER_STOP = 4
} example_gateway_peer_command_t;

/** \brief One exact host-only software network, never a PHY description. */
typedef struct {
    int frame_descriptor;
    int control_descriptor;
    uint16_t broker_port;
    nx_time_us_t deadline;
    nx_time_us_t pause_us;
    size_t relay_budget;
    uint32_t turn_limit;
} example_gateway_peer_config_t;

/** \brief Copied IPC/TCP facts; local detach does not prove remote delivery. */
typedef struct {
    uint64_t client_to_broker_bytes;
    uint64_t broker_to_client_bytes;
    uint64_t received_frames;
    uint32_t connections;
    uint32_t resets;
    uint32_t paused_rounds;
    uint32_t turns;
    bool local_resources_released;
} example_gateway_peer_report_t;

/**
 * \brief Exact tool storage for the selected NO_SYS raw-TCP profile.
 * \note Caller storage is static and remains alive through child process exit,
 *       including detached FIN/TIME_WAIT/global timers. The peer owns only its
 *       newly opened loopback broker socket; frame/control fds are borrowed.
 *       One owner performs all raw calls/callbacks. There is one active relay,
 *       one exact TCP_WND client-to-broker buffer and one 2048-byte reverse
 *       buffer. The application initializes lwIP once before this tool runs.
 */
typedef struct {
    example_gateway_peer_config_t config;
    example_gateway_peer_report_t report;
    example_gateway_native_frames_t frames;
    nx_lwip_adapter_t adapter;
    nx_eth_fragment_t receive_view;
    uint8_t transmit[NX_ETH_FRAME_MAX];
    struct tcp_pcb* listener;
    struct tcp_pcb* client;
    int broker_descriptor;
    uint8_t to_broker[TCP_WND];
    size_t to_broker_size;
    size_t to_broker_offset;
    uint8_t to_client[2048];
    size_t to_client_size;
    size_t to_client_offset;
    nx_time_us_t pause_until;
    bool initialized;
    bool broker_connecting;
    bool broker_eof;
    bool client_eof;
    bool broker_write_shutdown;
    bool client_write_shutdown;
    bool blackhole;
    bool stopping;
} example_gateway_peer_t;

/**
 * \brief Run the explicit host-tool relay in its caller's serialized context.
 * \param[in,out] peer: Zeroed static exact storage, retained until process
 * exit. \param[in] config: Fixed borrowed fds, loopback broker port and finite
 * budget. \param[out] report: Nonaliased final copied observations. \return
 * SUCCESS after explicit STOP and local detach, otherwise an error. \note This
 * tool neither starts a thread nor forks. Its outer Native main forks before
 * either process initializes lwIP or PSA. Control datagrams are one command
 * byte; replies are command|0x80 after execution. RESET aborts only the current
 * TCP flow; BLACKHOLE pauses payload relay while still progressing the stack;
 * RESTORE reopens it. STOP aborts the flow, closes the listener/owned broker fd
 * and withdraws the local adapter. Each EOF shuts down only its outgoing
 * direction after buffered bytes drain; the reverse direction stays open until
 * its own EOF. Payload is opaque ciphertext. No TLS or MQTT parsing, signing,
 * PHY, DMA or physical timing qualification is supplied by
 * this host tool.
 */
nx_result_t
example_gateway_peer_run(example_gateway_peer_t* peer,
                         const example_gateway_peer_config_t* config,
                         example_gateway_peer_report_t* report);
#ifdef __cplusplus
}
#endif
#endif /* NEXUS_EXAMPLE_GATEWAY_HOST_PEER_H */
