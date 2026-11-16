/**
 * \file            stream_application.c
 * \brief           Slow consumer, loss reporting and explicit stream recovery
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-10-24
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "stream_application.h"
#include "nexus/io/diagnostic.h"
#include "nexus/io/native/model.h"
#include "nexus/io/uart.h"
#include "nexus_bindings.h"
#include "nexus_factory_strong.h"
#include <string.h>

static uint8_t s_payload[2][2];
static nx_stream_slot_t s_slots[2];
static nx_stream_t s_stream;
static nx_stream_block_t s_first;
static nx_stream_block_t s_second;
static nx_stream_block_t s_later;

#define REQUIRE(condition, code)                                               \
    do {                                                                       \
        if (!(condition)) {                                                    \
            return (code);                                                     \
        }                                                                      \
    } while (0)

/** \brief Retain static storage on failure; stop never revokes a loan. */
int example_stream_run(example_stream_report_t* report) {
    if (report == NULL) {
        return 1;
    }
    memset(report, 0, sizeof(*report));
    nx_platform_start_result_t started = nx_platform_start();
    REQUIRE(started.primary == NX_SUCCESS && started.cleanup == NX_SUCCESS &&
                started.remaining_effects == 0U,
            2);
    const nx_uart_port_t* uart = nx_factory_uart_ref(NX_UART_REF_CONSOLE);
    for (size_t index = 0U; index < 2U; ++index) {
        s_slots[index].data = s_payload[index];
        s_slots[index].capacity = sizeof(s_payload[index]);
    }
    REQUIRE(nx_stream_initialize(&s_stream, s_slots, 2U) == NX_SUCCESS, 3);
    REQUIRE(nx_uart_port_rx_start(uart, &s_stream) == NX_SUCCESS, 4);
    REQUIRE(nx_native_uart_model_receive(uart, 17U, 0U) == NX_SUCCESS, 5);
    REQUIRE(nx_native_uart_model_idle(uart) == NX_SUCCESS, 6);
    REQUIRE(nx_stream_acquire(&s_stream, &s_first) == NX_SUCCESS, 7);
    REQUIRE(nx_native_uart_model_receive(uart, 29U, 0U) == NX_SUCCESS, 8);
    REQUIRE(nx_native_uart_model_receive(uart, 31U, 0U) == NX_SUCCESS, 9);
    REQUIRE(nx_stream_acquire(&s_stream, &s_second) == NX_SUCCESS, 10);
    REQUIRE(nx_native_uart_model_receive(uart, 99U, 0U) == NX_ERROR_OVERFLOW,
            11);
    /* Pending loss moves into the next published block. Copy this fact now;
     * a later stop snapshot must not be treated as cumulative loss history. */
    nx_stream_observation_t loss_observation;
    REQUIRE(nx_stream_observe(&s_stream, &loss_observation) == NX_SUCCESS, 32);
    nx_io_diagnostic_facts_t loss_facts = {.stream = &loss_observation,
                                           .known = NX_IO_FACT_LIFECYCLE,
                                           .started = true};
    nx_io_diagnostic_t loss_diagnostic;
    REQUIRE(nx_io_diagnose(&loss_facts, &loss_diagnostic) == NX_SUCCESS, 33);
    report->loss_diagnostic_findings = loss_diagnostic.findings;
    report->borrow_preserved = s_first.data[0] == 17U &&
                               s_second.data[0] == 29U &&
                               s_second.data[1] == 31U;
    REQUIRE(report->borrow_preserved, 12);
    REQUIRE(nx_stream_release(&s_stream, &s_first) == NX_SUCCESS, 13);
    REQUIRE(nx_native_uart_model_receive(uart, 37U, 0U) == NX_SUCCESS, 14);
    REQUIRE(nx_native_uart_model_idle(uart) == NX_SUCCESS, 15);
    REQUIRE(nx_stream_acquire(&s_stream, &s_later) == NX_SUCCESS, 16);
    REQUIRE((s_later.flags & NX_STREAM_BOUNDARY_LOSS) != 0U, 17);
    report->lost_blocks = s_later.lost_blocks;
    report->stop_waited = nx_uart_port_rx_stop(uart) == NX_ERROR_BUSY;
    REQUIRE(report->stop_waited, 18);
    REQUIRE(nx_native_uart_model_receive(uart, 101U, 0U) == NX_ERROR_STATE, 19);
    nx_stream_observation_t observation;
    REQUIRE(nx_stream_observe(&s_stream, &observation) == NX_SUCCESS, 20);
    nx_io_diagnostic_facts_t facts = {.stream = &observation,
                                      .known = NX_IO_FACT_LIFECYCLE,
                                      .started = true,
                                      .stopping = true};
    nx_io_diagnostic_t diagnostic;
    REQUIRE(nx_io_diagnose(&facts, &diagnostic) == NX_SUCCESS, 21);
    report->diagnostic_findings = diagnostic.findings;
    REQUIRE(nx_stream_release(&s_stream, &s_second) == NX_SUCCESS, 22);
    REQUIRE(nx_stream_release(&s_stream, &s_later) == NX_SUCCESS, 23);
    REQUIRE(nx_uart_port_rx_stop(uart) == NX_SUCCESS, 24);
    REQUIRE(nx_uart_port_rx_start(uart, &s_stream) == NX_SUCCESS, 25);
    REQUIRE(nx_native_uart_model_receive(uart, 53U, 0U) == NX_SUCCESS, 26);
    REQUIRE(nx_native_uart_model_idle(uart) == NX_SUCCESS, 27);
    REQUIRE(nx_stream_acquire(&s_stream, &s_first) == NX_SUCCESS, 28);
    report->restarted = s_first.length == 1U && s_first.data[0] == 53U;
    REQUIRE(nx_stream_release(&s_stream, &s_first) == NX_SUCCESS, 29);
    REQUIRE(nx_uart_port_rx_stop(uart) == NX_SUCCESS, 30);
    report->stopped = nx_platform_stop() == NX_SUCCESS;
    return report->restarted && report->stopped ? 0 : 31;
}
