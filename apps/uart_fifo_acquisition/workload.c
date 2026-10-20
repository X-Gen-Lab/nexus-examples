/**
 * \file            workload.c
 * \brief           Bounded concurrent byte traffic and retained request drain.
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-10-20
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "workload.h"
#include <string.h>

/** \brief Preserve the first observed failure without revoking any loan. */
static void remember(fifo_acquisition_t* state, nx_result_t result) {
    if (state->first_error == NX_SUCCESS && result != NX_SUCCESS) {
        state->first_error = result;
    }
}

/** \brief Stop admission, retaining a finite drain deadline in the same epoch.
 */
static void begin_close(fifo_acquisition_t* state, nx_time_us_t now) {
    if (state->phase != FIFO_ACQUISITION_RUNNING) {
        return;
    }
    state->phase = FIFO_ACQUISITION_DRAINING;
    state->acquisition_end = now;
    state->drain_end = nx_deadline_after(now, state->config.drain_timeout_us);
    if (state->drain_end == NX_DEADLINE_NEVER) {
        state->clock_fault = true;
        state->phase = FIFO_ACQUISITION_RETAINED;
        remember(state, NX_ERROR_STATE);
    }
}

/** \brief A broken epoch cannot become a valid deadline by observing zero. */
static bool current_time(fifo_acquisition_t* state, nx_time_us_t now) {
    if (state->clock_fault) {
        return false;
    }
    if (now == NX_DEADLINE_NEVER || now < state->last_now) {
        state->clock_fault = true;
        state->phase = FIFO_ACQUISITION_RETAINED;
        remember(state, NX_ERROR_STATE);
        return false;
    }
    state->last_now = now;
    return true;
}

/** \brief Capture every received byte, including valid failure prefixes. */
static void receive(fifo_acquisition_t* state, const uint8_t* bytes,
                    size_t count, nx_time_us_t now) {
    for (size_t i = 0U; i < count; ++i) {
        if (state->received_count == FIFO_ACQUISITION_RX_BYTES) {
            state->trace_exhausted = true;
            remember(state, NX_ERROR_EXHAUSTED);
            begin_close(state, now);
            return;
        }
        uint32_t offset = state->received_count;
        uint8_t expected =
            (uint8_t)(state->config.session + offset * 73U + (offset >> 8U));
        if (bytes[i] != expected) {
            ++state->mismatch_count;
        }
        state->received[state->received_count++] = bytes[i];
    }
}

/** \brief Fixed 32-byte wire blocks carry a session and monotonic request ID.
 */
static void payload(fifo_acquisition_t* state) {
    static const uint8_t magic[4] = {'N', 'X', 'F', '1'};
    memcpy(state->transmit, magic, sizeof(magic));
    for (unsigned i = 0U; i < 4U; ++i) {
        state->transmit[4U + i] = (uint8_t)(state->config.session >> (8U * i));
        state->transmit[8U + i] =
            (uint8_t)(state->accepted_requests >> (8U * i));
    }
    for (unsigned i = 12U; i < FIFO_ACQUISITION_TX_BYTES; ++i) {
        state->transmit[i] =
            (uint8_t)(state->config.session ^ state->accepted_requests ^ i);
    }
}

/** \brief Rejection never becomes an accepted request or cancellation target.
 */
static void submit(fifo_acquisition_t* state, nx_time_us_t now) {
    if (state->accepted_requests == UINT32_MAX) {
        remember(state, NX_ERROR_EXHAUSTED);
        begin_close(state, now);
        return;
    }
    payload(state);
    nx_time_us_t deadline =
        nx_deadline_after(now, state->config.transfer_timeout_us);
    if (deadline > state->window_end) {
        deadline = state->window_end;
    }
    nx_result_t result = nx_uart_tx_prepare(&state->request, state->transmit,
                                            sizeof(state->transmit), deadline);
    if (result == NX_SUCCESS) {
        result = nx_uart_port_submit(state->port, &state->request);
    }
    if (result == NX_SUCCESS) {
        state->borrowed = true;
        state->cancel_sent = false;
        ++state->accepted_requests;
    } else if (result == NX_ERROR_BUSY || result == NX_ERROR_TIMEOUT) {
        if (state->rejected_submissions == UINT32_MAX) {
            remember(state, NX_ERROR_EXHAUSTED);
            begin_close(state, now);
        } else {
            ++state->rejected_submissions;
        }
    } else {
        remember(state, result);
        begin_close(state, now);
    }
}

/** \brief Only an acquire-observed settlement permits payload reuse. */
static void consume_settlement(fifo_acquisition_t* state, nx_time_us_t now) {
    if (!state->borrowed ||
        nx_request_state(&state->request.base) != NX_REQUEST_SETTLED) {
        return;
    }
    nx_result_t terminal;
    size_t transferred;
    if (nx_request_result(&state->request.base, &terminal, &transferred) !=
            NX_SUCCESS ||
        transferred > sizeof(state->transmit) ||
        transferred > UINT32_MAX - state->completed_tx_bytes) {
        remember(state, NX_ERROR_STATE);
        begin_close(state, now);
        return;
    }
    state->borrowed = false;
    ++state->completed_requests;
    state->completed_tx_bytes += (uint32_t)transferred;
    if (terminal != NX_SUCCESS &&
        !(state->cancel_sent && terminal == NX_ERROR_CANCELLED)) {
        remember(state, terminal);
        begin_close(state, now);
    }
}

/** \brief Validate the complete finite plan before acquiring any I/O effect. */
nx_result_t fifo_acquisition_initialize(fifo_acquisition_t* state,
                                        const nx_uart_port_t* port,
                                        const fifo_acquisition_config_t* config,
                                        nx_time_us_t now) {
    if (state == NULL || port == NULL || port->ops == NULL ||
        port->context == NULL || config == NULL || now == NX_DEADLINE_NEVER ||
        config->session == 0U || config->duration_us == 0U ||
        config->service_period_us == 0U || config->transfer_timeout_us == 0U ||
        config->drain_timeout_us == 0U || config->cpu_work_units > 1024U ||
        (config->duration_us - 1U) / config->service_period_us >=
            FIFO_ACQUISITION_SERVICE_SAMPLES ||
        nx_deadline_after(now, config->duration_us) == NX_DEADLINE_NEVER ||
        port->ops->submit == NULL || port->ops->service == NULL ||
        port->ops->read_bytes == NULL || port->ops->cancel == NULL ||
        port->ops->stop == NULL) {
        return NX_ERROR_INVALID;
    }
    if (state->phase != FIFO_ACQUISITION_COLD) {
        return NX_ERROR_STATE;
    }
    fifo_acquisition_config_t copied = *config;
    memset(state, 0, sizeof(*state));
    state->port = port;
    state->config = copied;
    state->started = now;
    state->window_end = now + copied.duration_us;
    state->last_now = now;
    state->next_service = now;
    state->phase = FIFO_ACQUISITION_RUNNING;
    nx_request_initialize(&state->request.base);
    return NX_SUCCESS;
}

/** \brief Same task owner closes admission; never modifies borrowed storage. */
nx_result_t fifo_acquisition_close(fifo_acquisition_t* state,
                                   nx_time_us_t now) {
    if (state == NULL || state->phase == FIFO_ACQUISITION_COLD) {
        return NX_ERROR_INVALID;
    }
    if (state->phase == FIFO_ACQUISITION_CLOSED) {
        return NX_SUCCESS;
    }
    if (!current_time(state, now)) {
        return NX_ERROR_STATE;
    }
    begin_close(state, now);
    return state->clock_fault ? NX_ERROR_STATE : NX_SUCCESS;
}

/** \brief Execute at most one service, one RX read and one TX submission. */
nx_result_t fifo_acquisition_step(fifo_acquisition_t* state, nx_time_us_t now) {
    if (state == NULL || state->phase == FIFO_ACQUISITION_COLD) {
        return NX_ERROR_INVALID;
    }
    if (state->phase == FIFO_ACQUISITION_CLOSED) {
        return state->first_error;
    }
    if (!current_time(state, now)) {
        return state->first_error;
    }
    if (state->phase == FIFO_ACQUISITION_RUNNING && now >= state->window_end) {
        begin_close(state, now);
    }
    if (state->clock_fault) {
        return state->first_error;
    }
    if (state->phase == FIFO_ACQUISITION_RUNNING && now < state->next_service) {
        return state->first_error;
    }
    if (state->phase != FIFO_ACQUISITION_RUNNING && state->borrowed &&
        !state->cancel_sent &&
        nx_request_state(&state->request.base) != NX_REQUEST_SETTLED) {
        nx_result_t result = nx_uart_port_cancel(state->port, &state->request);
        if (result == NX_SUCCESS) {
            state->cancel_sent = true;
        } else {
            remember(state, result);
        }
    }
    if (state->phase == FIFO_ACQUISITION_RUNNING) {
        if (state->service_count == FIFO_ACQUISITION_SERVICE_SAMPLES) {
            state->trace_exhausted = true;
            remember(state, NX_ERROR_EXHAUSTED);
            begin_close(state, now);
        } else {
            state->service_times[state->service_count++] = now;
        }
    }
    nx_uart_port_service(state->port);
    uint8_t bytes[32];
    size_t count = 0U;
    nx_result_t result =
        nx_uart_port_read_bytes(state->port, bytes, sizeof(bytes), &count);
    if (count > sizeof(bytes)) {
        remember(state, NX_ERROR_STATE);
        begin_close(state, now);
        count = 0U;
    }
    receive(state, bytes, count, now);
    bool rx_empty = result == NX_ERROR_EMPTY && count == 0U;
    if (result == NX_ERROR_OVERFLOW) {
        if (state->overflow_events != UINT32_MAX) {
            ++state->overflow_events;
        } else {
            state->trace_exhausted = true;
        }
        remember(state, NX_ERROR_OVERFLOW);
    } else if (result != NX_SUCCESS && result != NX_ERROR_EMPTY) {
        remember(state, result);
        begin_close(state, now);
    } else if (result == NX_ERROR_EMPTY && count != 0U) {
        remember(state, NX_ERROR_STATE);
        begin_close(state, now);
    }
    consume_settlement(state, now);
    if (state->phase == FIFO_ACQUISITION_RUNNING && !state->borrowed) {
        submit(state, now);
    }
    for (uint32_t i = 0U; i < state->config.cpu_work_units; ++i) {
        state->cpu_checksum = state->cpu_checksum * 1664525U + 1013904223U;
    }
    state->next_service =
        nx_deadline_after(now, state->config.service_period_us);
    if (state->phase != FIFO_ACQUISITION_RUNNING && !state->borrowed &&
        rx_empty) {
        result = nx_uart_port_stop(state->port);
        if (result == NX_SUCCESS) {
            state->phase = FIFO_ACQUISITION_CLOSED;
        } else if (result != NX_ERROR_BUSY) {
            remember(state, result);
            state->phase = FIFO_ACQUISITION_RETAINED;
        }
    }
    if (state->phase != FIFO_ACQUISITION_RUNNING &&
        state->phase != FIFO_ACQUISITION_CLOSED && now >= state->drain_end) {
        remember(state, NX_ERROR_TIMEOUT);
        state->phase = FIFO_ACQUISITION_RETAINED;
    }
    return state->first_error;
}

/** \brief Caller policy retries cleanup only after direct traffic quiesces. */
nx_result_t fifo_acquisition_join_platform(const fifo_acquisition_t* state,
                                           nx_result_t (*stop_platform)(void),
                                           uint32_t attempt_budget,
                                           uint32_t* attempts) {
    if (state == NULL || stop_platform == NULL || attempts == NULL ||
        attempt_budget == 0U ||
        attempt_budget > FIFO_ACQUISITION_CLEANUP_ATTEMPTS) {
        return NX_ERROR_INVALID;
    }
    if ((state->phase != FIFO_ACQUISITION_COLD &&
         state->phase != FIFO_ACQUISITION_CLOSED) ||
        state->borrowed || state->clock_fault) {
        return NX_ERROR_STATE;
    }
    *attempts = 0U;
    nx_result_t result = NX_ERROR_BUSY;
    while (*attempts < attempt_budget) {
        ++*attempts;
        result = stop_platform();
        if (result == NX_SUCCESS ||
            (result != NX_ERROR_BUSY && result != NX_ERROR_IO)) {
            break;
        }
    }
    return result;
}
