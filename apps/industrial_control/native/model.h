/**
 * \file            model.h
 * \brief           Explicit Native device facts for the industrial reference
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-16
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#ifndef NEXUS_EXAMPLE_INDUSTRIAL_NATIVE_MODEL_H
#define NEXUS_EXAMPLE_INDUSTRIAL_NATIVE_MODEL_H
#include "nexus/components/modbus_rtu.h"
#include "nexus/io/spi.h"
#include "nexus/io/uart.h"
#include "nexus/io/watchdog.h"
#include "nexus/os/wait.h"

/** \brief Static model bytes borrowed by the selected SPI and UART providers.
 * \note One invocation owner retains this storage through platform withdrawal.
 */
typedef struct {
    uint8_t registers[128];
    nx_uart_rx_event_t receive[32];
    uint8_t wire[128];
    const nx_uart_port_t* uart;
} example_industrial_model_t;

/** \brief Read-only application observations for one synchronous model
 * exchange. \note All fields outlive the call; the helper retains no
 * application pointers.
 */
typedef struct {
    nx_modbus_rtu_t* protocol;
    nx_wait_port_t changed;
    const uint16_t* registers;
    const bool* transmitting;
    const bool* direction_tx;
} example_industrial_protocol_model_t;

/**
 * \brief           Select advancing Native time before creating application
 * tasks \return          SUCCESS or the explicit Native model configuration
 * error \note            Startup task only; no scheduler or worker is created.
 */
nx_result_t example_industrial_model_clock(void);

/**
 * \brief           Bind the reviewed Bosch calibration and sample register
 * model \param[in,out]   model: Static unused model storage, retained through
 * withdrawal \param[in]       endpoint: Selected public SPI endpoint \return
 * SUCCESS or model configuration failure \note            Startup task only,
 * before the unique SPI executor starts.
 */
nx_result_t example_industrial_model_sensor(example_industrial_model_t* model,
                                            const nx_spi_endpoint_t* endpoint);

/**
 * \brief           Bind explicit RX events and TX observations to the UART
 * model \param[in,out]   model: Static model storage retained through
 * withdrawal \param[in]       uart: Selected public UART interface \return
 * SUCCESS or model configuration failure \note            Startup task only; no
 * request is submitted or borrowed here.
 */
nx_result_t example_industrial_model_uart(example_industrial_model_t* model,
                                          const nx_uart_port_t* uart);

/**
 * \brief           Inject one finite request and inspect its actual RTU
 * response \param[in,out]   model: Live UART model and retained byte storage
 * \param[in]       application: Same-task protocol and application observations
 * \param[in]       function: Reference Modbus function code
 * \param[in]       index: Reference register address
 * \param[in]       value: Request value or register count
 * \param[in]       expected: Exact expected response length
 * \return          True only after the actual response, CRC and TC checks pass
 * \note            Single control task; two explicit 2 ms monotonic waits use
 *                  the live notification port. Application TX callbacks own
 *                  their deadlines and settlement; this helper revokes no loan.
 */
bool example_industrial_model_exchange(
    example_industrial_model_t* model,
    const example_industrial_protocol_model_t* application, uint8_t function,
    uint16_t index, uint16_t value, size_t expected);

/**
 * \brief           Model a new boot before enabling the irreversible watchdog
 * \param[in]       watchdog: Selected watchdog; previous invocation is
 * quiescent \note            Startup task only. This is not a hardware disable
 * procedure.
 */
void example_industrial_model_watchdog_boot(const nx_watchdog_port_t* watchdog);

/**
 * \brief           Observe Native watchdog expiry after the application's work
 * \param[in]       watchdog: Same watchdog used by this finite invocation
 * \return          The actual model expiry observation; no physical
 * qualification \note            Called by the sole control task; it changes no
 * borrow.
 */
bool example_industrial_model_watchdog_expired(
    const nx_watchdog_port_t* watchdog);

#endif /* NEXUS_EXAMPLE_INDUSTRIAL_NATIVE_MODEL_H */
