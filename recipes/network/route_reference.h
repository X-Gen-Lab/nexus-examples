/**
 * \file            route_reference.h
 * \brief           Managed IPv4 preparation and explicit trusted UTC bridge
 * \author          Nexus Team
 */
#ifndef NEXUS_EXAMPLE_ROUTE_REFERENCE_H
#define NEXUS_EXAMPLE_ROUTE_REFERENCE_H
#include "nexus/components/wall_clock.h"
#include "nexus/network/lwip_services.h"
#ifdef __cplusplus
extern "C" {
#endif
/** \brief Same-owner public stack resources, retained through successful stop.
 */
typedef struct {
    nx_lwip_adapter_t* network;
    nx_lwip_dhcp_t* dhcp;
    nx_lwip_dns_t* dns;
    nx_clock_t clock;
    const char* hostname;
    nx_time_us_t deadline;
    size_t receive_budget;
    uint32_t progress_turns;
} example_network_route_config_t;
/** \brief Ready is valid only while link, lease and the fixed clock persist.
 */
typedef enum {
    EXAMPLE_ROUTE_COLD,
    EXAMPLE_ROUTE_ADDRESS,
    EXAMPLE_ROUTE_RESOLVING,
    EXAMPLE_ROUTE_READY,
    EXAMPLE_ROUTE_DRAINING,
    EXAMPLE_ROUTE_STOPPED
} example_network_route_state_t;
/** \brief Caller-owned phase and public-resource identities; fields private.
 */
typedef struct {
    example_network_route_config_t config;
    nx_time_us_t last_time;
    uint32_t remaining_turns;
    uint8_t local[4];
    uint8_t peer[4];
    nx_result_t failure;
    example_network_route_state_t state;
    bool dhcp_owned;
    bool dns_owned;
} example_network_route_t;
/**
 * \brief Prepare bounded DHCP/DNS composition without starting any worker.
 * \param[in,out] route: Zeroed or successfully stopped exclusive metadata.
 * \param[in] config: Disjoint stable public resources and finite total budget.
 * \return SUCCESS, INVALID, STATE or TIMEOUT, with no stack I/O on rejection.
 * \note Task-only in the existing serialized NO_SYS executor. Network is
 *       already attached with the managed profile and its DNS source selected.
 *       Caller applies actual PHY observations through adapter_apply_link.
 *       All static-address users are joined before DHCP starts. Hostname,
 *       clock, handles and exact storage outlive every DNS callback.
 */
nx_result_t
example_network_route_start(example_network_route_t* route,
                            const example_network_route_config_t* config);
/**
 * \brief Progress real stack services and validate link/lease continuity.
 * \param[in,out] route: Same live serialized owner.
 * \param[in] link_up: Current proven product PHY fact, already applied to MAC.
 * \return SUCCESS only for a resolved current route; BUSY while preparing;
 *         actual error with DRAINING retained on loss, expiry or budget
 * failure. \note Call before new sessions and from the product's bounded wait
 * adapter. Link loss or changed/lost address closes route admission
 * immediately; caller then unwinds TCP/TLS/MQTT using their real cleanup. No
 * in-place reconnect, static fallback, deadline renewal or detached callback
 * reuse. A finite turn budget also bounds a clock that stops advancing.
 */
nx_result_t example_network_route_step(example_network_route_t* route,
                                       bool link_up);
/**
 * \brief Copy the peer selected by the most recent successful step.
 * \param[in] route: Same-owner READY metadata after a current step.
 * \param[out] address: Disjoint four-byte address, unchanged on failure.
 * \return SUCCESS, INVALID or STATE; no I/O or hidden time refresh.
 * \note The peer is a value, not a lasting readiness/ownership token. Recheck
 *       step during session progress and use the same hostname for TLS.
 */
nx_result_t example_network_route_peer(const example_network_route_t* route,
                                       uint8_t address[4]);
/**
 * \brief Join local DNS callbacks and stop the actual DHCP owner.
 * \param[in,out] route: Original live metadata after all TCP/TLS/MQTT users
 * join. \return SUCCESS after local withdrawal; BUSY/error retains original
 * handles. \note Caller closes higher sessions first. Stack timers and network
 * remain live while a cancelled upstream DNS query still holds its callback.
 *       This works after the operation deadline without revoking any borrow.
 *       No adapter/MAC/clock shutdown or server lease-withdrawal is claimed.
 */
nx_result_t example_network_route_stop(example_network_route_t* route);
/** \brief Selected UTC policy; storage lives through every TLS callback. */
typedef struct {
    const nx_wall_clock_t* wall;
    nx_clock_t clock;
    uint64_t max_uncertainty_us;
} example_trusted_utc_t;
/**
 * \brief Read bounded-quality trusted UTC for nx_tls_utc_t.read.
 * \param[in] context: Stable serialized example_trusted_utc_t and clock domain.
 * \param[out] seconds: Disjoint Unix seconds, unchanged on failure.
 * \return SUCCESS, INVALID, PERMISSION for insufficient trust/uncertainty, or
 *         actual wall-clock error. Negative Unix time is unsupported.
 * \note No I/O, SNTP query, trust upgrade or clock adjustment. Product chooses
 *       trust and the uncertainty bound; plain SNTP remains MEASURED.
 */
nx_result_t example_trusted_utc_read(void* context, int64_t* seconds);
#ifdef __cplusplus
}
#endif
#endif
