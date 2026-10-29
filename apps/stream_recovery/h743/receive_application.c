/**
 * \file            receive_application.c
 * \brief           Finite application receive progress and explicit loans
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-10-28
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "receive_application.h"

nx_result_t example_receive_consume(nx_stream_t* stream,
                                    example_receive_consumer_t* consumer,
                                    bool retain) {
    if (consumer->has_pending) {
        nx_result_t released = nx_stream_release(stream, &consumer->pending);
        if (released != NX_SUCCESS) {
            return released;
        }
        consumer->has_pending = false;
    }
    nx_result_t result = nx_stream_acquire(stream, &consumer->pending);
    if (result == NX_ERROR_EMPTY) {
        return NX_SUCCESS;
    }
    if (result != NX_SUCCESS) {
        return result;
    }
    consumer->has_pending = true;
    ++consumer->received;
    if ((consumer->pending.flags & NX_STREAM_BOUNDARY_LOSS) != 0U) {
        ++consumer->losses;
    }
    if (retain && !consumer->has_held) {
        consumer->held = consumer->pending;
        consumer->has_held = true;
        consumer->has_pending = false;
        return NX_SUCCESS;
    }
    result = nx_stream_release(stream, &consumer->pending);
    if (result == NX_SUCCESS) {
        consumer->has_pending = false;
    }
    return result;
}

nx_result_t example_receive_release(nx_stream_t* stream,
                                    example_receive_consumer_t* consumer) {
    if (consumer->has_pending) {
        nx_result_t result = nx_stream_release(stream, &consumer->pending);
        if (result != NX_SUCCESS) {
            return result;
        }
        consumer->has_pending = false;
    }
    if (consumer->has_held) {
        nx_result_t result = nx_stream_release(stream, &consumer->held);
        if (result != NX_SUCCESS) {
            return result;
        }
        consumer->has_held = false;
    }
    return NX_SUCCESS;
}

nx_result_t example_receive_stop(const nx_uart_port_t* uart,
                                 nx_stream_t* stream,
                                 example_receive_consumer_t* consumer) {
    nx_result_t result = nx_uart_port_rx_stop(uart);
    for (unsigned attempt = 0U; attempt < 64U; ++attempt) {
        nx_result_t released = example_receive_release(stream, consumer);
        if (released != NX_SUCCESS) {
            return released;
        }
        if (result != NX_ERROR_BUSY) {
            return result;
        }
        nx_uart_port_service(uart);
        nx_result_t consumed = example_receive_consume(stream, consumer, false);
        if (consumed != NX_SUCCESS) {
            return consumed;
        }
        result = nx_uart_port_rx_stop(uart);
    }
    return result;
}
