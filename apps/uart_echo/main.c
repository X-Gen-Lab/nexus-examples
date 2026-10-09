/** Typed UART event/ticket example. Electrical validation is pending.
 * Copyright (c) 2026 Nexus Embedded Platform Team. MIT license. */
#include "hal/base/nx_device.h"
#include "runtime/nx_runtime.h"
#include "nexus_config.h"
#include "osal/osal.h"
#include <stdbool.h>
#include <stdint.h>

#ifndef NEXUS_EXAMPLE_CONSOLE_DEVICE
#error "The application must select a console from the reference board wiring"
#endif
#if defined(NX_CONFIG_PLATFORM_NATIVE)
#error "uart_echo requires a hardware console RX source and is built only for MCU boards"
#endif

static nx_device_ref_t console;
/* Storage remains alive on every error path, including failed cancellation. */
static uint8_t echo_byte;
static const uint8_t banner[] = "Nexus typed UART echo\r\n";
static volatile uint32_t receive_errors;
static volatile nx_status_t last_error;

static _Noreturn void fault(nx_status_t status) {
    last_error = status;
    /* This example owns no actuators or watchdog policy. Keep any unsettled
     * UART lease and static buffers; an external debugger can inspect error. */
    for (;;) { (void)osal_task_delay(100U); }
}

static nx_status_t transmit(const uint8_t* bytes, size_t length) {
    nx_uart_ticket_t ticket = {0};
    nx_status_t status = nx_device_uart_submit(console, bytes, length, 1000U,
                                               &ticket);
    if (status != NX_OK) return status;
    for (;;) {
        nx_uart_result_t result;
        status = nx_device_uart_poll(console, ticket, &result);
        if (status != NX_OK) return status;
        if (result.settled) {
            if (result.status != NX_OK) return result.status;
            return result.wire_idle ? NX_OK : NX_ERR_INVALID_STATE;
        }
        if (osal_task_delay(1U) != OSAL_OK) {
            /* Successful UART cancel proves settlement. Failure keeps the
             * lease alive; the caller enters fault with permanent storage. */
            status = nx_device_uart_cancel(console, ticket);
            return status == NX_OK ? NX_ERR_CANCELLED : status;
        }
    }
}

static void echo(void* argument) {
    (void)argument;
    nx_status_t status = transmit(banner, sizeof(banner) - 1U);
    if (status != NX_OK) fault(status);
    for (;;) {
        nx_uart_rx_event_t event;
        status = nx_device_uart_receive_event(console, &event);
        if (status == NX_ERR_NO_DATA) {
            if (osal_task_delay(1U) != OSAL_OK) fault(NX_ERR_INVALID_STATE);
            continue;
        }
        if (status != NX_OK) fault(status);
        if (!event.has_data || event.status != NX_OK || event.raw_error != 0U) {
            ++receive_errors;
            continue;
        }
        echo_byte = event.data;
        status = transmit(&echo_byte, sizeof(echo_byte));
        if (status != NX_OK) fault(status);
    }
}

int main(void) {
    if (nx_runtime_bootstrap(NULL) != NX_OK) return 1;
    if (nx_device_open(NEXUS_EXAMPLE_CONSOLE_DEVICE, NX_DEVICE_CLASS_UART,
                       (uintptr_t)&console, &console) != NX_OK) return 1;
    nx_device_caps_t capabilities;
    const uint32_t required = NX_DEVICE_CAP_UART_OPERATIONS |
                              NX_DEVICE_CAP_UART_RX_EVENTS;
    if (nx_device_query(console, &capabilities) != NX_OK ||
        (capabilities.flags & required) != required) {
        (void)nx_device_close(console);
        return 1;
    }
#if defined(NX_CONFIG_OSAL_BAREMETAL)
    echo(NULL);
#else
    osal_task_handle_t task = NULL;
    const osal_task_config_t config = {
        .name = "UART echo", .func = echo, .arg = NULL,
        .priority = OSAL_TASK_PRIORITY_NORMAL, .stack_size = 1024U
    };
    if (osal_task_create(&config, &task) != OSAL_OK) {
        (void)nx_device_close(console);
        return 1;
    }
    osal_start();
#endif
    return 1;
}
