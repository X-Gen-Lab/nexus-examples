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
static const uint8_t s_message[] = "Nexus starter ready\r\n";

/** \brief Retain the request and payload through any timeout or drain. */
static nx_result_t send_greeting(const nx_uart_port_t* console) {
    nx_request_initialize(&s_request.base);
    nx_result_t result =
        nx_uart_tx_prepare(&s_request, s_message, sizeof(s_message) - 1U,
                           nx_deadline_after(nx_time_now_us(), 100000U));
    if (result != NX_SUCCESS) {
        return result;
    }
    result = nx_uart_port_submit(console, &s_request);
    if (result != NX_SUCCESS) {
        return result;
    }
    while (nx_request_state(&s_request.base) != NX_REQUEST_SETTLED) {
        nx_uart_port_service(console);
        /* A deadline requests termination; it never ends the memory borrow.
         * QUARANTINED storage remains static while this owner keeps servicing.
         * A product supplies its reviewed recovery policy for a stuck device.
         */
    }
    nx_result_t transfer_result;
    size_t transferred;
    result = nx_request_result(&s_request.base, &transfer_result, &transferred);
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
    const nx_gpio_port_t* status = nx_factory_gpio_ref(NX_GPIO_REF_STATUS);
    const nx_uart_port_t* console = nx_factory_uart_ref(NX_UART_REF_CONSOLE);
    nx_result_t result = nx_gpio_port_toggle(status, NEXUS_STATUS_MASK);
    if (result == NX_SUCCESS) {
        result = send_greeting(console);
    }
    /* send_greeting only returns after rejection or actual settlement. */
    nx_result_t stopped = nx_platform_stop();
    if (result != NX_SUCCESS || stopped != NX_SUCCESS) {
        return 2;
    }
#if defined(NEXUS_BACKEND_NATIVE)
    (void)puts("Nexus starter: transfer settled; platform stopped");
#endif
    return 0;
}
