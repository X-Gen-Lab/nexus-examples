/**
 * \file            main.c
 * \brief           H743 continuous DMA receive with explicit stop and recovery
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-10-24
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "nexus/core/time.h"
#include "nexus/io/stream.h"
#include "nexus/io/uart.h"
#include "nexus_bindings.h"
#include "nexus_factory_strong.h"
#include <stdbool.h>

/* The exact H743 Cache-off linker places .bss in DMA-visible AXI SRAM. Four
 * independent 128-byte reservations are aligned to whole 32-byte cache lines.
 * This does not authorize enabling Cache or substituting another memory bank.
 */
static _Alignas(32) uint8_t s_payload[4][128];
static nx_stream_slot_t s_slots[4];
static nx_stream_t s_stream;
static nx_stream_block_t s_held;
static bool s_has_held;

/** \brief Debugger-visible outcomes, never a physical qualification record. */
volatile uint32_t example_received_blocks;
volatile uint32_t example_loss_reports;
volatile uint32_t example_restarts;
volatile int example_result;

/** \brief Consume one ready block, retaining the first for a declared delay. */
static void consume(bool retain) {
    nx_stream_block_t block;
    if (nx_stream_acquire(&s_stream, &block) != NX_SUCCESS) {
        return;
    }
    ++example_received_blocks;
    if ((block.flags & NX_STREAM_BOUNDARY_LOSS) != 0U) {
        ++example_loss_reports;
    }
    if (retain && !s_has_held) {
        s_held = block;
        s_has_held = true;
    } else {
        (void)nx_stream_release(&s_stream, &block);
    }
}

/** \brief Stop new production, release held data, and drain before reuse. */
static nx_result_t stop_rx(const nx_uart_port_t* uart) {
    nx_result_t result = nx_uart_port_rx_stop(uart);
    if (s_has_held) {
        if (nx_stream_release(&s_stream, &s_held) != NX_SUCCESS) {
            return NX_ERROR_STATE;
        }
        s_has_held = false;
    }
    while (result == NX_ERROR_BUSY) {
        (void)nx_uart_port_service(uart);
        consume(false);
        result = nx_uart_port_rx_stop(uart);
    }
    return result;
}

/** \brief Run two finite receive windows; hardware drain may retain storage. */
int main(void) {
    nx_platform_start_result_t started = nx_platform_start();
    if (started.primary != NX_SUCCESS || started.cleanup != NX_SUCCESS ||
        started.remaining_effects != 0U) {
        example_result = 1;
        return example_result;
    }
    for (size_t index = 0U; index < 4U; ++index) {
        s_slots[index].data = s_payload[index];
        s_slots[index].capacity = sizeof(s_payload[index]);
    }
    if (nx_stream_initialize(&s_stream, s_slots, 4U) != NX_SUCCESS) {
        example_result = 2;
        return example_result;
    }
    const nx_uart_port_t* uart = nx_factory_uart_ref(NX_UART_REF_CONSOLE);
    for (unsigned window = 0U; window < 2U; ++window) {
        if (nx_uart_port_rx_start(uart, &s_stream) != NX_SUCCESS) {
            example_result = 3;
            return example_result;
        }
        if (window != 0U) {
            ++example_restarts;
        }
        nx_time_us_t finish = nx_deadline_after(nx_time_now_us(), 200000U);
        nx_time_us_t release = nx_deadline_after(nx_time_now_us(), 50000U);
        while (nx_time_now_us() < finish) {
            (void)nx_uart_port_service(uart);
            if (s_has_held && nx_time_now_us() >= release) {
                if (nx_stream_release(&s_stream, &s_held) != NX_SUCCESS) {
                    example_result = 4;
                    return example_result;
                }
                s_has_held = false;
            }
            consume(window == 0U && nx_time_now_us() < release);
        }
        if (stop_rx(uart) != NX_SUCCESS) {
            example_result = 5;
            return example_result;
        }
    }
    example_result = nx_platform_stop() == NX_SUCCESS ? 0 : 6;
    return example_result;
}
