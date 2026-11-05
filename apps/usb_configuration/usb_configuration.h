/**
 * \file            usb_configuration.h
 * \brief           Product-owned finite USB configuration and shutdown policy
 * \author          Nexus Team
 */
#ifndef EXAMPLE_USB_CONFIGURATION_H
#define EXAMPLE_USB_CONFIGURATION_H
#include "nexus/components/usb_control.h"
#include "nexus/io/usb_activation.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum {
    EXAMPLE_USB_IDLE,
    EXAMPLE_USB_DRAIN,
    EXAMPLE_USB_STAGE
} example_usb_phase_t;
/** \brief Single serialized application owner; never copy a live instance. */
typedef struct {
    const nx_usb_device_profile_t* profile;
    const nx_usb_activation_port_t* activation;
    const nx_usb_endpoint_port_t* bulk;
    nx_usb_control_t control;
    nx_usb_control_request_t reply;
    nx_usb_request_t tx;
    uint64_t transaction_epoch;
    uint64_t handled_epoch;
    uint64_t observed_epoch;
    nx_time_us_t deadline;
    nx_result_t failure;
    example_usb_phase_t phase;
    uint8_t payload[8];
    uint8_t configuration;
    uint8_t target;
    uint8_t configuration_reply;
    bool ready;
    bool active;
    bool tx_borrowed;
    bool tx_cancel_sent;
    bool reply_borrowed;
    bool configuration_transaction;
    bool stopping;
    bool closed;
} example_usb_t;
/**
 * \brief Bind zero-initialized or fully closed application storage.
 * \param[in,out] app: Single privileged unmasked task owner; retain through
 *       successful close and external observer joins.
 * \param[in] profile: Stable EP0 IN/OUT and EP1 bulk IN/64 profile, alive
 * through every request and close; immutable descriptors remain
 * application-owned. \param[in] activation: Same physical controller, not a
 * separate lifecycle. \return SUCCESS or INVALID/STATE; no hardware or worker
 * is started.
 */
nx_result_t example_usb_initialize(example_usb_t* app,
                                   const nx_usb_device_profile_t* profile,
                                   const nx_usb_activation_port_t* activation);
/**
 * \brief Advance bounded standard-request, configuration and recovery work.
 * \param[in,out] app: Same serialized owner with its original monotonic clock.
 * \return SUCCESS or BUSY while draining; errors retain all outstanding loans.
 * \note SET_CONFIGURATION readiness follows actual STATUS completion and the
 *       current epoch. Superseded staged configuration closes the controller;
 *       this recipe does not replay requests or implement USB classes.
 */
nx_result_t example_usb_step(example_usb_t* app);
/**
 * \brief Copy and admit one eight-byte application sample when configured.
 * \param[in,out] app: Same owner; one fixed request, no queue or worker.
 * \param[in] bytes: Eight readable bytes, copied before acceptance.
 * \return SUCCESS, BUSY or STATE; caller input is never retained. The complete
 *       application object stays alive through actual settlement and close.
 */
nx_result_t example_usb_send(example_usb_t* app, const uint8_t bytes[8]);
/**
 * \brief Close admission, cancel, service and stop without revoking loans.
 * \param[in,out] app: Same owner, called repeatedly until successful join.
 * \return SUCCESS only after control and data requests settle and controller
 *       stop succeeds; BUSY/IO retain storage and recovery responsibility.
 * \note Platform and Board release follow successful close. Reinitialization
 *       requires explicit Platform restart and all external observers joined.
 */
nx_result_t example_usb_close(example_usb_t* app);
#ifdef __cplusplus
}
#endif
#endif
