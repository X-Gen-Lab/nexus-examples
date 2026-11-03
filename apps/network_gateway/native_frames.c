/**
 * \file            native_frames.c
 * \brief           Bounded host IPC frames with explicit request settlement
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-02
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#define _POSIX_C_SOURCE 200809L
#include "native_frames.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/uio.h>

#define HOST_FRAGMENTS 8U
static const nx_eth_frames_ops_t operations;

static bool overlaps(const void* first, size_t first_size, const void* second,
                     size_t second_size) {
    const uintptr_t a = (uintptr_t)first;
    const uintptr_t b = (uintptr_t)second;
    return a <= b ? b - a < first_size : a - b < second_size;
}
static bool disjoint(const example_gateway_native_frames_t* port,
                     const void* value, size_t bytes) {
    return value != NULL && !overlaps(port, sizeof(*port), value, bytes);
}
static nx_result_t enter(example_gateway_native_frames_t* port) {
    if (port == NULL || port->face.ops != &operations ||
        port->face.context != port) {
        return NX_ERROR_INVALID;
    }
    if (port->entered) {
        return NX_ERROR_BUSY;
    }
    port->entered = true;
    return NX_SUCCESS;
}
static nx_result_t leave(example_gateway_native_frames_t* port,
                         nx_result_t result) {
    port->entered = false;
    return result;
}
static nx_result_t observed_now(example_gateway_native_frames_t* port,
                                nx_time_us_t* now) {
    const nx_time_us_t value = port->clock.read(port->clock.context);
    if (value == UINT64_MAX || value < port->last_time) {
        port->state.mac.closing = true;
        port->state.mac.fatal_bus_error = true;
        return NX_ERROR_STATE;
    }
    port->last_time = value;
    *now = value;
    return NX_SUCCESS;
}
static nx_result_t live_now(example_gateway_native_frames_t* port,
                            nx_time_us_t* now) {
    if (!port->state.mac.started || port->state.mac.closing) {
        return NX_ERROR_STATE;
    }
    return observed_now(port, now);
}
static nx_result_t transmit(void* context,
                            nx_eth_frames_tx_request_t* request) {
    example_gateway_native_frames_t* port = context;
    nx_result_t result = enter(port);
    if (result != NX_SUCCESS) {
        return result;
    }
    if (!disjoint(port, request, sizeof(*request))) {
        return leave(port, NX_ERROR_INVALID);
    }
    if (nx_request_state(&request->base) != NX_REQUEST_READY) {
        return leave(port, NX_ERROR_STATE);
    }
    if (request->fragments == NULL || request->fragment_count == 0U ||
        request->fragment_count > HOST_FRAGMENTS ||
        !disjoint(port, request->fragments,
                  request->fragment_count * sizeof(*request->fragments))) {
        return leave(port, NX_ERROR_INVALID);
    }
    for (size_t index = 0U; index < request->fragment_count; ++index) {
        if (!disjoint(port, request->fragments[index].data,
                      request->fragments[index].length)) {
            return leave(port, NX_ERROR_INVALID);
        }
    }
    /* Reuse complete-frame and original-request alias validation. A detached
     * temporary request would lose the original control/data relationship. */
    result = nx_eth_frames_tx_prepare(
        request, request->fragments, request->fragment_count,
        request->timestamp_requested, request->base.deadline);
    if (result != NX_SUCCESS) {
        return leave(port, result);
    }
    if (!port->state.mac.link.up) {
        return leave(port, NX_ERROR_STATE);
    }
    nx_time_us_t now = 0U;
    result = live_now(port, &now);
    if (result != NX_SUCCESS) {
        return leave(port, result);
    }
    if (request->base.deadline == NX_DEADLINE_NEVER) {
        return leave(port, NX_ERROR_INVALID);
    }
    if (nx_deadline_expired(request->base.deadline, now)) {
        return leave(port, NX_ERROR_TIMEOUT);
    }
    struct iovec vectors[HOST_FRAGMENTS] = {{0}};
    size_t bytes = 0U;
    for (size_t index = 0U; index < request->fragment_count; ++index) {
        vectors[index].iov_base = (void*)request->fragments[index].data;
        vectors[index].iov_len = request->fragments[index].length;
        bytes += request->fragments[index].length;
    }
    if (bytes > NX_ETH_FRAME_MAX) {
        return leave(port, NX_ERROR_INVALID);
    }
    result = nx_request_admit(&request->base, NX_REQUEST_ACTIVE);
    if (result != NX_SUCCESS) {
        return leave(port, result);
    }
    const struct msghdr message = {.msg_iov = vectors,
                                   .msg_iovlen = request->fragment_count};
    const ssize_t sent =
        sendmsg(port->descriptor, &message, MSG_DONTWAIT | MSG_NOSIGNAL);
    result = sent == (ssize_t)bytes ? NX_SUCCESS : NX_ERROR_IO;
    if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        result = NX_ERROR_EXHAUSTED;
    }
    request->stamp = (nx_io_timestamp_t){0};
    request->capture_flags = 0U;
    /* No request, fragment or data reference is stored by this port. Kernel
     * admission copied the datagram; every local reference ends at settle. */
    nx_request_settle(&request->base, result,
                      result == NX_SUCCESS ? bytes : 0U);
    return leave(port, NX_SUCCESS);
}
static nx_result_t cancel(void* context, nx_eth_frames_tx_request_t* request) {
    example_gateway_native_frames_t* port = context;
    nx_result_t result = enter(port);
    if (result != NX_SUCCESS) {
        return result;
    }
    (void)request;
    /* TX is synchronously settled. There is no live request to cancel. */
    return leave(port, NX_ERROR_STATE);
}
static nx_result_t service(void* context) {
    example_gateway_native_frames_t* port = context;
    nx_result_t result = enter(port);
    if (result != NX_SUCCESS) {
        return result;
    }
    nx_time_us_t now = 0U;
    result = live_now(port, &now);
    return leave(port, result);
}
static nx_result_t acquire(void* context, nx_eth_frames_rx_loan_t* loan,
                           nx_eth_fragment_t* views, size_t capacity) {
    example_gateway_native_frames_t* port = context;
    nx_result_t result = enter(port);
    if (result != NX_SUCCESS) {
        return result;
    }
    if (capacity == 0U || capacity > SIZE_MAX / sizeof(*views) ||
        !disjoint(port, loan, sizeof(*loan)) ||
        !disjoint(port, views, capacity * sizeof(*views)) ||
        overlaps(loan, sizeof(*loan), views, capacity * sizeof(*views))) {
        return leave(port, NX_ERROR_INVALID);
    }
    nx_time_us_t now = 0U;
    result = live_now(port, &now);
    if (result != NX_SUCCESS) {
        return leave(port, result);
    }
    if (port->state.mac.held_rx != 0U) {
        return leave(port, NX_ERROR_BUSY);
    }
    if (port->loan.epoch == UINT64_MAX) {
        return leave(port, NX_ERROR_EXHAUSTED);
    }
    struct iovec vector = {.iov_base = port->receive,
                           .iov_len = sizeof(port->receive)};
    struct msghdr message = {.msg_iov = &vector, .msg_iovlen = 1U};
    const ssize_t bytes =
        recvmsg(port->descriptor, &message, MSG_DONTWAIT | MSG_TRUNC);
    if (bytes < 0) {
        result = errno == EAGAIN || errno == EWOULDBLOCK ? NX_ERROR_EMPTY
                                                         : NX_ERROR_IO;
        return leave(port, result);
    }
    if ((size_t)bytes > sizeof(port->receive) ||
        (message.msg_flags & MSG_TRUNC) != 0) {
        ++port->state.mac.discarded;
        return leave(port, NX_ERROR_OVERFLOW);
    }
    if (bytes < (ssize_t)NX_ETH_FRAME_MIN) {
        ++port->state.mac.discarded;
        return leave(port, NX_ERROR_IO);
    }
    const nx_eth_frames_rx_loan_t value = {
        .identity = port,
        .epoch = port->loan.epoch + 1U,
        .descriptor_count = 1U,
        .fragment_count = 1U,
        .length = (size_t)bytes,
        .acquired_us = now,
        .acquired_source = NX_IO_TIMESTAMP_ACQUIRE_OBSERVATION};
    port->loan = value;
    port->state.mac.held_rx = 1U;
    ++port->state.mac.received;
    views[0] = (nx_eth_fragment_t){port->receive, (size_t)bytes};
    *loan = value;
    return leave(port, NX_SUCCESS);
}
static bool same_stamp(const nx_io_timestamp_t* a, const nx_io_timestamp_t* b) {
    return a->ticks == b->ticks && a->ticks_per_second == b->ticks_per_second &&
           a->domain == b->domain && a->epoch == b->epoch &&
           a->uncertainty_ticks == b->uncertainty_ticks &&
           a->source == b->source &&
           a->uncertainty_known == b->uncertainty_known;
}
static bool same_loan(const nx_eth_frames_rx_loan_t* a,
                      const nx_eth_frames_rx_loan_t* b) {
    return a->identity == b->identity && a->epoch == b->epoch &&
           a->first_slot == b->first_slot &&
           a->descriptor_count == b->descriptor_count &&
           a->fragment_count == b->fragment_count && a->length == b->length &&
           a->acquired_us == b->acquired_us &&
           a->acquired_source == b->acquired_source &&
           a->vlan.tci == b->vlan.tci && a->vlan.present == b->vlan.present &&
           same_stamp(&a->stamp, &b->stamp) &&
           a->capture_flags == b->capture_flags;
}
static nx_result_t release(void* context, const nx_eth_frames_rx_loan_t* loan) {
    example_gateway_native_frames_t* port = context;
    nx_result_t result = enter(port);
    if (result != NX_SUCCESS) {
        return result;
    }
    if (!disjoint(port, loan, sizeof(*loan))) {
        return leave(port, NX_ERROR_INVALID);
    }
    if (port->state.mac.held_rx == 0U || !same_loan(loan, &port->loan)) {
        return leave(port, NX_ERROR_STATE);
    }
    port->state.mac.held_rx = 0U;
    return leave(port, NX_SUCCESS);
}
static nx_result_t snapshot(void* context, nx_eth_frames_state_t* state) {
    example_gateway_native_frames_t* port = context;
    nx_result_t result = enter(port);
    if (result != NX_SUCCESS) {
        return result;
    }
    if (!disjoint(port, state, sizeof(*state))) {
        return leave(port, NX_ERROR_INVALID);
    }
    *state = port->state;
    return leave(port, NX_SUCCESS);
}
static nx_result_t apply_link(void* context, const nx_eth_link_t* link) {
    example_gateway_native_frames_t* port = context;
    nx_result_t result = enter(port);
    if (result != NX_SUCCESS) {
        return result;
    }
    if (!disjoint(port, link, sizeof(*link)) || link->speed_mbps != 100U ||
        !link->full_duplex) {
        return leave(port, NX_ERROR_INVALID);
    }
    if (!port->state.mac.started || port->state.mac.closing) {
        return leave(port, NX_ERROR_STATE);
    }
    if (port->state.mac.held_rx != 0U && link->up != port->state.mac.link.up) {
        return leave(port, NX_ERROR_BUSY);
    }
    port->state.mac.link = *link;
    return leave(port, NX_SUCCESS);
}
static nx_result_t stop(void* context) {
    example_gateway_native_frames_t* port = context;
    nx_result_t result = enter(port);
    if (result != NX_SUCCESS) {
        return result;
    }
    port->state.mac.closing = true;
    if (port->state.mac.held_rx != 0U) {
        return leave(port, NX_ERROR_BUSY);
    }
    port->state.mac.started = false;
    return leave(port, NX_SUCCESS);
}
static const nx_eth_frames_ops_t operations = {.submit = transmit,
                                               .cancel = cancel,
                                               .service = service,
                                               .acquire = acquire,
                                               .release = release,
                                               .state = snapshot,
                                               .apply_link = apply_link,
                                               .stop = stop};

nx_result_t
example_gateway_native_frames_init(example_gateway_native_frames_t* port,
                                   int descriptor, nx_clock_t clock) {
    if (port == NULL || descriptor < 0 || clock.read == NULL ||
        (clock.context != NULL && !disjoint(port, clock.context, 1U))) {
        return NX_ERROR_INVALID;
    }
    if (port->entered || port->state.mac.started ||
        port->state.mac.held_rx != 0U) {
        return NX_ERROR_STATE;
    }
    const int flags = fcntl(descriptor, F_GETFL);
    int kind = 0;
    socklen_t kind_size = sizeof(kind);
    struct sockaddr_storage address = {0};
    socklen_t address_size = sizeof(address);
    if (flags < 0 || (flags & O_NONBLOCK) == 0 ||
        getsockopt(descriptor, SOL_SOCKET, SO_TYPE, &kind, &kind_size) != 0 ||
        kind != SOCK_DGRAM ||
        getpeername(descriptor, (struct sockaddr*)&address, &address_size) !=
            0 ||
        address.ss_family != AF_UNIX) {
        return NX_ERROR_INVALID;
    }
    const nx_time_us_t now = clock.read(clock.context);
    if (now == UINT64_MAX ||
        (port->face.ops == &operations && now < port->last_time)) {
        return NX_ERROR_INVALID;
    }
    const uint64_t epoch = port->loan.epoch;
    memset(port, 0, sizeof(*port));
    port->descriptor = descriptor;
    port->clock = clock;
    port->last_time = now;
    port->loan.epoch = epoch;
    port->face = (nx_eth_frames_port_t){&operations, port};
    port->state.mac.started = true;
    /* This is an explicit software fabric fact, never PHY negotiation. */
    port->state.mac.link = (nx_eth_link_t){true, true, 100U};
    return NX_SUCCESS;
}
