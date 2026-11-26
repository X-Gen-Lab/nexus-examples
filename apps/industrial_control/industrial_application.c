/**
 * \file            industrial_application.c
 * \brief           Finite product composition of real platform services
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-02
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "industrial_application.h"
#include "industrial_spi.h"
#include "native/model.h"
#include "nexus/arch/arch.h"
#include "nexus/components/modbus_rtu.h"
#include "nexus/components/settings.h"
#include "nexus/os/storage.h"
#include "nexus_bindings.h"
#include "nexus_factory_strong.h"
#include <string.h>

#define PRODUCT_TX_US    30000U
#define PRODUCT_ABORT_US 2000U
#define PRODUCT_PUMP_US  1000U

typedef struct {
    example_industrial_config_t config;
    example_industrial_report_t* report;
    nx_settings_t settings;
    uint8_t settings_image[64];
    nx_bmp280_t sensor;
    example_industrial_model_t model;
    example_industrial_spi_t bridge;
    nx_bus_owner_t owner;
    nx_owner_slot_t slot;
    nx_spi_owner_executor_t executor;
    nx_notify_t changed;
    nx_notify_t owner_event;
    nx_task_t worker;
    uint32_t worker_exit;
    nx_modbus_rtu_t modbus;
    nx_uart_tx_request_t transmit;
    uint16_t values[8];
    uint32_t period_us;
    uint32_t watchdog_min_us;
    bool transmitting;
    bool direction_tx;
    bool changed_open;
    bool owner_event_open;
    bool owner_open;
    bool worker_started;
    bool platform_open;
    bool modbus_open;
} application_t;

/* This finite application has one explicit invocation owner. Retained failure
 * keeps this storage alive and rejects another run; it is not a device
 * registry.
 */
static application_t s_application;
static _Alignas(16) uint8_t s_owner_stack[65536];
static bool s_retained;

static nx_time_us_t now(void* context) {
    (void)context;
    return nx_time_now_us();
}
static uintptr_t enter(void* context) {
    (void)context;
    return nx_arch_irq_save().value;
}
static void leave(void* context, uintptr_t value) {
    (void)context;
    nx_arch_irq_restore((nx_arch_irq_state_t){(uint32_t)value});
}
static bool period_valid(uint32_t period, uint32_t operation_budget) {
    return period >= EXAMPLE_INDUSTRIAL_MIN_PERIOD && period <= 1000000U &&
           period % 1000U == 0U && period >= operation_budget + 50000U;
}
static uint32_t decode(const uint8_t value[4]) {
    return (uint32_t)value[0] | ((uint32_t)value[1] << 8U) |
           ((uint32_t)value[2] << 16U) | ((uint32_t)value[3] << 24U);
}
static bool health_window(uint32_t period, uint32_t window) {
    return window >= period * 2U + 100000U;
}
static nx_storage_status_t stage_period(application_t* app, uint32_t period) {
    uint8_t value[4] = {(uint8_t)period, (uint8_t)(period >> 8U),
                        (uint8_t)(period >> 16U), (uint8_t)(period >> 24U)};
    return nx_settings_set(&app->settings, EXAMPLE_INDUSTRIAL_PERIOD_KEY, value,
                           sizeof(value));
}
/** \brief One executor progresses SPI, including all cooperative drain work. */
static void owner_entry(void* context) {
    application_t* app = context;
    nx_wait_port_t wake = nx_notify_port(&app->owner_event);
    for (;;) {
        uint32_t sequence = wake.arm(wake.context);
        (void)nx_bus_owner_service(&app->owner);
        /* Polling has already finished I/O. Publish its result before waiting;
         * a BUSY start has no pending result and still yields below. */
        if (app->executor.pending) {
            (void)nx_bus_owner_service(&app->owner);
        }
        if (nx_atomic_u32_load_acquire(&app->worker_exit) != 0U &&
            nx_bus_owner_idle(&app->owner)) {
            break;
        }
        (void)wake.wait(wake.context, sequence,
                        nx_deadline_after(now(NULL), PRODUCT_PUMP_US));
    }
}

static bool direction(void* context, bool transmit) {
    application_t* app = context;
    if (!transmit && app->transmitting) {
        return false;
    }
    app->direction_tx = transmit;
    return true;
}
static bool write_bytes(void* context, const uint8_t* bytes, size_t length,
                        uint64_t deadline) {
    application_t* app = context;
    if (!app->direction_tx || app->transmitting ||
        nx_uart_tx_prepare(&app->transmit, bytes, length, deadline) !=
            NX_SUCCESS) {
        return false;
    }
    app->transmitting =
        nx_uart_port_submit(nx_factory_uart_ref(NX_UART_REF_CONSOLE),
                            &app->transmit) == NX_SUCCESS;
    return app->transmitting;
}

/** \brief Sole UART owner performs bounded service; no hidden TX worker. */
static bool settle_uart(application_t* app, nx_time_us_t deadline) {
    while (nx_request_state(&app->transmit.base) != NX_REQUEST_SETTLED) {
        nx_uart_port_service(nx_factory_uart_ref(NX_UART_REF_CONSOLE));
        if (nx_request_state(&app->transmit.base) == NX_REQUEST_SETTLED) {
            break;
        }
        if (nx_deadline_expired(deadline, now(NULL))) {
            return false;
        }
    }
    app->transmitting = false;
    return true;
}
static bool drain(void* context, uint64_t deadline) {
    application_t* app = context;
    if (!app->transmitting || !settle_uart(app, deadline)) {
        return false;
    }
    nx_result_t result = NX_ERROR_STATE;
    size_t count = 0U;
    return nx_request_result(&app->transmit.base, &result, &count) ==
               NX_SUCCESS &&
           result == NX_SUCCESS;
}
static bool abort_uart(void* context, uint64_t deadline) {
    application_t* app = context;
    if (!app->transmitting) {
        return true;
    }
    return nx_uart_port_cancel(nx_factory_uart_ref(NX_UART_REF_CONSOLE),
                               &app->transmit) == NX_SUCCESS &&
           settle_uart(app, deadline);
}
static nx_modbus_register_result_t read_registers(void* context, uint16_t start,
                                                  uint16_t count,
                                                  uint16_t* output) {
    application_t* app = context;
    if ((uint32_t)start + count > 8U) {
        return NX_MODBUS_ILLEGAL_ADDRESS;
    }
    memcpy(output, &app->values[start], (size_t)count * sizeof(*output));
    return NX_MODBUS_OK;
}
static nx_modbus_register_result_t authorize(void* context, uint16_t start,
                                             uint16_t count,
                                             const uint16_t* values) {
    application_t* app = context;
    if (start != 4U || count != 1U) {
        ++app->report->rejected_writes;
        return NX_MODBUS_ILLEGAL_ADDRESS;
    }
    return period_valid((uint32_t)values[0] * 1000U,
                        app->config.operation_budget_us) &&
                   health_window((uint32_t)values[0] * 1000U,
                                 app->watchdog_min_us)
               ? NX_MODBUS_OK
               : NX_MODBUS_ILLEGAL_VALUE;
}
static nx_modbus_register_result_t write_registers(void* context,
                                                   uint16_t start,
                                                   uint16_t count,
                                                   const uint16_t* values) {
    application_t* app = context;
    nx_modbus_register_result_t allowed =
        authorize(context, start, count, values);
    if (allowed != NX_MODBUS_OK) {
        return allowed;
    }
    uint32_t period = (uint32_t)values[0] * 1000U;
    if (stage_period(app, period) != NX_STORAGE_OK) {
        return NX_MODBUS_DEVICE_FAILURE;
    }
    app->period_us = period;
    app->bridge.period_us = period;
    app->bridge.next_tick = nx_deadline_after(now(NULL), period);
    app->values[4] = values[0];
    app->values[5] = 1U;
    return NX_MODBUS_OK;
}

static bool sample_due(void* context) {
    application_t* app = context;
    return now(NULL) >= app->bridge.next_tick;
}
static bool stopping(void* context) {
    application_t* app = context;
    return nx_atomic_u32_load_acquire(&app->bridge.stopping) != 0U;
}

static uint16_t next_reference_period(application_t* app) {
    uint16_t next = (uint16_t)(app->period_us / 1000U);
    next = next <= 990U ? (uint16_t)(next + 10U) : (uint16_t)(next - 10U);
    if (!health_window((uint32_t)next * 1000U, app->watchdog_min_us) ||
        !period_valid((uint32_t)next * 1000U,
                      app->config.operation_budget_us)) {
        next = (uint16_t)(app->period_us / 1000U - 10U);
    }
    if (!health_window((uint32_t)next * 1000U, app->watchdog_min_us) ||
        !period_valid((uint32_t)next * 1000U,
                      app->config.operation_budget_us)) {
        next = (uint16_t)(app->period_us / 1000U);
    }
    return next;
}

static nx_result_t run_control(application_t* app) {
    bool measuring = false;
    bool due = true;
    uint32_t protocol_step = 0U;
    const nx_wait_source_t sources[] = {{stopping, app}, {sample_due, app}};
    nx_wait_port_t port = nx_notify_port(&app->changed);
    const example_industrial_protocol_model_t protocol = {
        &app->modbus, port, app->values, &app->transmitting,
        &app->direction_tx};
    while (true) {
        uint32_t replies_before = app->report->modbus_replies;
        if (stopping(app)) {
            ++app->report->stop_events;
            return NX_SUCCESS;
        }
        if (due && !measuring &&
            app->report->samples < app->config.sample_limit) {
            nx_result_t result = nx_bmp280_begin(
                &app->sensor,
                nx_deadline_after(now(NULL), app->config.operation_budget_us));
            if (result != NX_SUCCESS) {
                return result;
            }
            measuring = true;
            due = false;
        }
        if (measuring) {
            nx_bmp280_sample_t sample;
            nx_result_t result = nx_bmp280_poll(&app->sensor, &sample);
            if (result == NX_SUCCESS) {
                app->report->sample = sample;
                ++app->report->samples;
                app->values[0] = (uint16_t)sample.centi_celsius;
                app->values[1] = (uint16_t)(sample.pressure_pa >> 16U);
                app->values[2] = (uint16_t)sample.pressure_pa;
                app->values[3] = (uint16_t)app->report->samples;
                measuring = false;
                if (nx_watchdog_port_feed(nx_factory_watchdog_ref(
                        NX_WATCHDOG_REF_WATCHDOG0)) != NX_SUCCESS) {
                    return NX_ERROR_IO;
                }
                ++app->report->watchdog_feeds;
            } else if (result != NX_ERROR_BUSY) {
                return result;
            }
        }
        /* The finite host fixture schedules one RTU response per turn. */
        if (app->report->samples != 0U && protocol_step < 3U) {
            bool exchanged;
            if (protocol_step == 0U) {
                exchanged = example_industrial_model_exchange(
                    &app->model, &protocol, 3U, 0U, 4U, 13U);
            } else if (protocol_step == 1U) {
                exchanged = example_industrial_model_exchange(
                    &app->model, &protocol, 6U, 4U, next_reference_period(app),
                    8U);
            } else {
                exchanged = example_industrial_model_exchange(
                    &app->model, &protocol, 6U, 0U, 0U, 5U);
            }
            if (!exchanged) {
                return NX_ERROR_IO;
            }
            ++app->report->modbus_replies;
            ++protocol_step;
        }
        if (app->report->samples >= app->config.sample_limit &&
            protocol_step == 3U) {
            example_industrial_spi_stop(&app->bridge);
        }
        size_t selected = SIZE_MAX;
        uint32_t replies = app->report->modbus_replies - replies_before;
        if (replies > app->report->max_modbus_replies_per_turn) {
            app->report->max_modbus_replies_per_turn = replies;
        }
        nx_time_us_t deadline = nx_deadline_after(now(NULL), PRODUCT_PUMP_US);
        if (app->bridge.next_tick < deadline) {
            deadline = app->bridge.next_tick;
        }
        nx_result_t wait =
            nx_wait_select(&port, sources, 2U, 0U, deadline, &selected);
        if (wait == NX_SUCCESS && selected == 0U) {
            ++app->report->stop_events;
            return NX_SUCCESS;
        }
        if (wait == NX_SUCCESS && selected == 1U) {
            ++app->report->timer_events;
            nx_time_us_t current = now(NULL);
            uint32_t remaining =
                app->period_us -
                (uint32_t)((current - app->bridge.next_tick) % app->period_us);
            app->bridge.next_tick = nx_deadline_after(current, remaining);
            due = true;
        } else if (wait != NX_SUCCESS && wait != NX_ERROR_TIMEOUT) {
            return wait;
        }
    }
}

/** \brief Quiescence precedes maintenance and every OS object reclamation. */
static nx_result_t shutdown(application_t* app) {
    /* The installed Native fixture can advance model I/O time independently
     * from the host monotonic clock used by the kernel join. */
    nx_time_us_t drain_deadline;
    nx_time_us_t join_deadline;
    nx_result_t result = nx_deadline_finite_after(
        now(NULL), app->config.shutdown_budget_us, &drain_deadline);
    if (result != NX_SUCCESS) {
        return result;
    }
    result = nx_deadline_finite_after(
        nx_os_now_us(), app->config.shutdown_budget_us, &join_deadline);
    if (result != NX_SUCCESS) {
        return result;
    }
    if (app->modbus_open && !nx_modbus_rtu_stop(&app->modbus)) {
        return NX_ERROR_BUSY;
    }
    app->modbus_open = false;
    nx_bmp280_stop(&app->sensor);
    if (app->owner_open) {
        example_industrial_spi_stop(&app->bridge);
        nx_bus_owner_stop(&app->owner);
        result = example_industrial_spi_join(&app->bridge, drain_deadline);
        if (result != NX_SUCCESS) {
            return result;
        }
        if (app->worker_started) {
            nx_atomic_u32_store_release(&app->worker_exit, 1U);
            nx_wait_port_t wake = nx_notify_port(&app->owner_event);
            (void)wake.wake(wake.context);
            result = nx_task_join_until(&app->worker, join_deadline);
            if (result != NX_SUCCESS) {
                return result;
            }
            app->worker_started = false;
        }
        if (!nx_bus_owner_idle(&app->owner)) {
            return NX_ERROR_STATE;
        }
    }
    app->report->joined = true;
    return NX_SUCCESS;
}

nx_result_t example_industrial_run(nx_storage_t* storage,
                                   const example_industrial_config_t* config,
                                   example_industrial_report_t* report) {
    if (report == NULL) {
        return NX_ERROR_INVALID;
    }
    *report = (example_industrial_report_t){0};
    if (storage == NULL || !storage->opened || config == NULL ||
        config->operation_budget_us == 0U ||
        config->operation_budget_us > 100000U ||
        !period_valid(config->period_us, config->operation_budget_us) ||
        config->sample_limit == 0U || config->sample_limit > 100U ||
        config->shutdown_budget_us == 0U ||
        config->shutdown_budget_us > 1000000U ||
        config->watchdog_timeout_us < config->period_us * 2U + 100000U ||
        config->watchdog_timeout_us > 32000000U) {
        return NX_ERROR_INVALID;
    }
    if (s_retained) {
        report->storage_retained = true;
        return NX_ERROR_STATE;
    }
    application_t* app = &s_application;
    *app = (application_t){.config = *config, .report = report};
    report->storage_status =
        nx_settings_open(&app->settings, storage, EXAMPLE_INDUSTRIAL_SCHEMA,
                         app->settings_image, sizeof(app->settings_image), 1U);
    if (report->storage_status != NX_STORAGE_OK) {
        return report->storage_status == NX_STORAGE_UNSUPPORTED
                   ? NX_ERROR_UNSUPPORTED
                   : NX_ERROR_IO;
    }
    uint8_t value[4];
    size_t length = sizeof(value);
    report->storage_status = nx_settings_get(
        &app->settings, EXAMPLE_INDUSTRIAL_PERIOD_KEY, value, &length);
    app->period_us = config->period_us;
    if (report->storage_status == NX_STORAGE_OK) {
        if (length != sizeof(value) ||
            !period_valid(decode(value), config->operation_budget_us)) {
            report->storage_status = NX_STORAGE_CORRUPT;
            nx_settings_close(&app->settings);
            return NX_ERROR_IO;
        }
        app->period_us = decode(value);
        report->restored_settings = true;
    } else if (report->storage_status != NX_STORAGE_NOT_FOUND) {
        nx_settings_close(&app->settings);
        return NX_ERROR_IO;
    }
    report->initial_period_us = app->period_us;
    if (!health_window(app->period_us, config->watchdog_timeout_us)) {
        nx_settings_close(&app->settings);
        return NX_ERROR_INVALID;
    }
    report->storage_status = stage_period(app, app->period_us);
    nx_result_t result =
        report->storage_status == NX_STORAGE_OK ? NX_SUCCESS : NX_ERROR_IO;
    if (result == NX_SUCCESS) {
        nx_platform_start_result_t started = nx_platform_start();
        report->startup_primary = started.primary;
        report->startup_cleanup = started.cleanup;
        report->startup_remaining_effects = started.remaining_effects;
        app->platform_open = started.primary == NX_SUCCESS &&
                             started.cleanup == NX_SUCCESS &&
                             started.remaining_effects == 0U;
        result = app->platform_open              ? NX_SUCCESS
                 : started.primary != NX_SUCCESS ? started.primary
                 : started.cleanup != NX_SUCCESS ? started.cleanup
                                                 : NX_ERROR_STATE;
        if (started.cleanup != NX_SUCCESS || started.remaining_effects != 0U) {
            nx_settings_close(&app->settings);
            s_retained = true;
            report->storage_retained = true;
            return result;
        }
    }
    if (result == NX_SUCCESS) {
        result = example_industrial_model_clock();
    }
    if (result == NX_SUCCESS) {
        result = nx_notify_init(&app->changed);
        app->changed_open = result == NX_SUCCESS;
    }
    if (result == NX_SUCCESS) {
        result = nx_notify_init(&app->owner_event);
        app->owner_event_open = result == NX_SUCCESS;
    }
    if (result == NX_SUCCESS) {
        result = example_industrial_model_sensor(
            &app->model, nx_factory_spi_device_ref(NX_SPI_DEVICE_REF_SENSOR0));
    }
    if (result == NX_SUCCESS) {
        nx_wait_port_t wake = nx_notify_port(&app->owner_event);
        nx_owner_executor_port_t executor = nx_spi_owner_executor_port(
            &app->executor, nx_factory_spi_ref(NX_SPI_REF_SPI0));
        result = nx_bus_owner_init(&app->owner, &app->slot, 1U,
                                   (nx_owner_guard_port_t){NULL, enter, leave},
                                   executor, (nx_clock_t){now, NULL}, &wake);
        app->owner_open = result == NX_SUCCESS;
    }
    if (result == NX_SUCCESS) {
        result = example_industrial_spi_open(
            &app->bridge, &app->owner,
            nx_factory_spi_device_ref(NX_SPI_DEVICE_REF_SENSOR0),
            nx_notify_port(&app->changed), (nx_clock_t){now, NULL},
            app->period_us);
    }
    if (result == NX_SUCCESS) {
        result = nx_task_start(&app->worker, "application", s_owner_stack,
                               sizeof(s_owner_stack), 0U, owner_entry, app);
        app->worker_started = result == NX_SUCCESS;
    }
    if (result == NX_SUCCESS) {
        result = nx_bmp280_init(
            &app->sensor, example_industrial_spi_transport(&app->bridge),
            nx_deadline_after(now(NULL), config->operation_budget_us));
    }
    if (result == NX_SUCCESS) {
        nx_request_initialize(&app->transmit.base);
        result = example_industrial_model_uart(
            &app->model, nx_factory_uart_ref(NX_UART_REF_CONSOLE));
    }
    if (result == NX_SUCCESS) {
        app->values[4] = (uint16_t)(app->period_us / 1000U);
        nx_modbus_config_t modbus = {
            .address = 17U,
            .baudrate = 115200U,
            .bits_per_character = 10U,
            .tx_timeout_us = PRODUCT_TX_US,
            .abort_timeout_us = PRODUCT_ABORT_US,
            .registers = {app, read_registers, authorize, write_registers},
            .port = {app, now, direction, write_bytes, drain, abort_uart}};
        app->modbus_open = nx_modbus_rtu_init(&app->modbus, &modbus);
        result = app->modbus_open ? NX_SUCCESS : NX_ERROR_IO;
    }
    if (result == NX_SUCCESS) {
        const nx_watchdog_port_t* watchdog =
            nx_factory_watchdog_ref(NX_WATCHDOG_REF_WATCHDOG0);
        example_industrial_model_watchdog_boot(watchdog);
        nx_watchdog_state_t state;
        result = nx_watchdog_port_enable(watchdog, config->watchdog_timeout_us,
                                         false, &state);
        if (result == NX_SUCCESS) {
            app->watchdog_min_us = state.minimum_timeout_us;
            if (!health_window(app->period_us, app->watchdog_min_us)) {
                result = NX_ERROR_INVALID;
            }
        }
    }
    if (result == NX_SUCCESS) {
        result = run_control(app);
    }
    nx_result_t cleanup = shutdown(app);
    report->spi_completions = app->bridge.completed;
    report->timer_events += app->bridge.timer_events;
    report->stop_events += app->bridge.stop_events;
    if (cleanup != NX_SUCCESS) {
        nx_settings_close(&app->settings);
        s_retained = true;
        report->storage_retained = true;
        return result == NX_SUCCESS ? cleanup : result;
    }
    if (result == NX_SUCCESS) {
        report->storage_status = nx_settings_commit(&app->settings);
        if (report->storage_status == NX_STORAGE_OK) {
            report->durable_period_us = app->period_us;
            report->committed_in_maintenance = true;
        } else {
            result = NX_ERROR_IO;
        }
    }
    nx_settings_close(&app->settings);
    if (app->owner_event_open &&
        nx_notify_destroy(&app->owner_event) != NX_SUCCESS) {
        cleanup = NX_ERROR_BUSY;
    }
    if (app->changed_open && nx_notify_destroy(&app->changed) != NX_SUCCESS) {
        cleanup = NX_ERROR_BUSY;
    }
    if (app->platform_open && cleanup == NX_SUCCESS) {
        cleanup = nx_platform_stop();
        report->platform_stopped = cleanup == NX_SUCCESS;
    }
    nx_watchdog_state_t state;
    if (app->platform_open &&
        nx_watchdog_port_state(
            nx_factory_watchdog_ref(NX_WATCHDOG_REF_WATCHDOG0), &state) ==
            NX_SUCCESS) {
        report->watchdog_retained = state.enabled && state.irreversible;
        report->watchdog_expired = example_industrial_model_watchdog_expired(
            nx_factory_watchdog_ref(NX_WATCHDOG_REF_WATCHDOG0));
        if (report->watchdog_expired && result == NX_SUCCESS) {
            result = NX_ERROR_IO;
        }
    }
    s_retained = cleanup != NX_SUCCESS;
    report->storage_retained = s_retained;
    return result == NX_SUCCESS ? cleanup : result;
}
