/**
 * \file            finite_dma.c
 * \brief           Single-executor finite SPI DMA application lifecycle
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-23
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "finite_dma.h"

/** \brief Check full control separation without overflowing an end pointer. */
static bool separate_payload(const void* payload, size_t length,
                             const void* control, size_t control_size) {
    uintptr_t first = (uintptr_t)payload;
    uintptr_t owner = (uintptr_t)control;
    if (length == 0U || length - 1U > UINTPTR_MAX - first) {
        return false;
    }
    return first <= owner ? length <= owner - first
                          : first - owner >= control_size;
}

nx_result_t example_finite_spi_start(example_finite_spi_t* app,
                                     const nx_spi_port_t* port,
                                     const nx_spi_endpoint_t* endpoint,
                                     const uint8_t* tx, uint8_t* rx,
                                     size_t length, nx_time_us_t now,
                                     uint64_t budget) {
    if (app == NULL || port == NULL || endpoint == NULL ||
        (tx == NULL && rx == NULL) || length == 0U) {
        return NX_ERROR_INVALID;
    }
    if ((tx != NULL && !separate_payload(tx, length, app, sizeof(*app))) ||
        (rx != NULL && !separate_payload(rx, length, app, sizeof(*app)))) {
        return NX_ERROR_INVALID;
    }
    if (app->pending) {
        return NX_ERROR_BUSY;
    }
    if (app->stopping || app->stopped ||
        (app->port != NULL && app->port != port)) {
        return NX_ERROR_STATE;
    }
    if (!nx_spi_endpoint_on_port(endpoint, port)) {
        return NX_ERROR_INVALID;
    }
    nx_time_us_t deadline;
    nx_result_t result = nx_deadline_finite_after(now, budget, &deadline);
    if (result != NX_SUCCESS) {
        return result;
    }
    if (app->port == NULL) {
        nx_request_initialize(&app->request.base);
        app->port = port;
    }
    result = nx_spi_request_prepare(&app->request, tx, rx, length, deadline);
    if (result == NX_SUCCESS) {
        result = nx_spi_endpoint_submit(endpoint, &app->request);
    }
    app->pending = result == NX_SUCCESS;
    app->result = result;
    app->transferred = 0U;
    return result;
}
nx_result_t example_finite_spi_step(example_finite_spi_t* app) {
    if (app == NULL || app->port == NULL) {
        return NX_ERROR_INVALID;
    }
    if (!app->pending) {
        return app->result;
    }
    nx_spi_port_service(app->port);
    if (nx_request_state(&app->request.base) != NX_REQUEST_SETTLED) {
        return NX_ERROR_BUSY;
    }
    nx_result_t result =
        nx_request_result(&app->request.base, &app->result, &app->transferred);
    if (result != NX_SUCCESS) {
        return result;
    }
    app->pending = false;
    if (app->result == NX_SUCCESS && app->transferred != app->request.length) {
        app->result = NX_ERROR_IO;
    }
    return app->result;
}
nx_result_t example_finite_spi_request_stop(example_finite_spi_t* app) {
    if (app == NULL || app->port == NULL) {
        return NX_ERROR_INVALID;
    }
    app->stopping = true;
    return app->pending ? nx_spi_port_cancel(app->port, &app->request)
                        : NX_SUCCESS;
}
nx_result_t example_finite_spi_finish(example_finite_spi_t* app) {
    if (app == NULL || app->port == NULL) {
        return NX_ERROR_INVALID;
    }
    if (app->stopped) {
        return NX_SUCCESS;
    }
    if (!app->stopping) {
        nx_result_t result = example_finite_spi_request_stop(app);
        if (result != NX_SUCCESS) {
            return result;
        }
    }
    if (app->pending) {
        (void)example_finite_spi_step(app);
        if (app->pending) {
            return NX_ERROR_BUSY;
        }
    }
    nx_result_t result = nx_spi_port_stop(app->port);
    app->stopped = result == NX_SUCCESS;
    return result;
}
