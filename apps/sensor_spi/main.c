/**
 * \file            main.c
 * \brief           BMP280 application using an explicit Native SPI fixture
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-10-09
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "nexus/components/bmp280_spi.h"
#include "nexus/io/native/model.h"
#include "nexus_bindings.h"
#include "nexus_factory.h"
#include <stdio.h>

/** \brief Inject the same clock domain into the sensor's narrow transport. */
static nx_time_us_t read_clock(void* context) {
    (void)context;
    return nx_time_now_us();
}

/** \brief Populate Bosch reference registers; this represents no PCB wiring. */
static void registers_initialize(uint8_t* registers) {
    registers[0x50] = 0x58;
    const int32_t calibration[12] = {27504, 26435, -1000, 36477, -10685, 3024,
                                     2855,  140,   -7,    15500, -14600, 6000};
    for (size_t i = 0U; i < 12U; ++i) {
        uint16_t value = (uint16_t)calibration[i];
        registers[8U + i * 2U] = (uint8_t)value;
        registers[9U + i * 2U] = (uint8_t)(value >> 8U);
    }
    const uint32_t raw[2] = {415148U, 519888U};
    for (size_t sample = 0U; sample < 2U; ++sample) {
        for (size_t i = 0U; i < 3U; ++i) {
            unsigned shift = (unsigned)(16U - i * 8U);
            registers[0x77U + sample * 3U + i] =
                (uint8_t)((raw[sample] << 4U) >> shift);
        }
    }
}

/** \brief Own the register model, device and conversion scheduling explicitly.
 */
int main(void) {
    nx_platform_start_result_t startup = nx_platform_start();
    if (startup.primary != NX_SUCCESS) {
        return 1;
    }
    uint8_t registers[128] = {0};
    registers_initialize(registers);
    if (nx_native_clock_configure(true, 0U) != NX_SUCCESS ||
        nx_native_spi_model_configure(
            nx_factory_spi_device(NX_SPI_DEVICE_ID_SENSOR0), registers,
            sizeof(registers)) != NX_SUCCESS) {
        return 2;
    }
    nx_bmp280_t sensor = {0};
    nx_clock_t clock = {read_clock, NULL};
    nx_time_us_t deadline = nx_deadline_after(nx_time_now_us(), 20000U);
    nx_bmp280_transport_port_t transport = nx_bmp280_spi_transport(
        nx_factory_spi_device(NX_SPI_DEVICE_ID_SENSOR0), clock);
    if (nx_bmp280_init(&sensor, transport, deadline) != NX_SUCCESS ||
        nx_bmp280_begin(&sensor, deadline) != NX_SUCCESS) {
        return 3;
    }
    /* The application chooses when to poll. No driver worker or sleep is
     * hidden. */
    if (nx_native_clock_advance(10000U) != NX_SUCCESS) {
        return 4;
    }
    nx_bmp280_sample_t sample;
    if (nx_bmp280_poll(&sensor, &sample) != NX_SUCCESS ||
        sample.centi_celsius != 2508 || sample.pressure_pa != 100653U) {
        return 5;
    }
    nx_bmp280_stop(&sensor);
    puts("Native BMP280: 25.08 C, 100653 Pa; physical qualification not "
         "executed");
    return nx_platform_stop() == NX_SUCCESS ? 0 : 6;
}
