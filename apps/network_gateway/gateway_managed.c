/**
 * \file            gateway_managed.c
 * \brief           Bounded managed route and durable secure gateway lifecycle
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-25
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "gateway_managed.h"

static bool disjoint(const example_gateway_managed_t* managed,
                     const void* value, size_t bytes) {
    uintptr_t a = (uintptr_t)managed;
    uintptr_t b = (uintptr_t)value;
    return a <= b ? b - a >= sizeof(*managed) : a - b >= bytes;
}

static nx_result_t route_step(example_gateway_managed_t* managed,
                              nx_time_us_t deadline, bool session) {
    if (managed->stopping) {
        return NX_ERROR_CANCELLED;
    }
    if (managed->route_failure != NX_SUCCESS) {
        return managed->route_failure;
    }
    if (deadline > managed->route.config.deadline) {
        deadline = managed->route.config.deadline;
    }
    nx_time_us_t now =
        managed->route.config.clock.read(managed->route.config.clock.context);
    if (now == NX_DEADLINE_NEVER || now < managed->route.last_time) {
        managed->route_failure = NX_ERROR_STATE;
        return managed->route_failure;
    }
    if (now >= deadline) {
        return NX_ERROR_TIMEOUT;
    }
    if (managed->remaining_phy_turns == 0U) {
        managed->route_failure = NX_ERROR_EXHAUSTED;
        return managed->route_failure;
    }
    --managed->remaining_phy_turns;
    bool up = false;
    nx_result_t result = managed->phy_step(managed->phy_context, &up, deadline);
    if (result == NX_SUCCESS) {
        result = example_network_route_step(&managed->route, up);
    }
    if (session && result == NX_ERROR_BUSY) {
        result = NX_ERROR_IO;
    }
    if (result != NX_SUCCESS && result != NX_ERROR_BUSY) {
        managed->route_failure = result;
    }
    return result;
}

static nx_result_t check_session_route(void* context, nx_time_us_t deadline) {
    return route_step(context, deadline, true);
}

nx_result_t
example_gateway_managed_start(example_gateway_managed_t* managed,
                              const example_network_route_config_t* route,
                              example_gateway_phy_step_t phy_step,
                              void* phy_context) {
    if (managed == NULL || route == NULL || phy_step == NULL ||
        route->hostname == NULL || !disjoint(managed, route, sizeof(*route))) {
        return NX_ERROR_INVALID;
    }
    if (managed->delivery.active) {
        return NX_ERROR_STATE;
    }
    size_t hostname_bytes = 0U;
    while (hostname_bytes <= 253U && route->hostname[hostname_bytes] != '\0') {
        ++hostname_bytes;
    }
    if (hostname_bytes == 0U || hostname_bytes > 253U ||
        !disjoint(managed, route->hostname, hostname_bytes + 1U)) {
        return NX_ERROR_INVALID;
    }
    nx_result_t result = example_network_route_start(&managed->route, route);
    if (result == NX_SUCCESS) {
        managed->phy_step = phy_step;
        managed->phy_context = phy_context;
        managed->hostname_bytes = hostname_bytes;
        managed->remaining_phy_turns = route->progress_turns;
        managed->route_failure = NX_SUCCESS;
        managed->stopping = false;
    }
    return result;
}

nx_result_t
example_gateway_managed_run(example_gateway_managed_t* managed,
                            example_outbox_t* outbox,
                            const nx_flashdb_operation_t* operation,
                            const example_gateway_config_t* gateway,
                            example_gateway_delivery_report_t* report) {
    if (managed == NULL || outbox == NULL || operation == NULL ||
        gateway == NULL || report == NULL ||
        !disjoint(managed, outbox, sizeof(*outbox)) ||
        !disjoint(managed, operation, sizeof(*operation)) ||
        !disjoint(managed, gateway, sizeof(*gateway)) ||
        !disjoint(managed, report, sizeof(*report))) {
        return NX_ERROR_INVALID;
    }
    if (managed->phy_step == NULL || managed->stopping ||
        managed->delivery.active) {
        return NX_ERROR_STATE;
    }
    const example_network_route_config_t* route = &managed->route.config;
    if (route->network != gateway->tcp_config.network ||
        route->clock.read != gateway->clock.read ||
        route->clock.context != gateway->clock.context ||
        gateway->policy.overall_deadline > route->deadline ||
        gateway->route_check != NULL) {
        return NX_ERROR_INVALID;
    }
    const nx_time_us_t deadline = gateway->policy.overall_deadline;
    example_gateway_config_t selected = *gateway;
    selected.tls_config.hostname = route->hostname;
    selected.tls_config.hostname_length = managed->hostname_bytes;
    selected.route_check = check_session_route;
    selected.route_context = managed;
    nx_result_t result = route_step(managed, deadline, false);
    if (result == NX_SUCCESS) {
        result = example_network_route_peer(&managed->route,
                                            selected.tcp_config.address);
    }
    if (result != NX_SUCCESS) {
        return result;
    }
    return example_gateway_delivery_run(&managed->delivery, outbox, operation,
                                        &selected, report);
}

nx_result_t example_gateway_managed_stop(
    example_gateway_managed_t* managed, const nx_flashdb_operation_t* operation,
    nx_time_us_t deadline, example_gateway_delivery_report_t* report) {
    if (managed == NULL || report == NULL || deadline == NX_DEADLINE_NEVER ||
        !disjoint(managed, report, sizeof(*report)) ||
        (operation != NULL &&
         !disjoint(managed, operation, sizeof(*operation)))) {
        return NX_ERROR_INVALID;
    }
    if (managed->phy_step == NULL) {
        return NX_ERROR_STATE;
    }
    managed->stopping = true;
    nx_result_t result = NX_SUCCESS;
    if (managed->delivery.active) {
        result = example_gateway_delivery_cleanup(&managed->delivery, operation,
                                                  deadline, report);
        if (managed->delivery.active) {
            return result;
        }
    }
    nx_result_t route_result = example_network_route_stop(&managed->route);
    return result != NX_SUCCESS ? result : route_result;
}
