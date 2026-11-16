/**
 * \file            native_runtime.h
 * \brief           Explicit POSIX clocks and kernel entropy for the gateway
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-02
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#ifndef NEXUS_EXAMPLE_GATEWAY_NATIVE_RUNTIME_H
#define NEXUS_EXAMPLE_GATEWAY_NATIVE_RUNTIME_H
#include "nexus/core/time.h"
#ifdef __cplusplus
extern "C" {
#endif
/**
 * \brief Read the host's unchanged CLOCK_MONOTONIC microsecond domain.
 * \param[in] context: Unused, no active-instance selector.
 * \return Finite monotonic time or NEVER on clock error/exhaustion.
 * \note Native reference only; no MCU clock or physical timing qualification.
 */
nx_time_us_t example_gateway_host_now(void* context);
/**
 * \brief Perform one bounded host wait without progressing any network owner.
 * \param[in] context: Unused; no task or worker is created.
 * \param[in] deadline: Finite absolute time in the host monotonic domain.
 * \return SUCCESS, BUSY after EINTR, TIMEOUT, INVALID or IO.
 */
nx_result_t example_gateway_host_wait(void* context, nx_time_us_t deadline);
/**
 * \brief Copy exactly one kernel random word without an alternate seed.
 * \param[in] context: Unused product entropy context.
 * \param[out] word: Written only after a complete getrandom result.
 * \return SUCCESS, BUSY for transient refusal, INVALID or IO.
 * \note Host entropy policy does not qualify the MCU entropy provider.
 */
nx_result_t example_gateway_host_entropy(void* context, uint32_t* word);
/**
 * \brief Read host UTC for the explicitly trusted Native reference policy.
 * \param[in] context: Unused; trust remains the product's declaration.
 * \param[out] seconds: Unix seconds, written only on a valid 1970..9999 result.
 * \return SUCCESS, INVALID or IO; no monotonic-to-UTC conversion or fallback.
 */
nx_result_t example_gateway_host_utc(void* context, int64_t* seconds);
#ifdef __cplusplus
}
#endif
#endif /* NEXUS_EXAMPLE_GATEWAY_NATIVE_RUNTIME_H */
