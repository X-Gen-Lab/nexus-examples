/**
 * \file            main.c
 * \brief           Public SPI4 drain and fixed-run pause/resume application
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-10-28
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "nexus/core/time.h"
#include "nexus/io/spi.h"
#include "nexus_bindings.h"
#include "nexus_factory_strong.h"

static nx_spi_request_t s_requests[2];
static _Alignas(32) uint8_t s_tx[32] = {0x12U, 0x34U};
static _Alignas(32) uint8_t s_rx[32];

/** \brief Debugger-visible observations; no physical result is inferred. */
volatile uint32_t example_settled_requests;
volatile nx_result_t example_transfer_result;
volatile size_t example_transferred;
volatile int example_runtime_result;

/** \brief Keep static buffers and every unfinished responsibility alive. */
static _Noreturn void retain(void) {
    for (;;) {
    }
}

/** \brief Cleanup makes no clock reads after Platform stop begins. */
static int close_platform(int result) {
    for (unsigned attempt = 0U; attempt < 64U; ++attempt) {
        if (nx_platform_stop() == NX_SUCCESS) {
            example_runtime_result = result;
            return result;
        }
    }
    example_runtime_result = result == 0 ? 7 : result;
    retain();
}

/** \brief Cancel is a request; only acquire-observed settlement joins the loan.
 */
static bool transfer(const nx_spi_port_t* port,
                     const nx_spi_endpoint_t* endpoint, unsigned round) {
    nx_spi_request_t* request = &s_requests[round];
    nx_request_initialize(&request->base);
    if (nx_spi_request_prepare(request, s_tx, s_rx, sizeof(s_tx),
                               nx_deadline_after(nx_time_now_us(), 10000U)) !=
            NX_SUCCESS ||
        nx_spi_endpoint_submit(endpoint, request) != NX_SUCCESS) {
        return false;
    }
    (void)nx_spi_port_cancel(port, request);
    for (unsigned attempt = 0U;
         attempt < 64U &&
         nx_request_state(&request->base) != NX_REQUEST_SETTLED;
         ++attempt) {
        (void)nx_spi_port_service(port);
    }
    if (nx_request_state(&request->base) != NX_REQUEST_SETTLED) {
        (void)close_platform(3);
        if (nx_request_state(&request->base) != NX_REQUEST_SETTLED) {
            retain();
        }
        return false;
    }
    nx_result_t result;
    size_t count;
    if (nx_request_result(&request->base, &result, &count) != NX_SUCCESS) {
        return false;
    }
    example_transfer_result = result;
    example_transferred = count;
    ++example_settled_requests;
    return result == NX_SUCCESS || result == NX_ERROR_CANCELLED;
}

/** \brief Own two finite submissions and one explicitly joined clock boundary.
 */
int main(void) {
    nx_clock_domain_t* domain = nx_platform_runtime_domain();
    nx_platform_start_result_t started = nx_platform_start();
    if (domain == NULL || started.primary != NX_SUCCESS ||
        started.cleanup != NX_SUCCESS || started.remaining_effects != 0U) {
        return close_platform(1);
    }
    const nx_spi_port_t* port = nx_factory_spi_ref(NX_SPI_REF_BUS4);
    const nx_spi_endpoint_t* endpoint =
        nx_factory_spi_device_ref(NX_SPI_DEVICE_REF_DEVICE4);
    if (!nx_spi_endpoint_on_port(endpoint, port) ||
        !transfer(port, endpoint, 0U)) {
        return close_platform(2);
    }
    nx_time_us_t before = nx_time_now_us();
    nx_result_t paused = NX_ERROR_BUSY;
    for (unsigned attempt = 0U; attempt < 64U && paused == NX_ERROR_BUSY;
         ++attempt) {
        paused = nx_platform_runtime_pause();
    }
    nx_clock_barrier_t barrier;
    if (paused != NX_SUCCESS || nx_time_now_us() < before ||
        nx_clock_domain_close(domain, &barrier) != NX_SUCCESS ||
        nx_clock_domain_quiescent(barrier) != NX_SUCCESS) {
        return close_platform(4);
    }
    nx_platform_start_result_t resumed = nx_platform_runtime_resume();
    if (resumed.primary != NX_SUCCESS || resumed.cleanup != NX_SUCCESS ||
        resumed.remaining_effects != 0U || nx_time_now_us() < before ||
        !transfer(port, endpoint, 1U)) {
        return close_platform(5);
    }
    return close_platform(0);
}
