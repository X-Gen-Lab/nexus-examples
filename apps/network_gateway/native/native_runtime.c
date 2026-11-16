/**
 * \file            native_runtime.c
 * \brief           Stable host clock, explicit UTC and kernel entropy ports
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-02
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#define _POSIX_C_SOURCE 200809L
#include "native_runtime.h"
#include "nexus/network/lwip_adapter.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/random.h>
#include <time.h>

nx_time_us_t example_gateway_host_now(void* context) {
    (void)context;
    struct timespec value = {0};
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0 || value.tv_sec < 0 ||
        (uint64_t)value.tv_sec > (UINT64_MAX - UINT64_C(999999)) / 1000000U) {
        return NX_DEADLINE_NEVER;
    }
    const uint64_t micros =
        (uint64_t)value.tv_sec * 1000000U + (uint64_t)value.tv_nsec / 1000U;
    return micros == NX_DEADLINE_NEVER ? NX_DEADLINE_NEVER : micros;
}

nx_result_t example_gateway_host_wait(void* context, nx_time_us_t deadline) {
    (void)context;
    if (deadline == NX_DEADLINE_NEVER) {
        return NX_ERROR_INVALID;
    }
    const nx_time_us_t now = example_gateway_host_now(NULL);
    if (now == NX_DEADLINE_NEVER) {
        return NX_ERROR_IO;
    }
    if (now >= deadline) {
        return NX_ERROR_TIMEOUT;
    }
    const nx_time_us_t remaining = deadline - now;
    const struct timespec duration = {
        .tv_nsec = (long)((remaining < 1000U ? remaining : 1000U) * 1000U)};
    if (nanosleep(&duration, NULL) != 0) {
        return errno == EINTR ? NX_ERROR_BUSY : NX_ERROR_IO;
    }
    const nx_time_us_t after = example_gateway_host_now(NULL);
    return after == NX_DEADLINE_NEVER ? NX_ERROR_IO
           : after >= deadline        ? NX_ERROR_TIMEOUT
                                      : NX_SUCCESS;
}

nx_result_t example_gateway_host_entropy(void* context, uint32_t* word) {
    (void)context;
    if (word == NULL) {
        return NX_ERROR_INVALID;
    }
    uint32_t value = 0U;
    const ssize_t bytes = getrandom(&value, sizeof(value), GRND_NONBLOCK);
    if (bytes == (ssize_t)sizeof(value)) {
        *word = value;
        return NX_SUCCESS;
    }
    return bytes < 0 && (errno == EAGAIN || errno == EINTR) ? NX_ERROR_BUSY
                                                            : NX_ERROR_IO;
}

nx_result_t example_gateway_host_utc(void* context, int64_t* seconds) {
    (void)context;
    if (seconds == NULL) {
        return NX_ERROR_INVALID;
    }
    struct timespec value = {0};
    if (clock_gettime(CLOCK_REALTIME, &value) != 0 || value.tv_sec < 0 ||
        (uint64_t)value.tv_sec > UINT64_C(253402300799)) {
        return NX_ERROR_IO;
    }
    *seconds = (int64_t)value.tv_sec;
    return NX_SUCCESS;
}

uint32_t nx_lwip_port_now_ms(void) {
    const nx_time_us_t now = example_gateway_host_now(NULL);
    if (now == NX_DEADLINE_NEVER) {
        nx_lwip_port_assert("host monotonic clock unavailable");
    }
    return (uint32_t)(now / 1000U);
}
uint32_t nx_lwip_port_random_u32(void) {
    uint32_t word = 0U;
    if (example_gateway_host_entropy(NULL, &word) != NX_SUCCESS) {
        nx_lwip_port_assert("host kernel entropy unavailable");
    }
    return word;
}
void nx_lwip_port_assert(const char* message) {
    fprintf(stderr, "gateway stack invariant: %s\n", message);
    abort();
}
