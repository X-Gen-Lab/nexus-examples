/**
 * \file            rx_policy.h
 * \brief           Bounded reference console receive policy
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-10-12
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#ifndef EXAMPLE_REFERENCE_RX_POLICY_H
#define EXAMPLE_REFERENCE_RX_POLICY_H

#include "nexus/io/uart.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \brief           Read at most eight console bytes without echoing loss facts.
 * \param[in]       port: Initialized UART owned by the calling task or loop.
 * \param[out]      bytes: Caller buffer, borrowed only during this call.
 * \param[in]       capacity: Buffer capacity from one through eight bytes.
 * \param[out]      count: Bytes safe to echo; zero on every failed read.
 * \return          SUCCESS, EMPTY, IO for damaged events, or provider failure.
 * \note            Event reception is preferred. Only UNSUPPORTED permits a
 *                  byte-profile retry. Aggregate byte loss stops the example;
 *                  products own framing, loss accounting and recovery policy.
 */
nx_result_t example_reference_read(const nx_uart_port_t* port, uint8_t* bytes,
                                   size_t capacity, size_t* count);

#ifdef __cplusplus
}
#endif
#endif
