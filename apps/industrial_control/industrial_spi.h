/**
 * \file            industrial_spi.h
 * \brief           Product-owned settled BMP280 transport through one SPI owner
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-02
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#ifndef NEXUS_EXAMPLE_INDUSTRIAL_SPI_H
#define NEXUS_EXAMPLE_INDUSTRIAL_SPI_H
#include "nexus/arch/atomic.h"
#include "nexus/components/bmp280.h"
#include "nexus/components/spi_owner.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EXAMPLE_INDUSTRIAL_SPI_BYTES 25U

/** \brief Exact persistent transaction storage; never borrows BMP280's stack.
 */
typedef struct {
    nx_bus_owner_t* owner;
    const nx_spi_endpoint_t* endpoint;
    nx_wait_port_t changed;
    nx_clock_t clock;
    nx_request_t request;
    nx_spi_owner_operation_t operation;
    nx_owner_ticket_t ticket;
    uint8_t tx[EXAMPLE_INDUSTRIAL_SPI_BYTES];
    uint8_t rx[EXAMPLE_INDUSTRIAL_SPI_BYTES];
    uint32_t stopping;
    nx_time_us_t next_tick;
    uint32_t period_us;
    uint32_t timer_events;
    uint32_t completed;
    uint32_t stop_events;
    bool pending;
    bool cancel_requested;
} example_industrial_spi_t;

/**
 * \brief           Bind unused product storage before starting its consumer
 * \param[out]      bridge: Fresh caller-owned storage
 * \param[in,out]   owner: Separately running unique SPI executor and live guard
 * \param[in]       endpoint: Explicit endpoint on that owner's controller
 * \param[in]       changed: Live single-consumer completion/stop notification
 * \param[in]       clock: Same advancing monotonic domain as changed and owner
 * \param[in]       period_us: Nonzero periodic readiness interval
 * \return          SUCCESS or INVALID, with no admitted request
 * \note            Task-only startup. All supplied objects outlive outstanding
 *                  requests, both tasks and notification publications.
 */
nx_result_t example_industrial_spi_open(example_industrial_spi_t* bridge,
                                        nx_bus_owner_t* owner,
                                        const nx_spi_endpoint_t* endpoint,
                                        nx_wait_port_t changed,
                                        nx_clock_t clock, uint32_t period_us);

/**
 * \brief           Supply BMP280's synchronous settled transport contract
 * \param[in,out]   bridge: Live initialized product bridge, one control task
 * \return          Explicit transport; no I/O, task, allocation or registry
 * \note            Each exchange copies at most 25 bytes into persistent
 *                  storage before admission. Only SETTLED permits reuse and RX
 *                  copy-back. Timeout/cancellation can retain bridge storage,
 *                  but never retains caller stack pointers. A retained request
 *                  rejects new exchanges until join succeeds. Timer selection
 *                  only accounts for due periods; it never reenters a driver.
 */
nx_bmp280_transport_port_t
example_industrial_spi_transport(example_industrial_spi_t* bridge);

/**
 * \brief           Request cooperative stop and notify the one consumer
 * \param[in,out]   bridge: Live storage; joins must precede its reclamation
 * \note            Task publisher only. Calls observing stop reject admission.
 *                  A racing exchange can already be admitted and must drain.
 *                  No request, buffer or owner is revoked.
 */
void example_industrial_spi_stop(example_industrial_spi_t* bridge);

/**
 * \brief           Cancel/drain a retained transaction without servicing SPI
 * \param[in,out]   bridge: Same single control task and persistent buffers
 * \param[in]       deadline: Finite absolute deadline in the bound clock domain
 * \return          SUCCESS after request settlement; error retains storage
 * \note            The separate owner must continue progressing. This joins the
 *                  request only; the application still joins the owner task and
 *                  every publisher before destroying notification storage.
 */
nx_result_t example_industrial_spi_join(example_industrial_spi_t* bridge,
                                        nx_time_us_t deadline);

#ifdef __cplusplus
}
#endif
#endif /* NEXUS_EXAMPLE_INDUSTRIAL_SPI_H */
