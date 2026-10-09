/**
 * \file            main.c
 * \brief           Application-owned LED and UART composition on three boards
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-10-09
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "nexus_bindings.h"
#include "nexus_config.h"
#if defined(NEXUS_BACKEND_FREERTOS)
#include "nexus/os/freertos.h"
#endif
#if defined(NEXUS_BACKEND_NATIVE)
#include "nexus/io/native/model.h"
#endif

static nx_uart_tx_request_t s_transmit;

/** \brief Map an application console role to one generated physical port. */
static nx_uart_port_t* console(void) {
#if defined(NEXUS_UART1_SELECTED)
    return nx_binding_uart1;
#else
    return nx_binding_uart0;
#endif
}

/** \brief Keep all caller bytes alive until the provider releases its borrow.
 */
static nx_result_t send_bytes(const uint8_t* bytes, size_t length) {
    nx_time_us_t deadline = nx_deadline_after(nx_time_now_us(), 100000U);
    nx_result_t status =
        nx_uart_tx_prepare(&s_transmit, bytes, length, deadline);
    if (status != NX_SUCCESS) {
        return status;
    }
    status = nx_uart_port_submit(console(), &s_transmit);
    if (status != NX_SUCCESS) {
        return status;
    }
    while (nx_request_state(&s_transmit.base) != NX_REQUEST_SETTLED) {
        nx_uart_port_service(console());
        /* QUARANTINED retains bytes and request; the product must recover the
         * controller or restart. A transfer deadline does not revoke borrowing.
         */
    }
    nx_result_t result;
    size_t transferred;
    status = nx_request_result(&s_transmit.base, &result, &transferred);
    if (status != NX_SUCCESS) {
        return status;
    }
    return result == NX_SUCCESS && transferred != length ? NX_ERROR_IO : result;
}

/** \brief Execute a serialized iteration with explicit RX error decisions. */
static nx_result_t iteration(void) {
    nx_result_t result = nx_gpio_port_toggle(nx_binding_led0, NEXUS_LED0_MASK);
    if (result != NX_SUCCESS) {
        return result;
    }
    nx_uart_rx_event_t events[8];
    size_t count = 0U;
    result = nx_uart_port_read_events(console(), events, 8U, &count);
    if (result == NX_ERROR_EMPTY) {
        return NX_SUCCESS;
    }
    if (result != NX_SUCCESS) {
        return result;
    }
    uint8_t echo[8];
    for (size_t i = 0U; i < count; ++i) {
        if (events[i].flags != 0U) {
            /* This reference stops on damaged RX facts. A real product owns
             * its loss accounting, protocol resynchronization and retry policy.
             */
            return NX_ERROR_IO;
        }
        echo[i] = events[i].byte;
    }
    return count == 0U ? NX_SUCCESS : send_bytes(echo, count);
}

#if defined(NEXUS_BACKEND_FREERTOS)
static nx_freertos_task_t s_application;
static StackType_t s_stack[256];

/** \brief The external application creates and owns this sole worker. */
static void application(void* context) {
    (void)context;
    static const uint8_t greeting[] = "Nexus static ports\r\n";
    if (send_bytes(greeting, sizeof(greeting) - 1U) != NX_SUCCESS) {
        return;
    }
    for (;;) {
        if (iteration() != NX_SUCCESS) {
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(100U));
    }
}
#endif

/** \brief Start the selected platform without an implicit product scheduler. */
int main(void) {
    nx_platform_start_result_t startup = nx_platform_start();
    if (startup.primary != NX_SUCCESS || startup.cleanup != NX_SUCCESS ||
        startup.remaining_effects != 0U) {
        return 1;
    }
    nx_request_initialize(&s_transmit.base);
#if defined(NEXUS_BACKEND_FREERTOS)
    if (nx_freertos_task_start(&s_application, "reference", s_stack, 256U, 2U,
                               application, NULL) != NX_SUCCESS) {
        return 2;
    }
    vTaskStartScheduler();
    return 3;
#elif defined(NEXUS_BACKEND_NATIVE)
    static const uint8_t ping[] = {'p', 'i', 'n', 'g'};
    for (size_t i = 0U; i < sizeof(ping); ++i) {
        if (nx_native_uart_receive(ping[i], 0U) != NX_SUCCESS) {
            return 4;
        }
    }
    if (iteration() != NX_SUCCESS ||
        nx_native_gpio_output() != NEXUS_LED0_MASK) {
        return 5;
    }
    return nx_platform_stop() == NX_SUCCESS ? 0 : 6;
#else
    static const uint8_t greeting[] = "Nexus static ports\r\n";
    if (send_bytes(greeting, sizeof(greeting) - 1U) != NX_SUCCESS) {
        return 7;
    }
    for (;;) {
        if (iteration() != NX_SUCCESS) {
            return 8;
        }
        nx_time_us_t next = nx_deadline_after(nx_time_now_us(), 100000U);
        while (!nx_deadline_expired(next, nx_time_now_us())) {
            nx_uart_port_service(console());
        }
    }
#endif
}
