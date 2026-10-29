/**
 * \file            main.c
 * \brief           H743 continuous DMA receive with explicit stop and recovery
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-10-24
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "nexus/core/time.h"
#include "nexus/io/uart.h"
#include "nexus_bindings.h"
#include "nexus_factory_strong.h"
#include "receive_application.h"
#include <stdbool.h>

/* Both reviewed H743 recipes place .bss in DMA-visible AXI SRAM. Each
 * 128-byte reservation owns complete 32-byte cache lines; slot/control storage
 * is separate. The selected provider owns DMA cache maintenance. Applications
 * access payload only after acquire and before release, in either cache mode.
 */
static _Alignas(32) uint8_t s_payload[4][128];
static nx_stream_slot_t s_slots[4];
static nx_stream_t s_stream;
static example_receive_consumer_t s_consumer;

/** \brief Debugger-visible outcomes, never a physical qualification record. */
volatile uint32_t example_received_blocks;
volatile uint32_t example_loss_reports;
volatile uint32_t example_restarts;
volatile int example_result;

/** \brief Keep every static loan live when cleanup cannot prove quiescence. */
static _Noreturn void retain(int result) {
    example_result = result;
    for (;;) {
    }
}

/** \brief Attempt bounded drain, then stop Platform without further time reads.
 */
static int close_platform(const nx_uart_port_t* uart, int result) {
    example_received_blocks = s_consumer.received;
    example_loss_reports = s_consumer.losses;
    if (uart != NULL &&
        example_receive_stop(uart, &s_stream, &s_consumer) != NX_SUCCESS) {
        retain(result == 0 ? 5 : result);
    }
    for (unsigned attempt = 0U; attempt < 64U; ++attempt) {
        if (nx_platform_stop() == NX_SUCCESS) {
            example_result = result;
            return result;
        }
    }
    retain(result == 0 ? 6 : result);
}

/** \brief Run two finite receive windows; hardware drain may retain storage. */
int main(void) {
    nx_platform_start_result_t started = nx_platform_start();
    if (started.primary != NX_SUCCESS || started.cleanup != NX_SUCCESS ||
        started.remaining_effects != 0U) {
        return close_platform(NULL, 1);
    }
    for (size_t index = 0U; index < 4U; ++index) {
        s_slots[index].data = s_payload[index];
        s_slots[index].capacity = sizeof(s_payload[index]);
    }
    if (nx_stream_initialize(&s_stream, s_slots, 4U) != NX_SUCCESS) {
        return close_platform(NULL, 2);
    }
    const nx_uart_port_t* uart = nx_factory_uart_ref(NX_UART_REF_CONSOLE);
    for (unsigned window = 0U; window < 2U; ++window) {
        if (nx_uart_port_rx_start(uart, &s_stream) != NX_SUCCESS) {
            return close_platform(uart, 3);
        }
        if (window != 0U) {
            ++example_restarts;
        }
        nx_time_us_t finish = nx_deadline_after(nx_time_now_us(), 200000U);
        nx_time_us_t release = nx_deadline_after(nx_time_now_us(), 50000U);
        while (nx_time_now_us() < finish) {
            (void)nx_uart_port_service(uart);
            if (s_consumer.has_held && nx_time_now_us() >= release &&
                example_receive_release(&s_stream, &s_consumer) != NX_SUCCESS) {
                return close_platform(uart, 4);
            }
            if (example_receive_consume(
                    &s_stream, &s_consumer,
                    window == 0U && nx_time_now_us() < release) != NX_SUCCESS) {
                return close_platform(uart, 4);
            }
        }
        if (example_receive_stop(uart, &s_stream, &s_consumer) != NX_SUCCESS) {
            return close_platform(uart, 5);
        }
    }
    return close_platform(uart, 0);
}
