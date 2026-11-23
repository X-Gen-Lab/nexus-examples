/**
 * \file            terminal_application.c
 * \brief           Small tiles, copied UDP status and explicit parameter saves
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-22
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "terminal_application.h"
#include <string.h>

/** \brief Product format avoids host ABI, padding and endian dependence. */
static void put16(uint8_t* bytes, uint16_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8U);
}
/** \brief Fixed-width little-endian field write. */
static void put32(uint8_t* bytes, uint32_t value) {
    for (size_t i = 0U; i < 4U; ++i)
        bytes[i] = (uint8_t)(value >> (i * 8U));
}
/** \brief Fixed-width little-endian field read. */
static uint16_t get16(const uint8_t* bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}
/** \brief Validate the complete selected product parameter domain. */
static bool valid(const example_terminal_parameters_t* parameters) {
    return parameters->refresh_ms >= 10U && parameters->refresh_ms <= 5000U;
}
/** \brief Encode only values, never the in-memory C structure. */
static void encode(uint8_t bytes[12],
                   const example_terminal_parameters_t* parameters,
                   uint32_t version) {
    bytes[0] = 1U;
    bytes[1] = 0U;
    put16(bytes + 2U, parameters->refresh_ms);
    put16(bytes + 4U, parameters->offline_color);
    put16(bytes + 6U, parameters->online_color);
    put32(bytes + 8U, version);
}
/** \brief Preserve persistence detail instead of inventing successful defaults.
 */
static nx_result_t storage_result(nx_storage_status_t status) {
    if (status == NX_STORAGE_OK)
        return NX_SUCCESS;
    if (status == NX_STORAGE_NO_SPACE)
        return NX_ERROR_EXHAUSTED;
    if (status == NX_STORAGE_IO)
        return NX_ERROR_IO;
    return NX_ERROR_STATE;
}
/** \brief Device writes use one admitted clock and never a renewed deadline. */
static nx_result_t time_ready(example_terminal_t* terminal,
                              nx_time_us_t deadline) {
    if (deadline == NX_DEADLINE_NEVER)
        return NX_ERROR_INVALID;
    nx_time_us_t now =
        terminal->config.route.clock.read(terminal->config.route.clock.context);
    if (now == NX_DEADLINE_NEVER || now < terminal->last_time) {
        terminal->ui_error = NX_ERROR_STATE;
        return NX_ERROR_STATE;
    }
    terminal->last_time = now;
    return now >= deadline ? NX_ERROR_TIMEOUT : NX_SUCCESS;
}
/** \brief DNS and DHCP may retire only after the higher UDP owner joins. */
static nx_result_t network_stop(example_terminal_t* terminal) {
    if (terminal->udp_owned) {
        nx_result_t result = nx_lwip_udp_close(terminal->config.udp);
        if (result != NX_SUCCESS)
            return result;
        terminal->udp_owned = false;
    }
    if (terminal->route_owned) {
        nx_result_t result = example_network_route_stop(&terminal->route);
        if (result != NX_SUCCESS)
            return result;
        terminal->route_owned = false;
    }
    return NX_SUCCESS;
}
/** \brief Terminal route failure is visible, never silently reconnected. */
static void network_failed(example_terminal_t* terminal, nx_result_t result) {
    if (terminal->network_error == NX_SUCCESS)
        terminal->network_error = result;
    if (terminal->network_ready)
        terminal->display_dirty = true;
    terminal->network_ready = false;
}

nx_result_t example_terminal_start(example_terminal_t* terminal,
                                   const example_terminal_config_t* config) {
    if (terminal == NULL || config == NULL || config->display == NULL ||
        config->touch == NULL || config->tile == NULL ||
        config->settings == NULL || !config->settings->opened ||
        config->udp == NULL || config->receive == NULL ||
        config->local_port == 0U || config->remote_port == 0U ||
        config->bar_height == 0U ||
        config->bar_height > config->calibration.height ||
        config->tile_capacity < config->calibration.width ||
        !valid(&config->defaults))
        return NX_ERROR_INVALID;
    if (terminal->live)
        return NX_ERROR_STATE;
    nx_xpt2046_sample_t raw = {0};
    example_touch_point_t point;
    nx_result_t result = example_touch_map(&config->calibration, &raw, &point);
    if (result != NX_SUCCESS && result != NX_ERROR_EMPTY)
        return result;
    uint8_t bytes[12];
    size_t size = sizeof(bytes);
    nx_storage_status_t status =
        nx_settings_get(config->settings, config->settings_key, bytes, &size);
    example_terminal_parameters_t parameters = config->defaults;
    uint32_t version = 1U;
    uint32_t persisted = 0U;
    if (status == NX_STORAGE_OK) {
        if (size != sizeof(bytes) || bytes[0] != 1U || bytes[1] != 0U)
            return NX_ERROR_STATE;
        parameters = (example_terminal_parameters_t){
            get16(bytes + 2U), get16(bytes + 4U), get16(bytes + 6U)};
        version = 0U;
        for (size_t i = 0U; i < 4U; ++i)
            version |= (uint32_t)bytes[8U + i] << (i * 8U);
        if (!valid(&parameters) || version == 0U)
            return NX_ERROR_STATE;
        persisted = version;
    } else if (status != NX_STORAGE_NOT_FOUND) {
        return storage_result(status);
    }
    example_network_route_t route = {0};
    result = example_network_route_start(&route, &config->route);
    if (result != NX_SUCCESS)
        return result;
    *terminal = (example_terminal_t){.config = *config,
                                     .route = route,
                                     .parameters = parameters,
                                     .last_time = route.last_time,
                                     .version = version,
                                     .persisted_version = persisted,
                                     .live = true,
                                     .route_owned = true,
                                     .display_owned = true,
                                     .touch_owned = true,
                                     .display_dirty = true,
                                     .report_pending = true};
    return NX_SUCCESS;
}

/** \brief One copied observation; stack data is not retained by UDP send. */
static nx_result_t report(example_terminal_t* terminal, nx_time_us_t deadline) {
    if (!terminal->network_ready ||
        (!terminal->report_pending &&
         terminal->last_time < terminal->next_report))
        return NX_SUCCESS;
    if (terminal->sequence == UINT32_MAX)
        return NX_ERROR_EXHAUSTED;
    /* UI work cannot renew the independently fixed route operation. */
    if (deadline > terminal->config.route.deadline)
        deadline = terminal->config.route.deadline;
    nx_result_t result = time_ready(terminal, deadline);
    if (result != NX_SUCCESS)
        return result;
    nx_ipv4_peer_t peer = {.port = terminal->config.remote_port};
    result = example_network_route_peer(&terminal->route, peer.address);
    if (result != NX_SUCCESS)
        return result;
    uint8_t bytes[16] = {'N', 'T', 1U, terminal->touched ? 1U : 0U};
    put32(bytes + 4U, terminal->version);
    put16(bytes + 8U, terminal->point.x);
    put16(bytes + 10U, terminal->point.y);
    put32(bytes + 12U, terminal->sequence + 1U);
    result = nx_lwip_udp_send(terminal->config.udp, &peer, bytes, sizeof(bytes),
                              deadline);
    if (result == NX_SUCCESS) {
        ++terminal->sequence;
        terminal->report_pending = false;
        result = nx_deadline_finite_after(
            terminal->last_time,
            (nx_time_us_t)terminal->parameters.refresh_ms * 1000U,
            &terminal->next_report);
    }
    return result;
}

nx_result_t example_terminal_step(example_terminal_t* terminal, bool link_up,
                                  nx_time_us_t deadline) {
    if (terminal == NULL)
        return NX_ERROR_INVALID;
    if (!terminal->live || terminal->closing)
        return NX_ERROR_STATE;
    if (terminal->ui_error != NX_SUCCESS)
        return terminal->ui_error;
    nx_result_t result = time_ready(terminal, deadline);
    if (result != NX_SUCCESS)
        return result;
    if (terminal->route_owned && terminal->network_error == NX_SUCCESS) {
        result = example_network_route_step(&terminal->route, link_up);
        if (result == NX_SUCCESS) {
            if (!terminal->udp_owned) {
                result = nx_lwip_udp_open(
                    terminal->config.udp, terminal->config.route.network,
                    terminal->config.local_port, terminal->config.receive, 16U);
                if (result == NX_SUCCESS)
                    terminal->udp_owned = true;
            }
            if (result == NX_SUCCESS && !terminal->network_ready) {
                terminal->network_ready = true;
                terminal->display_dirty = true;
                terminal->report_pending = true;
            }
        }
        if (result != NX_SUCCESS && result != NX_ERROR_BUSY)
            network_failed(terminal, result);
    }
    if (terminal->network_error != NX_SUCCESS && terminal->route_owned)
        (void)network_stop(terminal);
    result = time_ready(terminal, deadline);
    if (result != NX_SUCCESS)
        return result;
    example_touch_point_t point;
    result = example_ui_touch(terminal->config.touch,
                              &terminal->config.calibration, &point, deadline);
    if (result == NX_SUCCESS) {
        if (!terminal->touched || point.x != terminal->point.x ||
            point.y != terminal->point.y)
            terminal->report_pending = true;
        terminal->point = point;
        terminal->touched = true;
    } else if (result == NX_ERROR_EMPTY) {
        if (terminal->touched)
            terminal->report_pending = true;
        terminal->touched = false;
    } else {
        terminal->ui_error = result;
        return result;
    }
    if (!terminal->fill.active && terminal->display_dirty) {
        const example_touch_calibration_t* c = &terminal->config.calibration;
        result = example_ui_fill_begin(
            &terminal->fill, c->width, c->height, 0U, 0U, c->width,
            terminal->config.bar_height,
            terminal->network_ready ? terminal->parameters.online_color
                                    : terminal->parameters.offline_color);
        if (result != NX_SUCCESS) {
            terminal->ui_error = result;
            return result;
        }
        terminal->display_dirty = false;
    }
    if (terminal->fill.active) {
        result = example_ui_fill_step(&terminal->fill, terminal->config.display,
                                      terminal->config.tile,
                                      terminal->config.tile_capacity, deadline);
        if (result != NX_SUCCESS && result != NX_ERROR_BUSY) {
            terminal->ui_error = result;
            return result;
        }
    }
    result = time_ready(terminal, deadline);
    if (result != NX_SUCCESS)
        return result;
    result = report(terminal, deadline);
    if (result != NX_SUCCESS && result != NX_ERROR_BUSY)
        network_failed(terminal, result);
    return NX_SUCCESS;
}

nx_result_t
example_terminal_apply(example_terminal_t* terminal,
                       const example_terminal_parameters_t* parameters) {
    if (terminal == NULL || parameters == NULL || !valid(parameters))
        return NX_ERROR_INVALID;
    if (!terminal->live || terminal->closing)
        return NX_ERROR_STATE;
    if (terminal->version == UINT32_MAX)
        return NX_ERROR_EXHAUSTED;
    uint8_t bytes[12];
    encode(bytes, parameters, terminal->version + 1U);
    terminal->storage_error =
        nx_settings_set(terminal->config.settings,
                        terminal->config.settings_key, bytes, sizeof(bytes));
    if (terminal->storage_error != NX_STORAGE_OK)
        return storage_result(terminal->storage_error);
    terminal->parameters = *parameters;
    ++terminal->version;
    terminal->display_dirty = true;
    terminal->report_pending = true;
    return NX_SUCCESS;
}

nx_result_t example_terminal_save(example_terminal_t* terminal,
                                  bool maintenance_allowed) {
    if (terminal == NULL)
        return NX_ERROR_INVALID;
    if (!terminal->live || terminal->closing)
        return NX_ERROR_STATE;
    if (!maintenance_allowed)
        return NX_ERROR_PERMISSION;
    uint8_t bytes[12];
    encode(bytes, &terminal->parameters, terminal->version);
    terminal->storage_error =
        nx_settings_set(terminal->config.settings,
                        terminal->config.settings_key, bytes, sizeof(bytes));
    if (terminal->storage_error == NX_STORAGE_OK)
        terminal->storage_error = nx_settings_commit(terminal->config.settings);
    if (terminal->storage_error != NX_STORAGE_OK)
        return storage_result(terminal->storage_error);
    terminal->persisted_version = terminal->version;
    return NX_SUCCESS;
}

nx_result_t example_terminal_stop(example_terminal_t* terminal,
                                  nx_time_us_t deadline) {
    if (terminal == NULL)
        return NX_ERROR_INVALID;
    if (!terminal->live)
        return NX_SUCCESS;
    if (deadline == NX_DEADLINE_NEVER)
        return NX_ERROR_INVALID;
    terminal->closing = true;
    terminal->network_ready = false;
    nx_result_t result = network_stop(terminal);
    if (result != NX_SUCCESS)
        return result;
    if (terminal->touch_owned) {
        result = nx_xpt2046_stop(terminal->config.touch, deadline);
        if (result != NX_SUCCESS)
            return result;
        terminal->touch_owned = false;
    }
    if (terminal->display_owned) {
        result = nx_ili9341_stop(terminal->config.display, deadline);
        if (result != NX_SUCCESS)
            return result;
        terminal->display_owned = false;
    }
    terminal->live = false;
    return NX_SUCCESS;
}
