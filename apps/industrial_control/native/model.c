/**
 * \file            model.c
 * \brief           Native sensor facts and finite Modbus stimulus
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-16
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "model.h"
#include "nexus/io/native/model.h"
#include <string.h>

#define MODEL_GAP_US 2000U

static nx_time_us_t now(void* context) {
    (void)context;
    return nx_time_now_us();
}
static bool time_due(void* context) {
    return now(NULL) >= *(const nx_time_us_t*)context;
}
static nx_result_t
wait_time(const example_industrial_protocol_model_t* application,
          nx_time_us_t deadline) {
    return nx_wait_until(&application->changed, time_due, &deadline, deadline);
}

nx_result_t example_industrial_model_clock(void) {
    return nx_native_clock_configure(false, 0U);
}

/** \brief Product fixture data from the existing Bosch calibration reference.
 */
static void sensor_registers(example_industrial_model_t* model) {
    model->registers[0x50] = 0x58U;
    const int32_t calibration[12] = {27504, 26435, -1000, 36477, -10685, 3024,
                                     2855,  140,   -7,    15500, -14600, 6000};
    for (size_t index = 0U; index < 12U; ++index) {
        uint16_t value = (uint16_t)calibration[index];
        model->registers[8U + index * 2U] = (uint8_t)value;
        model->registers[9U + index * 2U] = (uint8_t)(value >> 8U);
    }
    const uint32_t raw[2] = {415148U, 519888U};
    for (size_t sample = 0U; sample < 2U; ++sample) {
        for (size_t index = 0U; index < 3U; ++index) {
            unsigned shift = (unsigned)(16U - index * 8U);
            model->registers[0x77U + sample * 3U + index] =
                (uint8_t)((raw[sample] << 4U) >> shift);
        }
    }
}

nx_result_t example_industrial_model_sensor(example_industrial_model_t* model,
                                            const nx_spi_endpoint_t* endpoint) {
    sensor_registers(model);
    return nx_native_spi_model_configure(endpoint, model->registers,
                                         sizeof(model->registers));
}

nx_result_t example_industrial_model_uart(example_industrial_model_t* model,
                                          const nx_uart_port_t* uart) {
    nx_native_uart_config_t config = {
        NX_UART_RX_EVENTS, model->receive,      32U,
        model->wire,       sizeof(model->wire), true};
    model->uart = uart;
    return nx_native_uart_model_configure(uart, &config);
}

bool example_industrial_model_exchange(
    example_industrial_model_t* model,
    const example_industrial_protocol_model_t* application, uint8_t function,
    uint16_t index, uint16_t value, size_t expected) {
    uint8_t request[8] = {17U,
                          function,
                          (uint8_t)(index >> 8U),
                          (uint8_t)index,
                          (uint8_t)(value >> 8U),
                          (uint8_t)value,
                          0U,
                          0U};
    uint16_t crc = nx_modbus_crc16(request, 6U);
    request[6] = (uint8_t)crc;
    request[7] = (uint8_t)(crc >> 8U);
    if (wait_time(application, nx_deadline_after(now(NULL), MODEL_GAP_US)) !=
        NX_SUCCESS) {
        return false;
    }
    size_t begin = nx_native_uart_model_transmitted(model->uart);
    for (size_t byte = 0U; byte < sizeof(request); ++byte) {
        if (nx_native_uart_model_receive(model->uart, request[byte], 0U) !=
            NX_SUCCESS) {
            return false;
        }
    }
    nx_uart_rx_event_t events[8];
    size_t count = 0U;
    if (nx_uart_port_read_events(model->uart, events, 8U, &count) !=
            NX_SUCCESS ||
        count != 8U) {
        return false;
    }
    for (size_t byte = 0U; byte < count; ++byte) {
        if (events[byte].flags != 0U ||
            !nx_modbus_rtu_feed(application->protocol, events[byte].byte,
                                events[byte].timestamp_us)) {
            nx_modbus_rtu_rx_error(application->protocol,
                                   events[byte].timestamp_us);
            return false;
        }
    }
    if (wait_time(application, nx_deadline_after(now(NULL), MODEL_GAP_US)) !=
            NX_SUCCESS ||
        !nx_modbus_rtu_poll(application->protocol, now(NULL)) ||
        *application->transmitting || *application->direction_tx) {
        return false;
    }
    size_t end = nx_native_uart_model_transmitted(model->uart);
    if (end - begin != expected ||
        nx_modbus_crc16(&model->wire[begin], expected) != 0U) {
        return false;
    }
    const uint8_t* response = &model->wire[begin];
    if (function == 3U) {
        if (response[1] != 3U || response[2] != 8U ||
            response[3] != (uint8_t)(application->registers[0] >> 8U) ||
            response[4] != (uint8_t)application->registers[0] ||
            response[5] != (uint8_t)(application->registers[1] >> 8U) ||
            response[6] != (uint8_t)application->registers[1] ||
            response[7] != (uint8_t)(application->registers[2] >> 8U) ||
            response[8] != (uint8_t)application->registers[2]) {
            return false;
        }
    } else if (index == 4U) {
        if (memcmp(response, request, sizeof(request)) != 0) {
            return false;
        }
    } else if (response[1] != 0x86U ||
               response[2] != NX_MODBUS_ILLEGAL_ADDRESS) {
        return false;
    }
    return true;
}

void example_industrial_model_watchdog_boot(
    const nx_watchdog_port_t* watchdog) {
    nx_native_watchdog_model_boot(watchdog, NX_RESET_POWER_ON);
}

bool example_industrial_model_watchdog_expired(
    const nx_watchdog_port_t* watchdog) {
    return nx_native_watchdog_model_expired(watchdog);
}
