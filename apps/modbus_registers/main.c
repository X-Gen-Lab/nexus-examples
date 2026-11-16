/**
 * \file            main.c
 * \brief           Product-owned Modbus authorization over explicit UART model.
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-10-09
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "nexus/components/log.h"
#include "nexus/components/modbus_rtu.h"
#include "nexus/io/native/model.h"
#include "nexus_bindings.h"
#include "nexus_factory_strong.h"
#include <stdio.h>
#include <string.h>

/** \brief The application owns its bank, transport borrow and model storage. */
typedef struct {
    uint16_t values[8];
    nx_uart_tx_request_t transmit;
    nx_uart_rx_event_t receive[32];
    uint8_t wire[64];
    bool sending;
    bool direction_tx;
    bool critical_enabled;
} application_t;

/** \brief Synchronous bounded memory logging, without a hidden worker. */
typedef struct {
    char bytes[128];
    size_t length;
} memory_log_t;

/** \brief Read the explicit monotonic provider domain. */
static uint64_t now_us(void* context) {
    (void)context;
    return nx_time_now_us();
}

/** \brief Never release modeled DE while UART still borrows server TX bytes. */
static bool direction(void* context, bool transmit) {
    application_t* app = context;
    if (!transmit && app->sending) {
        return false;
    }
    app->direction_tx = transmit;
    return true;
}

/** \brief Submit exactly one caller-owned descriptor with the protocol
 * deadline. */
static bool write_bytes(void* context, const uint8_t* bytes, size_t length,
                        uint64_t deadline) {
    application_t* app = context;
    if (!app->direction_tx || app->sending ||
        nx_uart_tx_prepare(&app->transmit, bytes, length, deadline) !=
            NX_SUCCESS) {
        return false;
    }
    nx_result_t result = nx_uart_port_submit(
        nx_factory_uart_ref(NX_UART_REF_CONSOLE), &app->transmit);
    app->sending = result == NX_SUCCESS;
    return app->sending;
}

/** \brief Own service progress and wait only for the authoritative level state.
 */
static bool settle(application_t* app, nx_time_us_t wait_deadline) {
    while (nx_request_state(&app->transmit.base) != NX_REQUEST_SETTLED) {
        nx_uart_port_service(nx_factory_uart_ref(NX_UART_REF_CONSOLE));
        if (nx_request_state(&app->transmit.base) == NX_REQUEST_SETTLED) {
            break;
        }
        if (nx_deadline_expired(wait_deadline, nx_time_now_us()) ||
            nx_native_clock_advance(1U) != NX_SUCCESS) {
            return false;
        }
    }
    app->sending = false;
    return true;
}

/** \brief True TC settlement releases borrowed bytes before receive direction.
 */
static bool drain(void* context, uint64_t deadline) {
    application_t* app = context;
    if (!app->sending || !settle(app, deadline)) {
        return false;
    }
    nx_result_t result;
    size_t length;
    return nx_request_result(&app->transmit.base, &result, &length) ==
               NX_SUCCESS &&
           result == NX_SUCCESS;
}

/** \brief A failed drain retains stable storage until cancellation really
 * settles. */
static bool abort_and_settle(void* context, uint64_t deadline) {
    application_t* app = context;
    if (!app->sending) {
        return true;
    }
    if (nx_uart_port_cancel(nx_factory_uart_ref(NX_UART_REF_CONSOLE),
                            &app->transmit) != NX_SUCCESS) {
        return false;
    }
    return settle(app, deadline);
}

/** \brief Read the application's coherent serialized register bank snapshot. */
static nx_modbus_register_result_t read_registers(void* context, uint16_t start,
                                                  uint16_t count,
                                                  uint16_t* output) {
    application_t* app = context;
    if ((uint32_t)start + count > 8U) {
        return NX_MODBUS_ILLEGAL_ADDRESS;
    }
    memcpy(output, &app->values[start], (size_t)count * sizeof(*output));
    return NX_MODBUS_OK;
}

/** \brief Interlocks and write authorization belong to the external
 * application. */
static nx_modbus_register_result_t authorize(void* context, uint16_t start,
                                             uint16_t count,
                                             const uint16_t* values) {
    application_t* app = context;
    (void)values;
    if ((uint32_t)start + count > 8U) {
        return NX_MODBUS_ILLEGAL_ADDRESS;
    }
    if ((uint32_t)start + count > 6U && !app->critical_enabled) {
        return NX_MODBUS_DEVICE_BUSY;
    }
    return NX_MODBUS_OK;
}

/** \brief Recheck the interlock and commit all values or none under one owner.
 */
static nx_modbus_register_result_t write_registers(void* context,
                                                   uint16_t start,
                                                   uint16_t count,
                                                   const uint16_t* values) {
    nx_modbus_register_result_t result =
        authorize(context, start, count, values);
    if (result != NX_MODBUS_OK) {
        return result;
    }
    application_t* app = context;
    memcpy(&app->values[start], values, (size_t)count * sizeof(*values));
    return NX_MODBUS_OK;
}

/** \brief Feed actual bounded event-profile RX facts into the protocol core. */
static bool exchange(application_t* app, nx_modbus_rtu_t* server,
                     uint8_t index) {
    uint8_t packet[8] = {17U, 6U, 0U, index, 0x12U, 0x34U, 0U, 0U};
    uint16_t crc = nx_modbus_crc16(packet, 6U);
    packet[6] = (uint8_t)crc;
    packet[7] = (uint8_t)(crc >> 8U);
    if (nx_native_clock_advance(server->t3_5_us + 100U) != NX_SUCCESS) {
        return false;
    }
    for (size_t i = 0U; i < sizeof(packet); ++i) {
        if (nx_native_uart_model_receive(
                nx_factory_uart_ref(NX_UART_REF_CONSOLE), packet[i], 0U) !=
                NX_SUCCESS ||
            nx_native_clock_advance(100U) != NX_SUCCESS) {
            return false;
        }
    }
    nx_uart_rx_event_t events[8];
    size_t count;
    if (nx_uart_port_read_events(nx_factory_uart_ref(NX_UART_REF_CONSOLE),
                                 events, 8U, &count) != NX_SUCCESS ||
        count != sizeof(packet)) {
        return false;
    }
    for (size_t i = 0U; i < count; ++i) {
        if (events[i].flags != 0U) {
            nx_modbus_rtu_rx_error(server, events[i].timestamp_us);
            return false;
        }
        if (!nx_modbus_rtu_feed(server, events[i].byte,
                                events[i].timestamp_us)) {
            return false;
        }
    }
    if (nx_native_clock_advance(server->t3_5_us) != NX_SUCCESS ||
        !nx_modbus_rtu_poll(server, nx_time_now_us()) || app->sending ||
        app->direction_tx) {
        return false;
    }
    return true;
}

/** \brief A deadline-aware memory sink releases caller bytes before returning.
 */
static nx_result_t log_write(void* context, nx_log_level_t level,
                             const void* bytes, size_t length,
                             nx_time_us_t deadline) {
    memory_log_t* log = context;
    (void)level;
    if (nx_deadline_expired(deadline, nx_time_now_us())) {
        return NX_ERROR_TIMEOUT;
    }
    if (length > sizeof(log->bytes) - log->length) {
        return NX_ERROR_OVERFLOW;
    }
    memcpy(&log->bytes[log->length], bytes, length);
    log->length += length;
    return NX_SUCCESS;
}

/** \brief The synchronous memory sink has no queued work to flush. */
static nx_result_t log_flush(void* context, nx_time_us_t deadline) {
    (void)context;
    return nx_deadline_expired(deadline, nx_time_now_us()) ? NX_ERROR_TIMEOUT
                                                           : NX_SUCCESS;
}

/** \brief Demonstrate authorized and rejected writes through real model IO. */
int main(void) {
    nx_platform_start_result_t startup = nx_platform_start();
    if (startup.primary != NX_SUCCESS || startup.cleanup != NX_SUCCESS ||
        startup.remaining_effects != 0U) {
        return 1;
    }
    application_t app = {0};
    nx_request_initialize(&app.transmit.base);
    nx_native_uart_config_t uart = {NX_UART_RX_EVENTS, app.receive,      32U,
                                    app.wire,          sizeof(app.wire), true};
    if (nx_native_clock_configure(true, 0U) != NX_SUCCESS ||
        nx_native_uart_model_configure(nx_factory_uart_ref(NX_UART_REF_CONSOLE),
                                       &uart) != NX_SUCCESS) {
        return 2;
    }
    nx_modbus_config_t config = {
        .address = 17U,
        .baudrate = 115200U,
        .bits_per_character = 10U,
        .tx_timeout_us = 100000U,
        .abort_timeout_us = 1000U,
        .registers = {&app, read_registers, authorize, write_registers},
        .port = {&app, now_us, direction, write_bytes, drain,
                 abort_and_settle}};
    nx_modbus_rtu_t server;
    if (!nx_modbus_rtu_init(&server, &config) ||
        nx_native_clock_advance(server.t3_5_us) != NX_SUCCESS ||
        !nx_modbus_rtu_poll(&server, nx_time_now_us()) ||
        !exchange(&app, &server, 2U) || app.values[2] != 0x1234U ||
        nx_native_uart_model_transmitted(
            nx_factory_uart_ref(NX_UART_REF_CONSOLE)) != 8U ||
        nx_modbus_crc16(app.wire, 8U) != 0U) {
        return 3;
    }
    size_t begin = nx_native_uart_model_transmitted(
        nx_factory_uart_ref(NX_UART_REF_CONSOLE));
    if (!exchange(&app, &server, 7U) || app.values[7] != 0U ||
        nx_native_uart_model_transmitted(
            nx_factory_uart_ref(NX_UART_REF_CONSOLE)) != begin + 5U ||
        app.wire[begin + 1U] != 0x86U ||
        app.wire[begin + 2U] != NX_MODBUS_DEVICE_BUSY ||
        nx_modbus_crc16(&app.wire[begin], 5U) != 0U) {
        return 4;
    }
    memory_log_t memory = {0};
    nx_log_t logger;
    nx_log_sink_port_t sink = {&memory, log_write, log_flush};
    static const char message[] =
        "Authorized bank write; protected bank unchanged";
    if (nx_log_init(&logger, sink, NX_LOG_INFO) != NX_SUCCESS ||
        nx_log_write(&logger, NX_LOG_INFO, message, sizeof(message) - 1U,
                     nx_deadline_after(nx_time_now_us(), 1000U)) !=
            NX_SUCCESS ||
        nx_log_flush(&logger, nx_deadline_after(nx_time_now_us(), 1000U)) !=
            NX_SUCCESS ||
        nx_log_stop(&logger) != NX_SUCCESS ||
        memory.length != sizeof(message) - 1U || !nx_modbus_rtu_stop(&server) ||
        nx_platform_stop() != NX_SUCCESS) {
        return 5;
    }
    puts("Native Modbus: authorized write, interlock rejection, UART TC "
         "settlement");
    return 0;
}
