/**
 * \file            terminal_application.h
 * \brief           One bounded UI, managed IPv4 and settings application
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-22
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#ifndef EXAMPLE_TERMINAL_APPLICATION_H
#define EXAMPLE_TERMINAL_APPLICATION_H
#include "../../recipes/network/route_reference.h"
#include "../../recipes/ui/display_reference.h"
#include "nexus/components/settings.h"
#ifdef __cplusplus
extern "C" {
#endif

/** \brief Whole product snapshot; no compiler-dependent struct persistence. */
typedef struct {
    uint16_t refresh_ms;
    uint16_t offline_color;
    uint16_t online_color;
} example_terminal_parameters_t;

/** \brief Caller-owned, already prepared components for one serialized task.
 * \note All objects, hostname, tile and exact 16-byte UDP receive storage are
 *       disjoint and live through successful stop. The display is READY;
 *       measured calibration matches its orientation. Settings is open with
 *       exclusive staging; its Flash port enforces finite maintenance budgets.
 *       DHCP/DNS/UDP objects are zeroed or successfully stopped. All other
 *       address consumers join before this application's DHCP stop. The
 *       adapter is attached with managed services. Product applies actual
 *       PHY link observations before step; no Board or AV wiring is inferred.
 *       No allocator, task, framebuffer, TLS, socket owner or OS is created.
 */
typedef struct {
    nx_ili9341_t* display;
    nx_xpt2046_t* touch;
    example_touch_calibration_t calibration;
    uint16_t* tile;
    size_t tile_capacity;
    uint16_t bar_height;
    nx_settings_t* settings;
    uint32_t settings_key;
    example_terminal_parameters_t defaults;
    example_network_route_config_t route;
    nx_lwip_udp_t* udp;
    uint8_t* receive;
    uint16_t local_port;
    uint16_t remote_port;
} example_terminal_config_t;

/** \brief Same-task application metadata; observers read only between calls.
 * \note Zero before first start. After any failed stop, retain this object and
 *       every remaining owned component; retry stop with a new finite budget.
 */
typedef struct {
    example_terminal_config_t config;
    example_network_route_t route;
    example_terminal_parameters_t parameters;
    example_ui_fill_t fill;
    example_touch_point_t point;
    nx_time_us_t last_time;
    nx_time_us_t next_report;
    uint32_t version;
    uint32_t persisted_version;
    uint32_t sequence;
    nx_result_t network_error;
    nx_result_t ui_error;
    nx_storage_status_t storage_error;
    bool live;
    bool closing;
    bool route_owned;
    bool udp_owned;
    bool display_owned;
    bool touch_owned;
    bool network_ready;
    bool display_dirty;
    bool report_pending;
    bool touched;
} example_terminal_t;

/**
 * \brief Load exact settings and prepare route metadata without device I/O.
 * \param[in,out] terminal: Zeroed or fully stopped exclusive application.
 * \param[in] config: Disjoint binding, caller buffers and fixed route budget.
 * \return SUCCESS, INVALID, STATE or exact settings/route admission error.
 * \note Same task; missing key uses explicit defaults without Flash writes.
 *       Malformed stored schema never silently becomes defaults. Successful
 *       start owns touch/display shutdown and route/UDP withdrawal; it does
 *       not own the underlying Platform, buses, MAC, Settings or Flash store.
 */
nx_result_t example_terminal_start(example_terminal_t* terminal,
                                   const example_terminal_config_t* config);

/**
 * \brief Progress one route turn, one touch sample and at most one UI tile.
 * \param[in,out] terminal: Live same-task application; no concurrent calls.
 * \param[in] link_up: Current PHY fact already applied to the adapter.
 * \param[in] deadline: Finite whole-turn budget in the configured clock.
 * \return SUCCESS while UI can run, or exact UI/time/admission error.
 * \note Network failure is separately latched in network_error; the UI stays
 *       usable. Close UDP before retiring DNS/DHCP, one cleanup attempt per
 *       turn. No implicit reconnect, route deadline renewal or Flash writes.
 *       At most one 16-byte copied UDP status packet is admitted per turn;
 *       SUCCESS means local acceptance, never peer delivery. RX is not used
 *       as a command/authorization path. Accepted lower loans remain their
 *       owner's responsibility. All device calls settle their tile/sample.
 */
nx_result_t example_terminal_step(example_terminal_t* terminal, bool link_up,
                                  nx_time_us_t deadline);

/**
 * \brief Validate, stage in RAM, then publish a complete parameter snapshot.
 * \param[in,out] terminal: Live same-task application with open Settings.
 * \param[in] parameters: Disjoint copied candidate, refresh 10..5000 ms.
 * \return SUCCESS, INVALID, EXHAUSTED or exact staging error.
 * \note No Flash access. Versions never wrap; active rectangle color stays
 *       fixed until its final tile. Caller authenticates any remote or Shell
 *       source before this API; this function does not grant authorization.
 */
nx_result_t
example_terminal_apply(example_terminal_t* terminal,
                       const example_terminal_parameters_t* parameters);

/**
 * \brief Explicitly commit staged parameters in an authorized maintenance gap.
 * \param[in,out] terminal: Live same-task application and exclusive Settings.
 * \param[in] maintenance_allowed: Product has paused unsafe real-time users.
 * \return SUCCESS, PERMISSION, STATE or actual persistence failure.
 * \note No retry or recovery. F407 single-bank erase/program can stall the
 *       CPU; ordinary UI turns never call this. Mutating failure can have an
 *       ambiguous durable outcome: retain active RAM, reopen Storage/Settings
 *       externally, and inspect the surviving version before another save.
 */
nx_result_t example_terminal_save(example_terminal_t* terminal,
                                  bool maintenance_allowed);

/**
 * \brief Close admission, then join UDP, route, touch and display in order.
 * \param[in,out] terminal: Original live or partially closing application.
 * \param[in] deadline: Finite cleanup deadline in the original clock domain.
 * \return SUCCESS only after all these owners withdraw; exact error otherwise.
 * \note Task-only; errors retain exact remaining owners and all their storage.
 *       Caller joins application users first, then stops Platform/shared buses
 *       only after SUCCESS. Does not save/close Settings or stop the adapter.
 */
nx_result_t example_terminal_stop(example_terminal_t* terminal,
                                  nx_time_us_t deadline);
#ifdef __cplusplus
}
#endif
#endif /* EXAMPLE_TERMINAL_APPLICATION_H */
