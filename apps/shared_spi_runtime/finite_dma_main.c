/**
 * \file            finite_dma_main.c
 * \brief           Explicit SPI DMA endpoint, finite request and cold exit
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-23
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "finite_dma.h"
#include "nexus_bindings.h"
#include "nexus_factory_strong.h"

static example_finite_spi_t s_transfer;
static _Alignas(4) const uint8_t s_tx[4] = {0x9FU, 0xFFU, 0xFFU, 0xFFU};
static _Alignas(4) uint8_t s_rx[4];

/** \brief Own execution; a retained failure never authorizes platform cleanup.
 */
int main(void) {
    nx_platform_start_result_t started = nx_platform_start();
    if (started.primary != NX_SUCCESS || started.cleanup != NX_SUCCESS ||
        started.remaining_effects != 0U) {
        return 1;
    }
    const nx_spi_port_t* bus = nx_factory_spi_ref(NX_SPI_REF_SPI0);
    const nx_spi_endpoint_t* endpoint =
        nx_factory_spi_device_ref(NX_SPI_DEVICE_REF_SENSOR0);
    nx_result_t result =
        example_finite_spi_start(&s_transfer, bus, endpoint, s_tx, s_rx,
                                 sizeof(s_tx), nx_time_now_us(), 100000U);
    while (s_transfer.pending) {
        result = example_finite_spi_step(&s_transfer);
        if (nx_request_state(&s_transfer.request.base) ==
            NX_REQUEST_QUARANTINED) {
            /* This software reference retains its static spans and live
             * infrastructure. A product owns physical recovery or reset.
             */
            return 2;
        }
    }
    if (result != NX_SUCCESS) {
        /* Idle recovery cannot revoke a payload loan; pending is already false.
         * Recovery does not reissue this command or erase its original result.
         */
        (void)nx_spi_port_recover(bus);
    }
    nx_result_t stopped = example_finite_spi_finish(&s_transfer);
    if (stopped != NX_SUCCESS) {
        return 3;
    }
    stopped = nx_platform_stop();
    return result == NX_SUCCESS && stopped == NX_SUCCESS ? 0 : 4;
}
