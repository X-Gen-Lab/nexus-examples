/**
 * \file            host_peer.c
 * \brief           Bounded raw-TCP ciphertext relay for an explicit host child
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-02
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#define _POSIX_C_SOURCE 200809L
/* POSIX declarations precede lwIP's optional byte-order aliases. */
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "host_peer.h"
#include "native_runtime.h"

static bool overlap(const void* a, size_t a_bytes, const void* b,
                    size_t b_bytes) {
    const uintptr_t first = (uintptr_t)a;
    const uintptr_t second = (uintptr_t)b;
    return first <= second ? second - first < a_bytes
                           : first - second < b_bytes;
}
static void close_broker(example_gateway_peer_t* peer) {
    if (peer->broker_descriptor >= 0) {
        (void)close(peer->broker_descriptor);
        peer->broker_descriptor = -1;
    }
    peer->broker_connecting = false;
    peer->broker_eof = false;
    peer->client_eof = false;
    peer->broker_write_shutdown = false;
    peer->client_write_shutdown = false;
    peer->to_broker_size = peer->to_broker_offset = 0U;
    peer->to_client_size = peer->to_client_offset = 0U;
}
static void detach(struct tcp_pcb* pcb) {
    tcp_arg(pcb, NULL);
    tcp_recv(pcb, NULL);
    tcp_sent(pcb, NULL);
    tcp_err(pcb, NULL);
}
static void abort_flow(example_gateway_peer_t* peer) {
    struct tcp_pcb* pcb = peer->client;
    peer->client = NULL;
    if (pcb != NULL) {
        detach(pcb);
        tcp_abort(pcb);
    }
    close_broker(peer);
}
static void flow_error(void* context, err_t error) {
    example_gateway_peer_t* peer = context;
    (void)error;
    /* The official stack already freed this PCB before invoking its error
     * callback. Clear metadata, never inspect or abort the former pointer. */
    peer->client = NULL;
    close_broker(peer);
}
static err_t flow_receive(void* context, struct tcp_pcb* pcb,
                          struct pbuf* packet, err_t error) {
    example_gateway_peer_t* peer = context;
    if (error != ERR_OK || pcb != peer->client) {
        return error != ERR_OK ? error : ERR_ARG;
    }
    if (packet == NULL) {
        peer->client_eof = true;
        return ERR_OK;
    }
    if (peer->to_broker_offset != 0U) {
        const size_t retained = peer->to_broker_size - peer->to_broker_offset;
        memmove(peer->to_broker, peer->to_broker + peer->to_broker_offset,
                retained);
        peer->to_broker_size = retained;
        peer->to_broker_offset = 0U;
    }
    if ((size_t)packet->tot_len >
        sizeof(peer->to_broker) - peer->to_broker_size) {
        /* ERR_MEM leaves the complete pbuf in the official refused-data path.
         * No new reference or receive-window credit is retained here. */
        return ERR_MEM;
    }
    const u16_t bytes = packet->tot_len;
    if (pbuf_copy_partial(packet, peer->to_broker + peer->to_broker_size, bytes,
                          0U) != bytes) {
        return ERR_BUF;
    }
    peer->to_broker_size += bytes;
    pbuf_free(packet);
    return ERR_OK;
}
static bool open_broker(example_gateway_peer_t* peer) {
    const int descriptor = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (descriptor < 0) {
        return false;
    }
    const nx_time_us_t now = example_gateway_host_now(NULL);
    if (now == NX_DEADLINE_NEVER || now >= peer->config.deadline) {
        (void)close(descriptor);
        return false;
    }
    const struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_port = htons(peer->config.broker_port),
        .sin_addr = {.s_addr = htonl(INADDR_LOOPBACK)}};
    const int result =
        connect(descriptor, (const struct sockaddr*)&address, sizeof(address));
    if (result != 0 && errno != EINPROGRESS) {
        (void)close(descriptor);
        return false;
    }
    peer->broker_descriptor = descriptor;
    peer->broker_connecting = result != 0;
    return true;
}
static err_t flow_accept(void* context, struct tcp_pcb* pcb, err_t error) {
    example_gateway_peer_t* peer = context;
    if (error != ERR_OK) {
        return error;
    }
    if (peer->client != NULL || peer->stopping ||
        example_gateway_host_now(NULL) >= peer->config.deadline) {
        tcp_abort(pcb);
        return ERR_ABRT;
    }
    ++peer->report.connections;
    peer->client = pcb;
    tcp_arg(pcb, peer);
    tcp_recv(pcb, flow_receive);
    tcp_err(pcb, flow_error);
    tcp_nagle_disable(pcb);
    if (!open_broker(peer)) {
        abort_flow(peer);
        return ERR_ABRT;
    }
    const nx_time_us_t now = example_gateway_host_now(NULL);
    if (now == NX_DEADLINE_NEVER || now >= peer->config.deadline) {
        abort_flow(peer);
        return ERR_ABRT;
    }
    const nx_time_us_t remaining = peer->config.deadline - now;
    peer->pause_until =
        now +
        (peer->config.pause_us < remaining ? peer->config.pause_us : remaining);
    return ERR_OK;
}
static nx_result_t progress_connect(example_gateway_peer_t* peer) {
    if (!peer->broker_connecting) {
        return NX_SUCCESS;
    }
    struct pollfd event = {.fd = peer->broker_descriptor, .events = POLLOUT};
    const int observed = poll(&event, 1U, 0);
    if (observed < 0) {
        return errno == EINTR ? NX_SUCCESS : NX_ERROR_IO;
    }
    if (observed == 0) {
        return NX_ERROR_BUSY;
    }
    int error = 0;
    socklen_t size = sizeof(error);
    if (getsockopt(peer->broker_descriptor, SOL_SOCKET, SO_ERROR, &error,
                   &size) != 0 ||
        error != 0) {
        abort_flow(peer);
        return NX_SUCCESS;
    }
    peer->broker_connecting = false;
    return NX_SUCCESS;
}
static void restore_callbacks(example_gateway_peer_t* peer,
                              struct tcp_pcb* pcb) {
    tcp_arg(pcb, peer);
    tcp_recv(pcb, flow_receive);
    tcp_err(pcb, flow_error);
}
static nx_result_t detach_closed_flow(example_gateway_peer_t* peer) {
    struct tcp_pcb* pcb = peer->client;
    detach(pcb);
    const err_t result = tcp_close(pcb);
    if (result != ERR_OK) {
        /* Failed close retains the PCB and callback ownership. A successful
         * close transfers copied TX/FIN state into the still-live stack. */
        restore_callbacks(peer, pcb);
        return result == ERR_MEM ? NX_ERROR_BUSY : NX_ERROR_IO;
    }
    peer->client = NULL;
    close_broker(peer);
    return NX_SUCCESS;
}
static nx_result_t relay(example_gateway_peer_t* peer) {
    if (peer->client == NULL) {
        return NX_SUCCESS;
    }
    nx_result_t result = progress_connect(peer);
    if (result != NX_SUCCESS || peer->client == NULL) {
        return result;
    }
    const nx_time_us_t now = example_gateway_host_now(NULL);
    if (now == NX_DEADLINE_NEVER || now >= peer->config.deadline) {
        return NX_ERROR_TIMEOUT;
    }
    if (peer->blackhole || now < peer->pause_until) {
        if (peer->to_broker_size != peer->to_broker_offset) {
            ++peer->report.paused_rounds;
        }
        return NX_SUCCESS;
    }
    if (peer->to_broker_offset < peer->to_broker_size) {
        size_t bytes = peer->to_broker_size - peer->to_broker_offset;
        if (bytes > peer->config.relay_budget) {
            bytes = peer->config.relay_budget;
        }
        const ssize_t sent = send(peer->broker_descriptor,
                                  peer->to_broker + peer->to_broker_offset,
                                  bytes, MSG_DONTWAIT | MSG_NOSIGNAL);
        if (sent > 0) {
            peer->to_broker_offset += (size_t)sent;
            peer->report.client_to_broker_bytes += (uint64_t)sent;
            tcp_recved(peer->client, (u16_t)sent);
        } else if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK &&
                   errno != EINTR) {
            abort_flow(peer);
            return NX_SUCCESS;
        }
    }
    if (peer->to_broker_offset == peer->to_broker_size) {
        peer->to_broker_offset = peer->to_broker_size = 0U;
        if (peer->client_eof && !peer->broker_write_shutdown) {
            if (shutdown(peer->broker_descriptor, SHUT_WR) == 0) {
                peer->broker_write_shutdown = true;
            } else if (errno == EINTR) {
                /* An interrupted observation proves no half-close. Keep the
                 * explicit operation retryable on a later bounded turn. */
                return NX_SUCCESS;
            } else {
                abort_flow(peer);
                return NX_SUCCESS;
            }
        }
    }
    if (peer->to_client_offset == peer->to_client_size && !peer->broker_eof) {
        const nx_time_us_t fresh = example_gateway_host_now(NULL);
        if (fresh == NX_DEADLINE_NEVER || fresh >= peer->config.deadline) {
            return NX_ERROR_TIMEOUT;
        }
        peer->to_client_offset = peer->to_client_size = 0U;
        size_t capacity = sizeof(peer->to_client);
        if (capacity > peer->config.relay_budget) {
            capacity = peer->config.relay_budget;
        }
        const ssize_t received = recv(peer->broker_descriptor, peer->to_client,
                                      capacity, MSG_DONTWAIT);
        if (received > 0) {
            peer->to_client_size = (size_t)received;
        } else if (received == 0) {
            peer->broker_eof = true;
        } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            abort_flow(peer);
            return NX_SUCCESS;
        }
    }
    if (peer->to_client_offset < peer->to_client_size) {
        const nx_time_us_t fresh = example_gateway_host_now(NULL);
        if (fresh == NX_DEADLINE_NEVER || fresh >= peer->config.deadline) {
            return NX_ERROR_TIMEOUT;
        }
        size_t bytes = peer->to_client_size - peer->to_client_offset;
        const size_t available = tcp_sndbuf(peer->client);
        if (bytes > available) {
            bytes = available;
        }
        if (bytes != 0U) {
            const err_t copied = tcp_write(
                peer->client, peer->to_client + peer->to_client_offset,
                (u16_t)bytes, TCP_WRITE_FLAG_COPY);
            if (copied == ERR_OK) {
                peer->to_client_offset += bytes;
                peer->report.broker_to_client_bytes += bytes;
            } else if (copied != ERR_MEM) {
                abort_flow(peer);
                return NX_SUCCESS;
            }
        }
    }
    if (peer->client != NULL) {
        const err_t output = tcp_output(peer->client);
        if (output != ERR_OK && output != ERR_MEM) {
            abort_flow(peer);
            return NX_SUCCESS;
        }
    }
    if (peer->client != NULL && peer->broker_eof &&
        peer->to_client_offset == peer->to_client_size) {
        if (!peer->client_write_shutdown) {
            /* Broker EOF closes only this direction. Late client bytes still
             * belong to the reverse stream until its independent EOF. */
            const err_t shut = tcp_shutdown(peer->client, 0, 1);
            if (shut != ERR_OK) {
                if (shut == ERR_MEM) {
                    return NX_ERROR_BUSY;
                }
                abort_flow(peer);
                return NX_SUCCESS;
            }
            peer->client_write_shutdown = true;
        }
        if (peer->client_eof &&
            peer->to_broker_offset == peer->to_broker_size) {
            return detach_closed_flow(peer);
        }
    }
    return NX_SUCCESS;
}
static nx_result_t reply(example_gateway_peer_t* peer, uint8_t command) {
    const uint8_t value = (uint8_t)(command | 0x80U);
    return send(peer->config.control_descriptor, &value, 1U,
                MSG_DONTWAIT | MSG_NOSIGNAL) == 1
               ? NX_SUCCESS
               : NX_ERROR_IO;
}
static nx_result_t control(example_gateway_peer_t* peer) {
    uint8_t command = 0U;
    const ssize_t bytes = recv(peer->config.control_descriptor, &command, 1U,
                               MSG_DONTWAIT | MSG_TRUNC);
    if (bytes < 0) {
        return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR
                   ? NX_SUCCESS
                   : NX_ERROR_IO;
    }
    if (bytes != 1) {
        return NX_ERROR_IO;
    }
    if (command == EXAMPLE_GATEWAY_PEER_RESET) {
        abort_flow(peer);
        ++peer->report.resets;
    } else if (command == EXAMPLE_GATEWAY_PEER_BLACKHOLE) {
        peer->blackhole = true;
    } else if (command == EXAMPLE_GATEWAY_PEER_RESTORE) {
        peer->blackhole = false;
    } else if (command == EXAMPLE_GATEWAY_PEER_STOP) {
        peer->stopping = true;
        return NX_SUCCESS;
    } else {
        return NX_ERROR_INVALID;
    }
    return reply(peer, command);
}
static bool control_descriptor_valid(int descriptor) {
    const int flags = fcntl(descriptor, F_GETFL);
    int kind = 0;
    socklen_t size = sizeof(kind);
    struct sockaddr_storage address = {0};
    socklen_t address_size = sizeof(address);
    return flags >= 0 && (flags & O_NONBLOCK) != 0 &&
           getsockopt(descriptor, SOL_SOCKET, SO_TYPE, &kind, &size) == 0 &&
           (kind == SOCK_SEQPACKET || kind == SOCK_DGRAM) &&
           getpeername(descriptor, (struct sockaddr*)&address, &address_size) ==
               0 &&
           address.ss_family == AF_UNIX;
}
static nx_result_t start_network(example_gateway_peer_t* peer) {
    nx_result_t result = example_gateway_native_frames_init(
        &peer->frames, peer->config.frame_descriptor,
        (nx_clock_t){example_gateway_host_now, NULL});
    if (result != NX_SUCCESS) {
        return result;
    }
    const nx_lwip_config_t config = {
        .port = &peer->frames.face,
        .clock = {example_gateway_host_now, NULL},
        .tx_timeout_us = 100000U,
        .tx_buffer = peer->transmit,
        .tx_capacity = sizeof(peer->transmit),
        .rx_views = &peer->receive_view,
        .rx_capacity = 1U,
        .hardware_address = {2U, 0U, 0U, 0U, 0U, 2U},
        .address = {192U, 0U, 2U, 2U},
        .netmask = {255U, 255U, 255U, 0U}};
    result = nx_lwip_adapter_start(&peer->adapter, &config);
    if (result == NX_SUCCESS) {
        const nx_eth_link_t link = {true, true, 100U};
        result = nx_lwip_adapter_apply_link(&peer->adapter, &link);
    }
    return result;
}
static nx_result_t start_listener(example_gateway_peer_t* peer) {
    struct tcp_pcb* pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (pcb == NULL) {
        return NX_ERROR_EXHAUSTED;
    }
    if (tcp_bind(pcb, IP_ANY_TYPE, 8883U) != ERR_OK) {
        tcp_abort(pcb);
        return NX_ERROR_IO;
    }
    err_t error = ERR_OK;
    struct tcp_pcb* listener = tcp_listen_with_backlog_and_err(pcb, 1U, &error);
    if (listener == NULL) {
        tcp_abort(pcb);
        return error == ERR_MEM ? NX_ERROR_EXHAUSTED : NX_ERROR_IO;
    }
    peer->listener = listener;
    tcp_arg(listener, peer);
    tcp_accept(listener, flow_accept);
    return NX_SUCCESS;
}
static nx_result_t cleanup(example_gateway_peer_t* peer) {
    abort_flow(peer);
    nx_result_t result = NX_SUCCESS;
    if (peer->listener != NULL) {
        struct tcp_pcb* listener = peer->listener;
        tcp_arg(listener, NULL);
        tcp_accept(listener, NULL);
        if (tcp_close(listener) == ERR_OK) {
            peer->listener = NULL;
        } else {
            tcp_arg(listener, peer);
            tcp_accept(listener, flow_accept);
            result = NX_ERROR_IO;
        }
    }
    if (result == NX_SUCCESS) {
        const nx_result_t stopped = nx_lwip_adapter_stop(&peer->adapter);
        if (stopped != NX_SUCCESS) {
            result = stopped;
        }
    }
    nx_eth_frames_state_t state = {0};
    const nx_result_t observed =
        nx_eth_frames_port_state(&peer->frames.face, &state);
    if (observed == NX_SUCCESS) {
        peer->report.received_frames = state.mac.received;
    }
    peer->report.local_resources_released =
        result == NX_SUCCESS && observed == NX_SUCCESS &&
        peer->client == NULL && peer->listener == NULL &&
        peer->broker_descriptor < 0 && !state.mac.started &&
        state.mac.pending_tx == 0U && state.mac.held_rx == 0U;
    return result;
}

nx_result_t
example_gateway_peer_run(example_gateway_peer_t* peer,
                         const example_gateway_peer_config_t* config,
                         example_gateway_peer_report_t* report) {
    if (peer == NULL || config == NULL || report == NULL ||
        overlap(peer, sizeof(*peer), config, sizeof(*config)) ||
        overlap(peer, sizeof(*peer), report, sizeof(*report)) ||
        overlap(config, sizeof(*config), report, sizeof(*report)) ||
        config->frame_descriptor < 0 || config->control_descriptor < 0 ||
        config->frame_descriptor == config->control_descriptor ||
        config->broker_port == 0U || config->relay_budget == 0U ||
        config->relay_budget > 2048U || config->turn_limit == 0U ||
        config->turn_limit > 100000U || config->deadline == NX_DEADLINE_NEVER ||
        config->pause_us == NX_DEADLINE_NEVER ||
        !control_descriptor_valid(config->control_descriptor)) {
        return NX_ERROR_INVALID;
    }
    const nx_time_us_t now = example_gateway_host_now(NULL);
    if (now == NX_DEADLINE_NEVER || now >= config->deadline) {
        return NX_ERROR_INVALID;
    }
    if (peer->initialized) {
        return NX_ERROR_STATE;
    }
    const example_gateway_peer_config_t copy = *config;
    memset(peer, 0, sizeof(*peer));
    peer->config = copy;
    peer->broker_descriptor = -1;
    peer->initialized = true;
    nx_result_t result = start_network(peer);
    if (result == NX_SUCCESS) {
        result = start_listener(peer);
    }
    while (result == NX_SUCCESS && !peer->stopping &&
           peer->report.turns < peer->config.turn_limit) {
        const nx_time_us_t current = example_gateway_host_now(NULL);
        if (current == NX_DEADLINE_NEVER || current >= peer->config.deadline) {
            result = NX_ERROR_TIMEOUT;
            break;
        }
        ++peer->report.turns;
        result = control(peer);
        if (result != NX_SUCCESS || peer->stopping) {
            break;
        }
        result = nx_lwip_adapter_service(&peer->adapter, 4U);
        if (result == NX_SUCCESS) {
            result = relay(peer);
            if (result == NX_ERROR_BUSY) {
                result = NX_SUCCESS;
            }
        }
        if (result == NX_SUCCESS) {
            result = example_gateway_host_wait(NULL, peer->config.deadline);
            if (result == NX_ERROR_BUSY) {
                result = NX_SUCCESS;
            }
        }
    }
    if (result == NX_SUCCESS && !peer->stopping) {
        result = NX_ERROR_EXHAUSTED;
    }
    const nx_result_t withdrawn = cleanup(peer);
    if (withdrawn != NX_SUCCESS) {
        result = withdrawn;
    }
    if (peer->stopping && withdrawn == NX_SUCCESS) {
        const nx_result_t acknowledged = reply(peer, EXAMPLE_GATEWAY_PEER_STOP);
        if (acknowledged != NX_SUCCESS) {
            result = acknowledged;
        }
    }
    *report = peer->report;
    return result;
}
