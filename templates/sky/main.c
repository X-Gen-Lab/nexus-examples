/**
 * \file            main.c
 * \brief           Single-owner GPIO and UART starter with retained storage
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-10-24
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "nexus/io/gpio.h"
#include "nexus/io/uart.h"
#include "nexus_bindings.h"
#include "nexus_config.h"
#include "nexus_factory_strong.h"
#if defined(NEXUS_BACKEND_NATIVE)
#include <stdio.h>
#endif

static nx_uart_tx_request_t s_request;
static bool s_pending;
static const uint8_t s_message[] = "Nexus starter ready\r\n";

/** \brief Retain the request and payload through any timeout or drain. */
static nx_result_t send_greeting(const nx_uart_port_t* console) {
    nx_request_initialize(&s_request.base);
    nx_time_us_t deadline;
    nx_result_t result =
        nx_deadline_finite_after(nx_time_now_us(), 100000U, &deadline);
    if (result != NX_SUCCESS) {
        return result;
    }
    result = nx_uart_tx_prepare(&s_request, s_message, sizeof(s_message) - 1U,
                                deadline);
    if (result != NX_SUCCESS) {
        return result;
    }
    result = nx_uart_port_submit(console, &s_request);
    s_pending = result == NX_SUCCESS;
    return result;
}

/** \brief Make one bounded pass; callers own scheduling and recovery. */
static nx_result_t greeting_step(const nx_uart_port_t* console) {
    nx_uart_port_service(console);
    if (nx_request_state(&s_request.base) != NX_REQUEST_SETTLED) {
        return NX_ERROR_BUSY;
    }
    s_pending = false;
    nx_result_t transfer_result;
    size_t transferred;
    nx_result_t result =
        nx_request_result(&s_request.base, &transfer_result, &transferred);
    if (result != NX_SUCCESS) {
        return result;
    }
    if (transfer_result != NX_SUCCESS) {
        return transfer_result;
    }
    return transferred == sizeof(s_message) - 1U ? NX_SUCCESS : NX_ERROR_IO;
}

/** \brief Own platform startup, one transaction, and the shutdown decision. */
int main(void) {
    nx_platform_start_result_t started = nx_platform_start();
    if (started.primary != NX_SUCCESS || started.cleanup != NX_SUCCESS ||
        started.remaining_effects != 0U) {
        return 1;
    }
    const nx_gpio_signal_t* status =
        nx_factory_gpio_signal_ref(NX_GPIO_REF_STATUS);
    const nx_uart_port_t* console = nx_factory_uart_ref(NX_UART_REF_CONSOLE);
    nx_result_t result = nx_gpio_signal_write(status, true);
    if (result == NX_SUCCESS) {
        result = send_greeting(console);
    }
    while (s_pending) {
        result = greeting_step(console);
        if (result == NX_ERROR_BUSY &&
            nx_request_state(&s_request.base) == NX_REQUEST_QUARANTINED) {
            /* The product takes over recovery here. Static request, payload
             * and Platform remain retained; no cleanup is claimed.
             */
            return 3;
        }
    }
    /* Only rejection or observed settlement permits platform withdrawal. */
    nx_result_t stopped = nx_platform_stop();
    if (result != NX_SUCCESS || stopped != NX_SUCCESS) {
        return 2;
    }
#if defined(NEXUS_BACKEND_NATIVE)
    (void)puts("Nexus starter: transfer settled; platform stopped");
#endif
    return 0;
}
