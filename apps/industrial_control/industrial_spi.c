/**
 * \file            industrial_spi.c
 * \brief           Product-owned settled copies and explicit multi-source wait
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-02
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "industrial_spi.h"
#include <string.h>

static bool settled(void* context) {
    example_industrial_spi_t* bridge = context;
    return bridge->pending &&
           nx_request_state(&bridge->request) == NX_REQUEST_SETTLED;
}
static bool timer_due(void* context) {
    example_industrial_spi_t* bridge = context;
    return bridge->clock.read(bridge->clock.context) >= bridge->next_tick;
}
static bool stop_due(void* context) {
    example_industrial_spi_t* bridge = context;
    return bridge->stop_events == 0U &&
           nx_atomic_u32_load_acquire(&bridge->stopping) != 0U;
}

/** \brief Skip elapsed periods in bounded arithmetic without recursive work. */
static void advance_timer(example_industrial_spi_t* bridge) {
    nx_time_us_t now = bridge->clock.read(bridge->clock.context);
    uint32_t remaining =
        bridge->period_us -
        (uint32_t)((now - bridge->next_tick) % bridge->period_us);
    bridge->next_tick = nx_deadline_after(now, remaining);
    if (bridge->timer_events != UINT32_MAX) {
        ++bridge->timer_events;
    }
}

/** \brief Cancel by stable slot identity; an observed settlement wins races. */
static nx_result_t cancel(example_industrial_spi_t* bridge) {
    if (!bridge->pending || bridge->cancel_requested || settled(bridge)) {
        return NX_SUCCESS;
    }
    nx_result_t result = nx_bus_owner_cancel(bridge->owner, bridge->ticket);
    if (result == NX_SUCCESS) {
        bridge->cancel_requested = true;
    }
    return result == NX_ERROR_STATE && settled(bridge) ? NX_SUCCESS : result;
}

static nx_result_t exchange(void* context, const uint8_t* tx, uint8_t* rx,
                            size_t length, nx_time_us_t deadline) {
    example_industrial_spi_t* bridge = context;
    if (bridge == NULL || bridge->owner == NULL || tx == NULL || length == 0U ||
        length > EXAMPLE_INDUSTRIAL_SPI_BYTES ||
        deadline == NX_DEADLINE_NEVER) {
        return NX_ERROR_INVALID;
    }
    if (bridge->pending) {
        return NX_ERROR_BUSY;
    }
    if (nx_atomic_u32_load_acquire(&bridge->stopping) != 0U) {
        return NX_ERROR_STATE;
    }
    if (nx_deadline_expired(deadline,
                            bridge->clock.read(bridge->clock.context))) {
        return NX_ERROR_TIMEOUT;
    }
    memcpy(bridge->tx, tx, length);
    memset(bridge->rx, 0, length);
    bridge->operation = (nx_spi_owner_operation_t){bridge->endpoint, bridge->tx,
                                                   bridge->rx, length};
    nx_result_t result = nx_request_prepare(&bridge->request, deadline);
    if (result != NX_SUCCESS) {
        return result;
    }
    result =
        nx_bus_owner_submit(bridge->owner, &bridge->request, &bridge->operation,
                            &bridge->changed, &bridge->ticket);
    if (result != NX_SUCCESS) {
        return result;
    }
    bridge->pending = true;
    bridge->cancel_requested = false;
    const nx_wait_source_t sources[] = {
        {settled, bridge}, {timer_due, bridge}, {stop_due, bridge}};
    size_t cursor = 0U;
    for (;;) {
        /* A ready timer never extends the operation budget. An already
         * published terminal result remains authoritative at the boundary. */
        if (nx_deadline_expired(deadline,
                                bridge->clock.read(bridge->clock.context)) &&
            !settled(bridge)) {
            (void)cancel(bridge);
            return NX_ERROR_TIMEOUT;
        }
        size_t selected = SIZE_MAX;
        nx_time_us_t wake =
            bridge->next_tick < deadline ? bridge->next_tick : deadline;
        result = nx_wait_select(&bridge->changed, sources, 3U, cursor, wake,
                                &selected);
        if (result != NX_SUCCESS) {
            (void)cancel(bridge);
            return result;
        }
        cursor = (selected + 1U) % 3U;
        if (selected == 0U) {
            size_t count = 0U;
            nx_result_t terminal = NX_ERROR_STATE;
            result = nx_request_result(&bridge->request, &terminal, &count);
            if (result != NX_SUCCESS) {
                return result;
            }
            bridge->pending = false;
            bridge->cancel_requested = false;
            ++bridge->completed;
            if (terminal == NX_SUCCESS && count != length) {
                return NX_ERROR_IO;
            }
            if (terminal == NX_SUCCESS && rx != NULL) {
                memcpy(rx, bridge->rx, length);
            }
            return terminal;
        }
        if (selected == 1U) {
            advance_timer(bridge);
        } else {
            ++bridge->stop_events;
            result = cancel(bridge);
            if (result != NX_SUCCESS) {
                return result;
            }
        }
    }
}

nx_result_t example_industrial_spi_open(example_industrial_spi_t* bridge,
                                        nx_bus_owner_t* owner,
                                        const nx_spi_endpoint_t* endpoint,
                                        nx_wait_port_t changed,
                                        nx_clock_t clock, uint32_t period_us) {
    if (bridge == NULL || owner == NULL || endpoint == NULL ||
        changed.arm == NULL || changed.wait == NULL || changed.wake == NULL ||
        clock.read == NULL || period_us == 0U) {
        return NX_ERROR_INVALID;
    }
    *bridge = (example_industrial_spi_t){.owner = owner,
                                         .endpoint = endpoint,
                                         .changed = changed,
                                         .clock = clock,
                                         .period_us = period_us};
    nx_request_initialize(&bridge->request);
    bridge->next_tick = nx_deadline_after(clock.read(clock.context), period_us);
    return NX_SUCCESS;
}

nx_bmp280_transport_port_t
example_industrial_spi_transport(example_industrial_spi_t* bridge) {
    return bridge == NULL
               ? (nx_bmp280_transport_port_t){0}
               : (nx_bmp280_transport_port_t){bridge, exchange, bridge->clock};
}

void example_industrial_spi_stop(example_industrial_spi_t* bridge) {
    if (bridge != NULL && bridge->owner != NULL) {
        nx_atomic_u32_store_release(&bridge->stopping, 1U);
        (void)bridge->changed.wake(bridge->changed.context);
    }
}

nx_result_t example_industrial_spi_join(example_industrial_spi_t* bridge,
                                        nx_time_us_t deadline) {
    if (bridge == NULL || bridge->owner == NULL ||
        deadline == NX_DEADLINE_NEVER) {
        return NX_ERROR_INVALID;
    }
    if (!bridge->pending) {
        return NX_SUCCESS;
    }
    nx_result_t result = cancel(bridge);
    if (result == NX_SUCCESS) {
        result = nx_wait_until(&bridge->changed, settled, bridge, deadline);
    }
    if (result == NX_SUCCESS) {
        bridge->pending = false;
        bridge->cancel_requested = false;
        ++bridge->completed;
    }
    return result;
}
