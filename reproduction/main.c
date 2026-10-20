/**
 * \file            main.c
 * \brief           Application-owned LED and UART reference composition
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-10-09
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "nexus_bindings.h"
#include "nexus_config.h"
#include "nexus_factory.h"
#include "rx_policy.h"
#if defined(NEXUS_BACKEND_FREERTOS)
#include "nexus/os/freertos.h"
#endif
#if defined(NEXUS_BACKEND_NATIVE)
#include "nexus/io/native/model.h"
#include <stdio.h>
#endif

static nx_uart_tx_request_t s_transmit;

#if defined(NEXUS_BACKEND_NATIVE)
/** \brief Print host diagnostics without turning metadata into release rights.
 */
static void report_lifecycle(nx_result_t primary, nx_result_t cleanup,
                             uint32_t remaining_effects,
                             const nx_platform_diagnostic_t* diagnostic) {
    (void)fprintf(stderr,
                  "platform primary=%u cleanup=%u remaining_effects=%lu "
                  "phase=%u controller_index=%zu cleanup_phase=%u "
                  "cleanup_controller_index=%zu\n",
                  (unsigned)primary, (unsigned)cleanup,
                  (unsigned long)remaining_effects, (unsigned)diagnostic->phase,
                  diagnostic->controller_index,
                  (unsigned)diagnostic->cleanup_phase,
                  diagnostic->cleanup_controller_index);
}
#endif

/** \brief Map an application console role to one generated physical port. */
static const nx_uart_port_t* console(void) {
#if defined(NEXUS_UART1_SELECTED)
    return nx_factory_uart(NX_UART_ID_UART1);
#else
    return nx_factory_uart(NX_UART_ID_UART0);
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
    nx_result_t result =
        nx_gpio_port_toggle(nx_factory_gpio(NX_GPIO_ID_LED0), NEXUS_LED0_MASK);
    if (result != NX_SUCCESS) {
        return result;
    }
    uint8_t echo[8];
    size_t count = 0U;
    result = example_reference_read(console(), echo, sizeof(echo), &count);
    if (result == NX_ERROR_EMPTY) {
        return NX_SUCCESS;
    }
    if (result != NX_SUCCESS) {
        return result;
    }
    return count == 0U ? NX_SUCCESS : send_bytes(echo, count);
}

#if defined(NEXUS_BACKEND_FREERTOS)
static nx_freertos_task_t s_application;
static StackType_t s_stack[256];

/** \brief The external application creates and owns this sole worker. */
static void application(void* context) {
    (void)context;
    static const uint8_t greeting[] = "Nexus static factory\r\n";
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
    nx_platform_diagnostic_t diagnostic;
    nx_platform_start_result_t startup =
        nx_platform_start_diagnosed(&diagnostic);
    if (startup.primary != NX_SUCCESS || startup.cleanup != NX_SUCCESS ||
        startup.remaining_effects != 0U) {
#if defined(NEXUS_BACKEND_NATIVE)
        report_lifecycle(startup.primary, startup.cleanup,
                         startup.remaining_effects, &diagnostic);
#endif
        /* Metadata never authorizes a reset, retry or borrowed-storage reuse.
         * The external product owns recovery when effects remain outstanding.
         */
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
    int exit_code = 0;
    for (size_t i = 0U; i < sizeof(ping); ++i) {
        if (nx_native_uart_model_receive(console(), ping[i], 0U) !=
            NX_SUCCESS) {
            exit_code = 4;
            break;
        }
    }
    if (exit_code == 0 && (iteration() != NX_SUCCESS ||
                           nx_native_gpio_model_output(nx_factory_gpio(
                               NX_GPIO_ID_LED0)) != NEXUS_LED0_MASK)) {
        exit_code = 5;
    }
    nx_result_t stopped = nx_platform_stop_diagnosed(&diagnostic);
    if (stopped != NX_SUCCESS) {
        (void)fprintf(stderr,
                      "platform stop=%u phase=%u controller_index=%zu\n",
                      (unsigned)stopped, (unsigned)diagnostic.phase,
                      diagnostic.controller_index);
        return 6;
    }
    return exit_code;
#else
    static const uint8_t greeting[] = "Nexus static factory\r\n";
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
