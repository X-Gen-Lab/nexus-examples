/**
 * \file            route_reference.c
 * \brief           Explicit managed address, DNS retirement and UTC policy
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-22
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "route_reference.h"
#include <string.h>

/** \brief Validate bounded known spans before changing caller metadata. */
static bool overlap(const void* a, size_t an, const void* b, size_t bn) {
    uintptr_t x = (uintptr_t)a;
    uintptr_t y = (uintptr_t)b;
    return an > UINTPTR_MAX - x || bn > UINTPTR_MAX - y ||
           (an != 0U && bn != 0U && x < y + bn && y < x + an);
}
/** \brief Close route admission while retaining all accepted lower owners. */
static nx_result_t failed(example_network_route_t* route, nx_result_t result) {
    if (route->failure == NX_SUCCESS) {
        route->failure = result;
    }
    route->state = EXAMPLE_ROUTE_DRAINING;
    return result;
}

nx_result_t
example_network_route_start(example_network_route_t* route,
                            const example_network_route_config_t* config) {
    if (route == NULL || config == NULL || config->network == NULL ||
        config->dhcp == NULL || config->dns == NULL ||
        config->clock.read == NULL || config->hostname == NULL ||
        config->hostname[0] == '\0' || config->receive_budget == 0U ||
        config->progress_turns == 0U || config->deadline == NX_DEADLINE_NEVER ||
        overlap(route, sizeof(*route), config, sizeof(*config))) {
        return NX_ERROR_INVALID;
    }
    if ((route->state != EXAMPLE_ROUTE_COLD &&
         route->state != EXAMPLE_ROUTE_STOPPED) ||
        route->dhcp_owned || route->dns_owned) {
        return NX_ERROR_STATE;
    }
    nx_time_us_t now = config->clock.read(config->clock.context);
    if (now == NX_DEADLINE_NEVER || now >= config->deadline) {
        return now == NX_DEADLINE_NEVER ? NX_ERROR_STATE : NX_ERROR_TIMEOUT;
    }
    *route =
        (example_network_route_t){.config = *config,
                                  .last_time = now,
                                  .remaining_turns = config->progress_turns,
                                  .state = EXAMPLE_ROUTE_ADDRESS};
    return NX_SUCCESS;
}

nx_result_t example_network_route_step(example_network_route_t* route,
                                       bool link_up) {
    if (route == NULL) {
        return NX_ERROR_INVALID;
    }
    if (route->state != EXAMPLE_ROUTE_ADDRESS &&
        route->state != EXAMPLE_ROUTE_RESOLVING &&
        route->state != EXAMPLE_ROUTE_READY) {
        return NX_ERROR_STATE;
    }
    nx_time_us_t now = route->config.clock.read(route->config.clock.context);
    if (now == NX_DEADLINE_NEVER || now < route->last_time) {
        return failed(route, NX_ERROR_STATE);
    }
    route->last_time = now;
    if (now >= route->config.deadline) {
        return failed(route, NX_ERROR_TIMEOUT);
    }
    if (route->remaining_turns == 0U) {
        return failed(route, NX_ERROR_EXHAUSTED);
    }
    --route->remaining_turns;
    nx_result_t result = nx_lwip_adapter_service(route->config.network,
                                                 route->config.receive_budget);
    if (result != NX_SUCCESS && result != NX_ERROR_BUSY) {
        return failed(route, result);
    }
    if (!link_up) {
        return route->dhcp_owned ? failed(route, NX_ERROR_IO) : NX_ERROR_BUSY;
    }
    if (!route->dhcp_owned) {
        result = nx_lwip_dhcp_start(route->config.dhcp, route->config.network);
        if (result != NX_SUCCESS) {
            return failed(route, result);
        }
        route->dhcp_owned = true;
    }
    uint8_t address[4];
    result = nx_lwip_dhcp_address(route->config.dhcp, address);
    if (route->state != EXAMPLE_ROUTE_ADDRESS) {
        if (result != NX_SUCCESS || memcmp(address, route->local, 4U) != 0) {
            return failed(route, result == NX_ERROR_BUSY ? NX_ERROR_STATE
                                 : result == NX_SUCCESS  ? NX_ERROR_STATE
                                                         : result);
        }
    } else if (result == NX_ERROR_BUSY) {
        return NX_ERROR_BUSY;
    } else if (result != NX_SUCCESS) {
        return failed(route, result);
    } else {
        memcpy(route->local, address, 4U);
        result =
            nx_lwip_dns_start(route->config.dns, route->config.network,
                              route->config.hostname, route->config.deadline);
        if (result != NX_SUCCESS) {
            return failed(route, result);
        }
        route->dns_owned = true;
        route->state = EXAMPLE_ROUTE_RESOLVING;
    }
    if (route->state == EXAMPLE_ROUTE_READY) {
        return NX_SUCCESS;
    }
    result = nx_lwip_dns_poll(route->config.dns, address);
    if (result == NX_ERROR_BUSY) {
        return result;
    }
    if (result != NX_SUCCESS && result != NX_ERROR_IO &&
        result != NX_ERROR_TIMEOUT && result != NX_ERROR_CANCELLED) {
        return failed(route, result);
    }
    route->dns_owned = false;
    if (result != NX_SUCCESS) {
        return failed(route, result);
    }
    memcpy(route->peer, address, 4U);
    route->state = EXAMPLE_ROUTE_READY;
    return NX_SUCCESS;
}

nx_result_t example_network_route_peer(const example_network_route_t* route,
                                       uint8_t address[4]) {
    if (route == NULL || address == NULL ||
        overlap(route, sizeof(*route), address, 4U)) {
        return NX_ERROR_INVALID;
    }
    if (route->state != EXAMPLE_ROUTE_READY) {
        return NX_ERROR_STATE;
    }
    memcpy(address, route->peer, 4U);
    return NX_SUCCESS;
}

nx_result_t example_network_route_stop(example_network_route_t* route) {
    if (route == NULL) {
        return NX_ERROR_INVALID;
    }
    if (route->state == EXAMPLE_ROUTE_STOPPED) {
        return NX_SUCCESS;
    }
    if (route->state == EXAMPLE_ROUTE_COLD) {
        return NX_ERROR_STATE;
    }
    route->state = EXAMPLE_ROUTE_DRAINING;
    if (route->dns_owned) {
        nx_result_t result = nx_lwip_adapter_service(
            route->config.network, route->config.receive_budget);
        if (result != NX_SUCCESS && result != NX_ERROR_BUSY &&
            route->failure == NX_SUCCESS) {
            route->failure = result;
        }
        result = nx_lwip_dns_cancel(route->config.dns);
        if (result != NX_ERROR_CANCELLED) {
            return result == NX_SUCCESS ? NX_ERROR_STATE : result;
        }
        route->dns_owned = false;
    }
    if (route->dhcp_owned) {
        nx_result_t result = nx_lwip_dhcp_stop(route->config.dhcp);
        if (result != NX_SUCCESS) {
            return result;
        }
        route->dhcp_owned = false;
    }
    route->state = EXAMPLE_ROUTE_STOPPED;
    return NX_SUCCESS;
}

nx_result_t example_trusted_utc_read(void* context, int64_t* seconds) {
    const example_trusted_utc_t* bridge = context;
    if (bridge == NULL || seconds == NULL || bridge->wall == NULL ||
        bridge->clock.read == NULL ||
        overlap(bridge, sizeof(*bridge), seconds, sizeof(*seconds)) ||
        overlap(bridge->wall, sizeof(*bridge->wall), seconds,
                sizeof(*seconds))) {
        return NX_ERROR_INVALID;
    }
    nx_utc_sample_t sample;
    nx_time_us_t now = bridge->clock.read(bridge->clock.context);
    nx_result_t result = nx_wall_clock_read(bridge->wall, now, &sample);
    if (result != NX_SUCCESS) {
        return result;
    }
    if (sample.quality != NX_UTC_TRUSTED ||
        sample.uncertainty_us > bridge->max_uncertainty_us) {
        return NX_ERROR_PERMISSION;
    }
    if (sample.unix_us < 0) {
        return NX_ERROR_UNSUPPORTED;
    }
    *seconds = sample.unix_us / INT64_C(1000000);
    return NX_SUCCESS;
}
