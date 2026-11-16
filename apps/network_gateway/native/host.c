/**
 * \file            host.c
 * \brief           Serialized Native gateway process and resource lifecycle
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-02
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#define _POSIX_C_SOURCE 200809L
/* Include POSIX before the selected lwIP byte-order aliases. */
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <signal.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "host.h"
#include "host_peer.h"
#include "lwip/init.h"
#include "native_runtime.h"
#include "nexus/mqtt/core_mqtt_storage.h"
#include "nexus/network/lwip_tcp_storage.h"
#include "nexus/tls/storage.h"

static example_gateway_peer_t peer_storage;
static volatile sig_atomic_t interrupted;
static const nx_entropy_ops_t entropy_operations = {
    example_gateway_host_entropy};
/* This port has no mutable provider state; its explicit context is disjoint
 * from the crypto domain and satisfies the public entropy face contract. */
static uint8_t entropy_context;
static const nx_entropy_port_t entropy = {&entropy_operations,
                                          &entropy_context};

static void signal_stop(int number) {
    (void)number;
    interrupted = 1;
}

static nx_time_us_t after(nx_time_us_t interval) {
    const nx_time_us_t now = example_gateway_host_now(NULL);
    return now == NX_DEADLINE_NEVER || interval >= NX_DEADLINE_NEVER - now
               ? NX_DEADLINE_NEVER
               : now + interval;
}

static bool before(nx_time_us_t deadline) {
    const nx_time_us_t now = example_gateway_host_now(NULL);
    return now != NX_DEADLINE_NEVER && now < deadline;
}

/* The control plane never services the application or either stack. Its peer
 * is a separate explicit process and each command/ACK is one datagram. */
static nx_result_t command(example_gateway_host_t* state, uint8_t value,
                           nx_time_us_t deadline) {
    bool sent = false;
    for (uint32_t turn = 0U; turn < 1000U && before(deadline); ++turn) {
        if (!sent) {
            const ssize_t bytes = send(state->control_descriptor, &value, 1U,
                                       MSG_DONTWAIT | MSG_NOSIGNAL);
            if (bytes == 1) {
                sent = true;
            } else if (bytes >= 0 || (errno != EAGAIN && errno != EWOULDBLOCK &&
                                      errno != EINTR)) {
                return NX_ERROR_IO;
            }
        }
        if (sent) {
            uint8_t acknowledged = 0U;
            const ssize_t bytes = recv(state->control_descriptor, &acknowledged,
                                       1U, MSG_DONTWAIT | MSG_TRUNC);
            if (bytes == 1) {
                return acknowledged == (uint8_t)(value | 0x80U) ? NX_SUCCESS
                                                                : NX_ERROR_IO;
            }
            if (bytes >= 0 ||
                (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
                return NX_ERROR_IO;
            }
        }
        const nx_result_t waited = example_gateway_host_wait(NULL, deadline);
        if (waited != NX_SUCCESS && waited != NX_ERROR_BUSY) {
            return waited;
        }
    }
    return NX_ERROR_TIMEOUT;
}

static bool stopped(void* context) {
    example_gateway_host_t* state = context;
    if (interrupted != 0) {
        return true;
    }
    if (!state->injected && state->application.report.authenticated &&
        (state->selected == CASE_FAULT || state->selected == CASE_DEADLINE)) {
        nx_time_us_t deadline = after(100000U);
        if (state->application.attempt.deadline < deadline) {
            deadline = state->application.attempt.deadline;
        }
        /* A deterministic host-fixture action before the first publication.
         * It changes only the external peer, never an application's borrower.
         * The public SUBACK progress guard calls this bounded predicate. */
        state->control_error = command(state,
                                       state->selected == CASE_FAULT
                                           ? EXAMPLE_GATEWAY_PEER_RESET
                                           : EXAMPLE_GATEWAY_PEER_BLACKHOLE,
                                       deadline);
        state->injected = state->control_error == NX_SUCCESS;
        if (!state->injected) {
            return true;
        }
    }
    return false;
}

static nx_result_t read_der(const char* directory, const char* name,
                            uint8_t* output, size_t capacity, size_t* bytes) {
    char path[4096];
    const int length = snprintf(path, sizeof(path), "%s/%s", directory, name);
    if (length < 0 || (size_t)length >= sizeof(path)) {
        return NX_ERROR_INVALID;
    }
    const int descriptor = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0) {
        return NX_ERROR_IO;
    }
    struct stat info;
    nx_result_t result = NX_ERROR_INVALID;
    if (fstat(descriptor, &info) == 0 && S_ISREG(info.st_mode) &&
        info.st_size > 0 && (uintmax_t)info.st_size <= capacity) {
        size_t offset = 0U;
        while (offset < (size_t)info.st_size) {
            const ssize_t count = read(descriptor, output + offset,
                                       (size_t)info.st_size - offset);
            if (count > 0) {
                offset += (size_t)count;
            } else if (count == 0 || errno != EINTR) {
                break;
            }
        }
        uint8_t extra = 0U;
        if (offset == (size_t)info.st_size &&
            read(descriptor, &extra, 1U) == 0) {
            *bytes = offset;
            result = NX_SUCCESS;
        } else {
            result = NX_ERROR_IO;
        }
    }
    if (close(descriptor) != 0) {
        result = NX_ERROR_IO;
    }
    return result;
}

static void wipe(uint8_t* bytes, size_t size) {
    volatile uint8_t* output = bytes;
    while (size-- != 0U) {
        *output++ = 0U;
    }
}

/** \brief Start the explicit peer after establishing waitable-child ownership.
 */
nx_result_t example_gateway_host_start_peer(example_gateway_host_t* state,
                                            uint16_t broker_port) {
    /* This standalone program owns its one child's reaping. No concurrent
     * waiter or SIGCHLD handler is admitted through join. Discard inherited
     * SIG_IGN/SA_NOCLDWAIT so a dead child retains its PID until our waitpid.
     */
    struct sigaction child_action = {.sa_handler = SIG_DFL};
    if (sigemptyset(&child_action.sa_mask) != 0 ||
        sigaction(SIGCHLD, &child_action, NULL) != 0) {
        return NX_ERROR_IO;
    }
    int frames[2];
    int controls[2];
    if (socketpair(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0,
                   frames) != 0) {
        return NX_ERROR_IO;
    }
    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC, 0,
                   controls) != 0) {
        (void)close(frames[0]);
        (void)close(frames[1]);
        return NX_ERROR_IO;
    }
    state->peer = fork();
    if (state->peer == 0) {
        (void)close(frames[0]);
        (void)close(controls[0]);
        /* Each process initializes its independent stack once, after fork.
         * No PSA context, private credentials or running worker is forked. */
        lwip_init();
        const example_gateway_peer_config_t config = {
            .frame_descriptor = frames[1],
            .control_descriptor = controls[1],
            .broker_port = broker_port,
            .deadline = after(12000000U),
            .pause_us = state->selected == CASE_BACKPRESSURE ? 40000U : 0U,
            .relay_budget = state->selected == CASE_BACKPRESSURE ? 17U : 2048U,
            .turn_limit = 20000U};
        example_gateway_peer_report_t report = {0};
        const nx_result_t result =
            example_gateway_peer_run(&peer_storage, &config, &report);
        const ssize_t copied = send(controls[1], &report, sizeof(report),
                                    MSG_DONTWAIT | MSG_NOSIGNAL);
        (void)close(frames[1]);
        (void)close(controls[1]);
        _exit(result == NX_SUCCESS && report.local_resources_released &&
                      copied == (ssize_t)sizeof(report)
                  ? 0
                  : 1);
    }
    (void)close(frames[1]);
    (void)close(controls[1]);
    if (state->peer < 0) {
        (void)close(frames[0]);
        (void)close(controls[0]);
        return NX_ERROR_IO;
    }
    state->frame_descriptor = frames[0];
    state->control_descriptor = controls[0];
    return NX_SUCCESS;
}

/** \brief Join only the owned peer and preserve failed withdrawal as an error.
 */
nx_result_t example_gateway_host_join(example_gateway_host_t* state) {
    if (state->peer <= 0) {
        return NX_SUCCESS;
    }
    const nx_time_us_t deadline = after(500000U);
    nx_result_t result = command(state, EXAMPLE_GATEWAY_PEER_STOP, deadline);
    if (result == NX_SUCCESS) {
        while (before(deadline)) {
            const ssize_t count =
                recv(state->control_descriptor, &state->peer_report,
                     sizeof(state->peer_report), MSG_DONTWAIT | MSG_TRUNC);
            if (count == (ssize_t)sizeof(state->peer_report)) {
                state->peer_report_valid = true;
                break;
            }
            if (count >= 0 ||
                (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
                result = NX_ERROR_IO;
                break;
            }
            (void)example_gateway_host_wait(NULL, deadline);
        }
    }
    int status = 0;
    bool child_owned = true;
    while (before(deadline)) {
        const pid_t joined = waitpid(state->peer, &status, WNOHANG);
        if (joined == state->peer) {
            child_owned = false;
            state->peer_joined = WIFEXITED(status) && WEXITSTATUS(status) == 0;
            if (!state->peer_joined) {
                result = NX_ERROR_IO;
            }
            break;
        }
        if (joined < 0 && errno != EINTR) {
            /* ECHILD withdraws PID ownership even without a successful status
             * observation. Never signal a number that may already be reused. */
            if (errno == ECHILD) {
                child_owned = false;
            }
            result = NX_ERROR_IO;
            break;
        }
        (void)example_gateway_host_wait(NULL, deadline);
    }
    if (child_owned) {
        /* Withdraw only the child we created. A forced exit fails this case;
         * it is never represented as successful graceful withdrawal. */
        (void)kill(state->peer, SIGKILL);
        while (waitpid(state->peer, &status, 0) < 0 && errno == EINTR) {
        }
        result = NX_ERROR_IO;
    }
    return result == NX_SUCCESS && state->peer_report_valid &&
                   state->peer_report.local_resources_released
               ? NX_SUCCESS
               : NX_ERROR_IO;
}

static nx_result_t start_network(example_gateway_host_t* state) {
    lwip_init();
    nx_result_t result = example_gateway_native_frames_init(
        &state->frames, state->frame_descriptor,
        (nx_clock_t){example_gateway_host_now, NULL});
    if (result != NX_SUCCESS) {
        return result;
    }
    const nx_lwip_config_t config = {
        .port = &state->frames.face,
        .clock = {example_gateway_host_now, NULL},
        .tx_timeout_us = 100000U,
        .tx_buffer = state->transmit,
        .tx_capacity = sizeof(state->transmit),
        .rx_views = &state->receive_view,
        .rx_capacity = 1U,
        .hardware_address = {2U, 0U, 0U, 0U, 0U, 1U},
        .address = {192U, 0U, 2U, 1U},
        .netmask = {255U, 255U, 255U, 0U}};
    result = nx_lwip_adapter_start(&state->network, &config);
    if (result == NX_SUCCESS) {
        state->network_live = true;
        /* This is an explicit IPC fabric fact, not an inferred PHY fact. */
        const nx_eth_link_t software_link = {true, true, 100U};
        result = nx_lwip_adapter_apply_link(&state->network, &software_link);
    }
    return result;
}

static nx_result_t start_crypto(example_gateway_host_t* state) {
    const nx_tls_runtime_config_t config = {
        .arena = state->arena,
        .arena_bytes = sizeof(state->arena),
        .entropy = &entropy,
        .clock = {example_gateway_host_now, NULL},
        .utc = {example_gateway_host_utc, NULL},
        .entropy_timeout_us = 50000U,
        .entropy_quality = NX_TLS_ENTROPY_FULL_SOURCE};
    const nx_result_t result =
        nx_tls_runtime_start(&state->crypto, &config, after(100000U));
    state->crypto_live = result == NX_SUCCESS;
    return result;
}

/** \brief Bind the selected finite Native budgets to the real C application. */
nx_result_t example_gateway_host_run_job(example_gateway_host_t* state) {
    static const char client_id[] = "nexus-private-gateway";
    static const char topic[] = "nexus/gateway/status";
    static const uint8_t payload[] = "id:42";
    state->mqtt_memory =
        (nx_mqtt_memory_t){state->mqtt_buffer, sizeof(state->mqtt_buffer),
                           state->outgoing,    2U,
                           state->incoming,    2U,
                           state->controls,    2U};
    example_gateway_config_t config = {
        .tcp = &state->tcp,
        .tls = &state->tls,
        .mqtt = &state->mqtt,
        .mqtt_memory = &state->mqtt_memory,
        .tcp_config = {.network = &state->network,
                       .receive_budget = 4U,
                       .address = {192U, 0U, 2U, 2U},
                       .port = 8883U},
        .tls_config = {.runtime = &state->crypto,
                       .trust_der = state->ca,
                       .trust_bytes = state->ca_bytes,
                       .client_der = state->certificate_bytes != 0U
                                         ? state->certificate
                                         : NULL,
                       .client_bytes = state->certificate_bytes,
                       .key_der = state->key_bytes != 0U ? state->key : NULL,
                       .key_bytes = state->key_bytes,
                       .hostname = "server.test",
                       .hostname_length = sizeof("server.test") - 1U,
                       .tx_stage = state->stage,
                       .tx_stage_bytes = sizeof(state->stage)},
        .identity = {.client_id = client_id,
                     .client_id_size = sizeof(client_id) - 1U,
                     .keepalive_seconds = 30U},
        .clock = {example_gateway_host_now, NULL},
        .wait = example_gateway_host_wait,
        .stop = stopped,
        .wait_context = state,
        .topic = topic,
        .topic_bytes = sizeof(topic) - 1U,
        .payload = payload,
        .payload_bytes = sizeof(payload) - 1U,
        .policy = {.overall_deadline = after(
                       state->selected == CASE_DEADLINE ? 1200000U : 5000000U),
                   /* The injected 40ms pause is only a lower bound: 17-byte
                    * TLS/MQTT relay rounds also consume the fixed 5s job. */
                   .attempt_budget_us =
                       state->selected == CASE_DEADLINE       ? 900000U
                       : state->selected == CASE_BACKPRESSURE ? 4500000U
                                                              : 2500000U,
                   .retry_delay_us = 10000U,
                   .attempts_limit =
                       state->selected == CASE_MISSING_CLIENT ? 1U : 2U},
        .cleanup_budget_us = 750000U,
        .mqtt_io_budget = 65535U,
        .progress_turns = 12000U};
    return example_gateway_run(&state->application, &config, &state->report);
}

static bool expected(const example_gateway_host_t* state, nx_result_t result) {
    const example_gateway_report_t* report = &state->report;
    if (!report->local_sessions_released ||
        state->control_error != NX_SUCCESS || !state->peer_joined ||
        !state->peer_report_valid ||
        !state->peer_report.local_resources_released ||
        state->peer_report.received_frames == 0U ||
        state->peer_report.client_to_broker_bytes == 0U ||
        state->peer_report.broker_to_client_bytes == 0U) {
        return false;
    }
    if (state->selected == CASE_MISSING_CLIENT) {
        return result != NX_SUCCESS && !report->delivered &&
               report->tls_snapshot_valid &&
               report->tls_before_cleanup.library_error ==
                   MBEDTLS_ERR_SSL_FATAL_ALERT_MESSAGE;
    }
    if (state->selected == CASE_DEADLINE) {
        return state->injected && report->authenticated &&
               result == NX_ERROR_TIMEOUT && !report->delivered;
    }
    if (result != NX_SUCCESS || !report->authenticated || !report->delivered ||
        !report->echoed || !report->graceful_close) {
        return false;
    }
    if (state->selected == CASE_FAULT) {
        return state->injected && report->attempts == 2U &&
               report->first_attempt_failure == NX_ERROR_IO &&
               state->peer_report.resets == 1U &&
               state->peer_report.connections >= 2U;
    }
    return state->selected != CASE_BACKPRESSURE ||
           state->peer_report.paused_rounds != 0U;
}

nx_time_us_t nx_mqtt_port_now_us(void) {
    return example_gateway_host_now(NULL);
}
void nx_mqtt_port_assert(const char* file, unsigned line) {
    fprintf(stderr, "gateway assertion %s:%u\n", file, line);
    abort();
}
/** \brief Run one serialized Native fixture and preserve release failures. */
int example_gateway_host_run(example_gateway_host_t* state, int argc,
                             char** argv) {
    static const char* const names[] = {"clean", "backpressure", "fault",
                                        "deadline", "missing-client"};
    if (argc != 4 || argv[1][0] == '\0') {
        return 2;
    }
    char* end = NULL;
    errno = 0;
    const unsigned long port = strtoul(argv[1], &end, 10);
    if (errno != 0 || *end != '\0' || port == 0U || port > UINT16_MAX) {
        return 2;
    }
    size_t selected = 0U;
    while (selected < sizeof(names) / sizeof(names[0]) &&
           strcmp(argv[2], names[selected]) != 0) {
        ++selected;
    }
    if (selected == sizeof(names) / sizeof(names[0])) {
        return 2;
    }
    state->selected = (gateway_case_t)selected;
    state->frame_descriptor = state->control_descriptor = -1;
    nx_result_t result = example_gateway_host_start_peer(state, (uint16_t)port);
    if (result != NX_SUCCESS) {
        return 1;
    }
    const struct sigaction action = {.sa_handler = signal_stop};
    if (sigaction(SIGINT, &action, NULL) != 0 ||
        sigaction(SIGTERM, &action, NULL) != 0) {
        result = NX_ERROR_IO;
    }
    if (result == NX_SUCCESS) {
        result = read_der(argv[3], "ca.der", state->ca, sizeof(state->ca),
                          &state->ca_bytes);
    }
    if (result == NX_SUCCESS && state->selected != CASE_MISSING_CLIENT) {
        result =
            read_der(argv[3], "client.der", state->certificate,
                     sizeof(state->certificate), &state->certificate_bytes);
        if (result == NX_SUCCESS) {
            result = read_der(argv[3], "client.key.der", state->key,
                              sizeof(state->key), &state->key_bytes);
        }
    }
    if (result == NX_SUCCESS) {
        result = start_network(state);
    }
    if (result == NX_SUCCESS) {
        result = start_crypto(state);
    }
    if (result == NX_SUCCESS) {
        result = example_gateway_host_run_job(state);
    }
    nx_result_t local_cleanup = NX_SUCCESS;
    if (state->application.tcp_owned || state->application.tls_owned ||
        state->application.mqtt_owned) {
        local_cleanup = example_gateway_cleanup(&state->application,
                                                after(500000U), &state->report);
    }
    if (local_cleanup == NX_SUCCESS &&
        (state->application.tcp_owned || state->application.tls_owned ||
         state->application.mqtt_owned)) {
        local_cleanup = NX_ERROR_BUSY;
    }
    if (local_cleanup == NX_SUCCESS && state->crypto_live) {
        local_cleanup = nx_tls_runtime_stop(&state->crypto);
        state->crypto_live = local_cleanup != NX_SUCCESS;
    }
    if (local_cleanup == NX_SUCCESS && state->network_live) {
        local_cleanup = nx_lwip_adapter_stop(&state->network);
        state->network_live = local_cleanup != NX_SUCCESS;
    }
    const nx_result_t joined = example_gateway_host_join(state);
    if (!state->network_live) {
        /* A retained lower network keeps its borrowed frame descriptor live
         * until process exit; a timeout never withdraws that substrate. */
        (void)close(state->frame_descriptor);
        state->frame_descriptor = -1;
    }
    (void)close(state->control_descriptor);
    state->control_descriptor = -1;
    wipe(state->key, sizeof(state->key));
    printf("gateway_case=%s result=%d attempts=%u first_failure=%d "
           "authenticated=%u delivered=%u echoed=%u graceful=%u "
           "session_release=%u cleanup=%d tls_snapshot=%u tls_error=%d "
           "verify_flags=%u "
           "control=%d injected=%u peer_join=%u peer_release=%u "
           "peer_connections=%u peer_resets=%u peer_pauses=%u "
           "peer_frames=%" PRIu64 " client_bytes=%" PRIu64
           " broker_bytes=%" PRIu64 "\n",
           names[selected], (int)result, state->report.attempts,
           (int)state->report.first_attempt_failure,
           state->report.authenticated, state->report.delivered,
           state->report.echoed, state->report.graceful_close,
           state->report.local_sessions_released, (int)local_cleanup,
           state->report.tls_snapshot_valid,
           state->report.tls_before_cleanup.library_error,
           state->report.tls_before_cleanup.verify_flags,
           (int)state->control_error, state->injected, state->peer_joined,
           state->peer_report.local_resources_released,
           state->peer_report.connections, state->peer_report.resets,
           state->peer_report.paused_rounds, state->peer_report.received_frames,
           state->peer_report.client_to_broker_bytes,
           state->peer_report.broker_to_client_bytes);
    return local_cleanup == NX_SUCCESS && joined == NX_SUCCESS &&
                   !state->crypto_live && !state->network_live &&
                   expected(state, result)
               ? 0
               : 1;
}
