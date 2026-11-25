/**
 * \file            main.c
 * \brief           Cold link consumer for the managed gateway's exact storage
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-25
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "gateway_managed.h"
#include "nexus/mqtt/core_mqtt_storage.h"
#include "nexus/network/lwip_services_storage.h"
#include "nexus/network/lwip_storage.h"
#include "nexus/network/lwip_tcp_storage.h"
#include "nexus/tls/storage.h"
#include "nexus_bindings.h"
#include "nexus_factory.h"
#include "public_ca.h"
#include <stdalign.h>

static nx_lwip_adapter_t network;
static nx_lwip_tcp_t tcp;
static nx_lwip_dhcp_t dhcp;
static nx_lwip_dns_t dns;
static nx_tls_runtime_t runtime;
static nx_tls_session_t tls;
static nx_mqtt_t mqtt;
static nx_flashdb_t database;
static example_outbox_t outbox;
static example_gateway_managed_t application;
static example_gateway_delivery_report_t report;
static alignas(max_align_t) uint8_t arena[NX_TLS_ARENA_BYTES];
static uint8_t tls_stage[NX_TLS_TX_STAGE_BYTES];
static uint8_t mqtt_network[1024U];
static MQTTPubAckInfo_t mqtt_outgoing[2U];
static MQTTPubAckInfo_t mqtt_incoming[2U];
static nx_mqtt_control_t mqtt_controls[2U];

static nx_time_us_t now(void* context) {
    (void)context;
    return nx_time_now_us();
}

static nx_result_t unavailable(void* context, nx_time_us_t deadline) {
    (void)context;
    (void)deadline;
    return NX_ERROR_PERMISSION;
}

static nx_result_t no_link(void* context, bool* up, nx_time_us_t deadline) {
    *up = false;
    return unavailable(context, deadline);
}

static nx_result_t untrusted_utc(void* context, int64_t* seconds) {
    (void)context;
    (void)seconds;
    return NX_ERROR_PERMISSION;
}

uint32_t nx_lwip_port_now_ms(void) {
    return (uint32_t)(nx_time_now_us() / 1000U);
}

void nx_lwip_port_assert(const char* message) {
    (void)message;
    for (;;) {
    }
}

uint32_t nx_lwip_port_random_u32(void) {
    nx_lwip_port_assert("Cold network consumer has no entropy session");
    return 0U;
}

nx_time_us_t nx_mqtt_port_now_us(void) {
    return nx_time_now_us();
}

void nx_mqtt_port_assert(const char* file, unsigned line) {
    (void)line;
    nx_lwip_port_assert(file);
}

const nx_flashdb_operation_t* nx_flashdb_operation_get(void) {
    return NULL;
}

int main(void) {
    const nx_clock_t clock = {now, NULL};
    const nx_time_us_t deadline = nx_deadline_after(now(NULL), 5000000U);
    const nx_tls_runtime_config_t runtime_config = {
        arena,
        sizeof(arena),
        nx_factory_entropy(NX_ENTROPY_ID_RANDOM),
        clock,
        {untrusted_utc, NULL},
        100000U,
        NX_TLS_ENTROPY_UNKNOWN};
    /* Exercise admission with an unqualified entropy declaration. The Board,
     * clock, factory, PHY, Flash and stack remain cold in this consumer. */
    if (nx_tls_runtime_start(&runtime, &runtime_config, deadline) ==
        NX_SUCCESS) {
        return 1;
    }
    const nx_mqtt_memory_t mqtt_memory = {mqtt_network,  sizeof(mqtt_network),
                                          mqtt_outgoing, 2U,
                                          mqtt_incoming, 2U,
                                          mqtt_controls, 2U};
    const example_network_route_config_t route = {
        &network, &dhcp, &dns, clock, "server.test", deadline, 2U, 2000U};
    const example_gateway_config_t gateway = {
        .tcp = &tcp,
        .tls = &tls,
        .mqtt = &mqtt,
        .mqtt_memory = &mqtt_memory,
        .tcp_config = {.network = &network,
                       .receive_budget = 2U,
                       .port = 8883U},
        .tls_config = {.runtime = &runtime,
                       .trust_der = test_ca,
                       .trust_bytes = sizeof(test_ca),
                       .tx_stage = tls_stage,
                       .tx_stage_bytes = sizeof(tls_stage)},
        .identity = {.client_id = "cold-gateway", .client_id_size = 12U},
        .clock = clock,
        .wait = unavailable,
        .topic = "fixture/status",
        .topic_bytes = 14U,
        .policy = {deadline, 1000000U, 100000U, 2U},
        .cleanup_budget_us = 100000U,
        .mqtt_io_budget = 100U,
        .progress_turns = 500U};
    const nx_flashdb_operation_t operation = {&database, deadline};
    if (example_gateway_managed_start(&application, &route, no_link, NULL) !=
        NX_SUCCESS) {
        return 2;
    }
    nx_result_t run = example_gateway_managed_run(
        &application, &outbox, &operation, &gateway, &report);
    nx_result_t stop = example_gateway_managed_stop(&application, &operation,
                                                    deadline, &report);
    return run == NX_ERROR_PERMISSION && stop == NX_SUCCESS ? 0 : 3;
}
