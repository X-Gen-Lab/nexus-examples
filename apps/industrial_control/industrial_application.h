/**
 * \file            industrial_application.h
 * \brief           Finite Native industrial composition with explicit budgets
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-02
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#ifndef NEXUS_EXAMPLE_INDUSTRIAL_APPLICATION_H
#define NEXUS_EXAMPLE_INDUSTRIAL_APPLICATION_H
#include "nexus/components/bmp280.h"
#include "nexus/components/storage.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EXAMPLE_INDUSTRIAL_SCHEMA     1U
#define EXAMPLE_INDUSTRIAL_PERIOD_KEY 1U
#define EXAMPLE_INDUSTRIAL_MIN_PERIOD 50000U

/** \brief Product budgets; all times are monotonic microseconds, never ticks.
 */
typedef struct {
    uint32_t period_us;
    uint32_t sample_limit;
    uint32_t operation_budget_us;
    uint32_t shutdown_budget_us;
    uint32_t watchdog_timeout_us;
} example_industrial_config_t;

/** \brief Observed finite software behavior; contains no hardware
 * qualification. */
typedef struct {
    nx_bmp280_sample_t sample;
    nx_storage_status_t storage_status;
    nx_result_t startup_primary;
    nx_result_t startup_cleanup;
    uint32_t startup_remaining_effects;
    uint32_t samples;
    uint32_t initial_period_us;
    uint32_t durable_period_us;
    uint32_t modbus_replies;
    uint32_t max_modbus_replies_per_turn;
    uint32_t rejected_writes;
    uint32_t spi_completions;
    uint32_t timer_events;
    uint32_t stop_events;
    uint32_t watchdog_feeds;
    bool restored_settings;
    bool committed_in_maintenance;
    bool joined;
    bool platform_stopped;
    bool storage_retained;
    bool watchdog_retained;
    bool watchdog_expired;
} example_industrial_report_t;

/**
 * \brief           Run one finite Native reference with a dedicated open store
 * \param[in,out]   storage: Open two-bank store with persistent workspace
 * \param[in]       config: Finite budgets; period 50..1000 ms, 1..100 samples
 * \param[out]      report: Final observations; no concurrent readers
 * \return          SUCCESS, INVALID, storage mapping or runtime failure
 * \note            One control task and one separately joined SPI executor.
 *                  UART/Modbus, sensor, Settings and watchdog policy stay in
 *                  the control task. The Native reference injects Bosch
 *                  registers and finite Modbus requests through public model
 *                  ports; no MCU wiring or Flash region is inferred. No heap
 *                  or implicit worker is introduced. The store is borrowed
 *                  exclusively during the call; no parallel invocation.
 *                  Failed commit requires reopening the underlying store.
 *                  Retained I/O keeps static application storage alive and
 *                  forbids another run.
 */
nx_result_t example_industrial_run(nx_storage_t* storage,
                                   const example_industrial_config_t* config,
                                   example_industrial_report_t* report);

#ifdef __cplusplus
}
#endif
#endif /* NEXUS_EXAMPLE_INDUSTRIAL_APPLICATION_H */
