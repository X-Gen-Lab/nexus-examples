/**
 * \file            rx_policy.c
 * \brief           Reference console rejects damaged reception before echo
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-10-12
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "rx_policy.h"

nx_result_t example_reference_read(const nx_uart_port_t* port, uint8_t* bytes,
                                   size_t capacity, size_t* count) {
    if (count == NULL) {
        return NX_ERROR_INVALID;
    }
    *count = 0U;
    if (port == NULL || bytes == NULL || capacity == 0U || capacity > 8U) {
        return NX_ERROR_INVALID;
    }
    nx_uart_rx_event_t events[8];
    size_t received = 0U;
    nx_result_t result =
        nx_uart_port_read_events(port, events, capacity, &received);
    if (result == NX_ERROR_UNSUPPORTED) {
        result = nx_uart_port_read_bytes(port, bytes, capacity, &received);
        if (result == NX_SUCCESS) {
            *count = received;
        }
        return result;
    }
    if (result != NX_SUCCESS) {
        return result;
    }
    for (size_t i = 0U; i < received; ++i) {
        if (events[i].flags != 0U) {
            return NX_ERROR_IO;
        }
        bytes[i] = events[i].byte;
    }
    *count = received;
    return NX_SUCCESS;
}
