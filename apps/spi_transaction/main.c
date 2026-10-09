/** Finite typed SPI transactions using public Nexus contracts only. */
#include "nexus_config.h"
#include "hal/base/nx_device.h"
#include "osal/osal.h"
#include "runtime/nx_runtime.h"
#include <string.h>
#if defined(NX_CONFIG_PLATFORM_NATIVE)
#include <stdio.h>
#endif

#ifndef NEXUS_EXAMPLE_SPI_DEVICE
#error "The application must explicitly select an available SPI controller"
#endif
#ifndef NEXUS_EXAMPLE_SPI_CS
#error "The application must select a board-defined logical SPI CS identifier"
#endif

enum { TX_BYTES = 8, TRANSACTION_BUDGET_MS = 100, WORKER_STACK_BYTES = 2048 };
static nx_device_ref_t controller;
static nx_device_spi_ref_t peripheral;
static nx_device_spi_ticket_t ticket;
/* These buffers remain alive even if a transaction or settlement fails. */
static const uint8_t tx[TX_BYTES] = {0x00, 0x17, 0x80, 0xff, 0x55, 0xaa, 0x42, 0x01};
static uint8_t rx[TX_BYTES];
volatile int spi_example_result = -1; /* -1 not run; 0 passed; 1 failed */
volatile nx_status_t spi_example_status = NX_OK;
volatile unsigned int spi_example_completed;
volatile bool spi_example_buffer_lease_possible;
const char* volatile spi_example_failure;

static bool checked(nx_status_t status, const char* operation) {
    if (status == NX_OK) return true;
    spi_example_status = status;
    spi_example_failure = operation;
    return false;
}

static bool verified(bool passed, const char* operation) {
    return passed || checked(NX_ERR_IO, operation);
}

/* One application owner drives service and observes settlement. service() may
 * wait inside the provider; its original transaction deadline includes queue
 * delay and bus contention. The outer budget bounds repeated service attempts. */
static bool settle(nx_status_t expected) {
    uint32_t started = 0;
    if (!verified(osal_get_time_ms(&started) == OSAL_OK, "settlement clock"))
        return false;
    for (;;) {
        nx_device_spi_result_t result = {NX_ERR_INVALID_STATE, false};
        if (!checked(nx_device_spi_poll(peripheral, ticket, &result), "SPI poll"))
            return false;
        if (result.settled) {
            spi_example_buffer_lease_possible = false;
            if (result.status == expected) return true;
            spi_example_status = result.status;
            spi_example_failure = "unexpected SPI terminal status";
            return false;
        }
        uint32_t now = 0;
        if (!verified(osal_get_time_ms(&now) == OSAL_OK, "settlement clock"))
            return false;
        if ((uint32_t)(now - started) >= TRANSACTION_BUDGET_MS)
            return checked(NX_ERR_TIMEOUT, "settlement deadline");
        /* A service error is not settlement. Poll must confirm the terminal
         * callback has returned before caller storage may be reused. */
        (void)nx_device_spi_service(controller);
        if (!verified(osal_task_delay(1) == OSAL_OK, "settlement yield"))
            return false;
    }
}

static int run_spi_example(void) {
    if (!checked(nx_device_open(NEXUS_EXAMPLE_SPI_DEVICE, NX_DEVICE_CLASS_SPI,
                                (uintptr_t)&controller, &controller), "controller open"))
        goto failed;
    const nx_spi_device_config_t config = {
        .cs_pin = NEXUS_EXAMPLE_SPI_CS, .speed = 1000000,
        .mode = NX_SPI_MODE_0, .bit_order = NX_SPI_BIT_ORDER_MSB
    };
    if (!checked(nx_device_spi_open(controller, &config, &peripheral), "SPI child open"))
        goto failed;
    nx_device_caps_t caps = {0};
    const uint32_t required = NX_DEVICE_CAP_SPI_DEVICES | NX_DEVICE_CAP_SPI_QUEUE |
                              NX_DEVICE_CAP_SPI_CANCEL;
    if (!checked(nx_device_spi_query(peripheral, &caps), "SPI child capabilities"))
        goto failed;
    if ((caps.flags & required) != required) {
        (void)checked(NX_ERR_NOT_SUPPORTED, "required typed SPI capabilities");
        goto failed;
    }

    nx_spi_transaction_t transaction = {
        .tx_data = tx, .rx_data = rx, .length = sizeof(tx),
        .timeout_ms = TRANSACTION_BUDGET_MS
    };
    memset(rx, 0xcc, sizeof(rx));
    spi_example_buffer_lease_possible = true;
    if (!checked(nx_device_spi_transfer(peripheral, &transaction), "synchronous SPI transfer"))
        goto failed;
    spi_example_buffer_lease_possible = false;
    /* Native's uninjected RX echoes TX. MCU execution of this same assertion
     * requires an explicit MOSI/MISO loopback fixture, not an unknown slave. */
    if (!verified(memcmp(rx, tx, sizeof(tx)) == 0, "synchronous RX matches TX"))
        goto failed;
    ++spi_example_completed;

    memset(rx, 0xcc, sizeof(rx));
    spi_example_buffer_lease_possible = true;
    if (!checked(nx_device_spi_submit(peripheral, &transaction, &ticket), "queued SPI submit") ||
        !verified(ticket.sequence != 0, "nonzero SPI ticket") ||
        !settle(NX_OK) || !verified(memcmp(rx, tx, sizeof(tx)) == 0, "queued RX matches TX"))
        goto failed;
    ++spi_example_completed;

    /* A queued cancel does not release the buffers. No service owner runs in
     * parallel, so the initial poll must still report an unsettled request. */
    memset(rx, 0xcc, sizeof(rx));
    spi_example_buffer_lease_possible = true;
    if (!checked(nx_device_spi_submit(peripheral, &transaction, &ticket), "cancel case submit") ||
        !checked(nx_device_spi_cancel(peripheral, ticket), "cancel request"))
        goto failed;
    nx_device_spi_result_t result = {NX_ERR_INVALID_STATE, false};
    if (!checked(nx_device_spi_poll(peripheral, ticket, &result), "cancel request poll") ||
        !verified(!result.settled && result.status == NX_ERR_BUSY,
                  "accepted cancellation is not settlement") ||
        !settle(NX_ERR_CANCELLED))
        goto failed;
    for (size_t i = 0; i < sizeof(rx); ++i)
        if (!verified(rx[i] == 0xcc, "cancelled queued RX remains untouched"))
            goto failed;
    ++spi_example_completed;

    /* The child owns a parent reference. Close the child before its controller;
     * failed close keeps both static reference values available for recovery. */
    if (!checked(nx_device_spi_close(peripheral), "SPI child close") ||
        !checked(nx_device_close(controller), "controller close"))
        goto failed;
    spi_example_result = 0;
    return 0;

failed:
    /* No implicit discard/recover/reset policy. Unknown or failed settlement
     * retains permanent buffers, refs and tickets; never claim successful close. */
    spi_example_result = 1;
#if defined(NX_CONFIG_PLATFORM_NATIVE)
    (void)fprintf(stderr, "SPI example failed: %s (status %u)\n",
                  spi_example_failure ? spi_example_failure : "unknown",
                  (unsigned int)spi_example_status);
#endif
    return 1;
}

#if defined(NX_CONFIG_OSAL_FREERTOS)
static void spi_task(void* unused) {
    (void)unused;
    (void)run_spi_example();
    /* Finite worker, running kernel remains application-owned until reset. */
}
#endif

int main(void) {
    if (!checked(nx_runtime_bootstrap(NULL), "runtime bootstrap")) {
        spi_example_result = 1;
        return 1;
    }
#if defined(NX_CONFIG_OSAL_FREERTOS)
    const osal_task_config_t config = {
        .name = "SPIExample", .func = spi_task,
        .priority = OSAL_TASK_PRIORITY_NORMAL, .stack_size = WORKER_STACK_BYTES
    };
    osal_task_handle_t task = NULL;
    if (!verified(osal_task_create(&config, &task) == OSAL_OK, "SPI task create")) {
        spi_example_result = 1;
        (void)nx_runtime_shutdown(NULL);
        return 1;
    }
    osal_start();
    (void)checked(NX_ERR_INVALID_STATE, "scheduler returned");
    spi_example_result = 1;
    return 1;
#else
    int result = run_spi_example();
#if defined(NX_CONFIG_PLATFORM_NATIVE)
    if (result == 0 && !checked(nx_runtime_shutdown(NULL), "runtime shutdown"))
        result = spi_example_result = 1;
    if (result == 0 && (puts("Native SPI model passed: synchronous echo, queued echo, "
                            "cancel settlement; 3 cases.") < 0 || fflush(stdout) != 0))
        result = spi_example_result = 1;
#endif
    return result;
#endif
}
