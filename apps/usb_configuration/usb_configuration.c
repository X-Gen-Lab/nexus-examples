/**
 * \file            usb_configuration.c
 * \brief           Explicit application-owned Chapter 9 subset and shutdown
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-05
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "usb_configuration.h"
#include "nexus/components/usb_descriptors.h"
#include <string.h>

/* Software-reference VID/PID, one vendor interface and one bulk IN direction.
 * Product USB identity, power claims and electrical qualification stay
 * external. */
static const uint8_t device_descriptor[] = {18U, 1U,  0U,    2U,    0U, 0U,
                                            0U,  64U, 0x09U, 0x12U, 1U, 0U,
                                            0U,  1U,  0U,    0U,    0U, 1U};
static const uint8_t configuration_descriptor[] = {
    9U, 2U,    25U, 0U, 1U, 1U, 0U, 0x80U, 50U, 9U,  4U, 0U, 0U,
    1U, 0xFFU, 0U,  0U, 0U, 7U, 5U, 0x81U, 2U,  64U, 0U, 0U};
static const uint8_t device_status[2] = {0U, 0U};

/** \brief Failure closes admission; no retry can replay a staged transaction.
 */
static void fail(example_usb_t* app, nx_result_t result) {
    if (app->failure == NX_SUCCESS)
        app->failure = result;
    app->ready = false;
    app->stopping = true;
}
/** \brief Acquire final data completion before detaching application storage.
 */
static void fold_data(example_usb_t* app) {
    if (app->tx_borrowed &&
        nx_request_state(&app->tx.base) == NX_REQUEST_SETTLED) {
        nx_result_t result = app->tx.base.result;
        bool cancelled = app->tx_cancel_sent && result == NX_ERROR_CANCELLED;
        app->tx_borrowed = app->tx_cancel_sent = false;
        if (!app->stopping && result != NX_SUCCESS && !cancelled)
            fail(app, result);
    }
}
nx_result_t example_usb_initialize(example_usb_t* app,
                                   const nx_usb_device_profile_t* profile,
                                   const nx_usb_activation_port_t* activation) {
    if (app == NULL || profile == NULL || activation == NULL ||
        activation->ops == NULL || activation->ops->set == NULL ||
        nx_usb_profile_validate(profile) != NX_SUCCESS ||
        profile->endpoint_count != 3U ||
        profile->port->context != activation->context)
        return NX_ERROR_INVALID;
    if (app->profile != NULL && !app->closed)
        return NX_ERROR_STATE;
    const nx_usb_endpoint_profile_t* bulk =
        nx_usb_profile_endpoint(profile, 1U, true);
    if (bulk == NULL || bulk->transfer != NX_USB_PROFILE_BULK ||
        bulk->max_packet != 64U || profile->port->ops->read_events == NULL)
        return NX_ERROR_INVALID;
    const nx_usb_descriptor_view_t device = {device_descriptor,
                                             sizeof(device_descriptor)};
    const nx_usb_descriptor_view_t configuration = {
        configuration_descriptor, sizeof(configuration_descriptor)};
    if (nx_usb_descriptor_validate(profile, &device) != NX_SUCCESS ||
        nx_usb_descriptor_validate(profile, &configuration) != NX_SUCCESS)
        return NX_ERROR_INVALID;
    memset(app, 0, sizeof(*app));
    app->profile = profile;
    app->activation = activation;
    app->bulk = bulk->port;
    nx_request_initialize(&app->reply.base);
    nx_request_initialize(&app->tx.base);
    return nx_usb_control_initialize(&app->control, profile);
}

/** \brief Stop is independent of borrowed EP0/data packet completion. */
nx_result_t example_usb_close(example_usb_t* app) {
    if (app == NULL || app->profile == NULL)
        return NX_ERROR_INVALID;
    if (app->closed)
        return NX_SUCCESS;
    app->ready = false;
    app->stopping = true;
    fold_data(app);
    if (app->tx_borrowed && !app->tx_cancel_sent) {
        nx_result_t result = nx_usb_endpoint_cancel(app->bulk, &app->tx);
        if (result == NX_SUCCESS)
            app->tx_cancel_sent = true;
    }
    if (app->reply_borrowed &&
        nx_request_state(&app->reply.base) != NX_REQUEST_SETTLED)
        (void)nx_usb_control_cancel(&app->control, &app->reply,
                                    app->transaction_epoch);
    (void)nx_usb_control_step(&app->control);
    nx_result_t stopped = nx_usb_device_stop(app->profile->port);
    /* A successful device stop ends provider loans, not the outer adapter
     * observation. Fold its last packet before releasing the application. */
    (void)nx_usb_control_step(&app->control);
    fold_data(app);
    if (app->reply_borrowed &&
        nx_request_state(&app->reply.base) == NX_REQUEST_SETTLED)
        app->reply_borrowed = false;
    if (stopped == NX_SUCCESS && !app->tx_borrowed && !app->reply_borrowed) {
        app->active = false;
        app->configuration = 0U;
        app->configuration_transaction = false;
        app->closed = true;
        return NX_SUCCESS;
    }
    return stopped == NX_SUCCESS ? NX_ERROR_BUSY : stopped;
}

/** \brief Admission alone does not submit a STATUS or publish configuration. */
static nx_result_t begin_status(example_usb_t* app) {
    nx_result_t result = nx_usb_control_status_prepare(
        &app->reply, app->transaction_epoch, app->deadline);
    if (result == NX_SUCCESS)
        result = nx_usb_control_begin(&app->control, &app->reply);
    if (result == NX_SUCCESS) {
        app->reply_borrowed = true;
        app->handled_epoch = app->transaction_epoch;
        app->phase = EXAMPLE_USB_IDLE;
    }
    return result;
}
/** \brief Never disable a nonzero direction while its old data is borrowed. */
static nx_result_t configure(example_usb_t* app) {
    if (app->phase == EXAMPLE_USB_DRAIN) {
        if (app->tx_borrowed) {
            if (!app->tx_cancel_sent) {
                nx_result_t result =
                    nx_usb_endpoint_cancel(app->bulk, &app->tx);
                if (result == NX_SUCCESS)
                    app->tx_cancel_sent = true;
                else if (nx_request_state(&app->tx.base) == NX_REQUEST_SETTLED)
                    fold_data(app);
                else if (result != NX_ERROR_BUSY)
                    fail(app, result);
            }
            return NX_ERROR_BUSY;
        }
        if (app->active) {
            nx_result_t result = nx_usb_activation_set(
                app->activation, app->transaction_epoch, 0x81U, false);
            if (result != NX_SUCCESS)
                return result;
            app->active = false;
        }
        app->phase = EXAMPLE_USB_STAGE;
        return NX_ERROR_BUSY;
    }
    if (app->target == 1U) {
        nx_result_t result = nx_usb_activation_set(
            app->activation, app->transaction_epoch, 0x81U, true);
        if (result != NX_SUCCESS)
            return result;
        app->active = true;
    }
    return begin_status(app);
}
/** \brief Standard descriptor bytes are application policy, not SDK state. */
static nx_result_t reply_in(example_usb_t* app, const uint8_t* bytes,
                            size_t count) {
    nx_result_t result = nx_usb_control_in_prepare(
        &app->reply, bytes, count, app->transaction_epoch, app->deadline);
    if (result == NX_SUCCESS)
        result = nx_usb_control_begin(&app->control, &app->reply);
    if (result == NX_SUCCESS) {
        app->reply_borrowed = true;
        app->handled_epoch = app->transaction_epoch;
    }
    return result;
}
/** \brief Unsupported values are rejected, never silently repaired. */
static nx_result_t dispatch(example_usb_t* app, const nx_usb_setup_t* setup,
                            uint8_t address) {
    if (setup->request_type == 0U && setup->request == 9U &&
        setup->value <= 1U && setup->index == 0U && setup->length == 0U &&
        address != 0U) {
        app->ready = false;
        app->configuration_transaction = true;
        app->target = (uint8_t)setup->value;
        app->handled_epoch = setup->epoch;
        app->phase = EXAMPLE_USB_DRAIN;
        return NX_ERROR_BUSY;
    }
    if (setup->request_type == 0U && setup->request == 5U &&
        setup->value <= 127U && setup->index == 0U && setup->length == 0U &&
        app->configuration == 0U) {
        nx_result_t result = begin_status(app);
        if (result == NX_SUCCESS) {
            /* begin has no packet action. Stage the address before the next
             * explicit control step can submit STATUS IN. */
            result = nx_usb_device_address(app->profile->port, setup->epoch,
                                           (uint8_t)setup->value);
            if (result != NX_SUCCESS)
                fail(app, result);
        }
        return result;
    }
    if (setup->request_type == 0x80U && setup->request == 6U &&
        setup->index == 0U && setup->length != 0U) {
        if (setup->value == 0x0100U)
            return reply_in(app, device_descriptor, sizeof(device_descriptor));
        if (setup->value == 0x0200U)
            return reply_in(app, configuration_descriptor,
                            sizeof(configuration_descriptor));
    }
    if (setup->request_type == 0x80U && setup->request == 8U &&
        setup->value == 0U && setup->index == 0U && setup->length == 1U) {
        app->configuration_reply = app->configuration;
        return reply_in(app, &app->configuration_reply, 1U);
    }
    if (setup->request_type == 0x80U && setup->request == 0U &&
        setup->value == 0U && setup->index == 0U && setup->length == 2U)
        return reply_in(app, device_status, sizeof(device_status));
    nx_result_t result =
        nx_usb_setup_reject(&app->profile->setup_decision, setup->epoch);
    if (result == NX_SUCCESS)
        app->handled_epoch = setup->epoch;
    return result;
}
nx_result_t example_usb_step(example_usb_t* app) {
    if (app == NULL || app->profile == NULL)
        return NX_ERROR_INVALID;
    if (app->closed)
        return NX_ERROR_STATE;
    if (app->stopping)
        return example_usb_close(app);
    fold_data(app);
    if (app->stopping)
        return example_usb_close(app);
    nx_result_t result = nx_usb_control_step(&app->control);
    nx_usb_control_snapshot_t snapshot;
    nx_result_t sampled = nx_usb_control_snapshot(&app->control, &snapshot);
    if (sampled != NX_SUCCESS ||
        (result != NX_SUCCESS && result != NX_ERROR_BUSY)) {
        fail(app, sampled != NX_SUCCESS ? sampled : result);
        return example_usb_close(app);
    }
    const nx_usb_device_state_t* state = &snapshot.device;
    app->observed_epoch = state->epoch;
    if (!state->started || state->closing ||
        (state->address == 0U && (app->configuration != 0U || app->active))) {
        fail(app, NX_ERROR_STATE);
        return example_usb_close(app);
    }
    /* Drain bounded value events. Readiness comes from the authoritative
     * current state, not a queued historical SETUP or RESET observation. */
    nx_usb_event_t events[2];
    size_t count = 0U;
    result = nx_usb_device_read_events(app->profile->port, events, 2U, &count);
    if (result != NX_SUCCESS && result != NX_ERROR_EMPTY) {
        fail(app, result);
        return example_usb_close(app);
    }
    if (app->reply_borrowed &&
        nx_request_state(&app->reply.base) == NX_REQUEST_SETTLED) {
        result = app->reply.base.result;
        app->reply_borrowed = false;
        if (app->configuration_transaction) {
            app->configuration_transaction = false;
            if (result != NX_SUCCESS ||
                state->epoch != app->transaction_epoch) {
                fail(app, result != NX_SUCCESS ? result : NX_ERROR_STATE);
                return example_usb_close(app);
            }
            app->configuration = app->target;
        } else if (result != NX_SUCCESS && result != NX_ERROR_CANCELLED) {
            fail(app, result);
            return example_usb_close(app);
        }
    }
    app->ready = app->configuration == 1U && app->active &&
                 !app->configuration_transaction;
    if (app->phase != EXAMPLE_USB_IDLE) {
        if (state->epoch != app->transaction_epoch || !state->control_pending) {
            fail(app, NX_ERROR_CANCELLED);
            return example_usb_close(app);
        }
        if (nx_deadline_expired(app->deadline, nx_time_now_us())) {
            fail(app, NX_ERROR_TIMEOUT);
            return example_usb_close(app);
        }
        result = configure(app);
        if (result != NX_SUCCESS && result != NX_ERROR_BUSY) {
            fail(app, result);
            return example_usb_close(app);
        }
        return result;
    }
    if (!app->reply_borrowed && state->control_pending &&
        snapshot.phase == NX_USB_CONTROL_WAIT_REPLY &&
        state->epoch != app->handled_epoch) {
        if (state->epoch != app->transaction_epoch) {
            app->transaction_epoch = state->epoch;
            app->deadline = nx_deadline_after(nx_time_now_us(), 1000000U);
        }
        return dispatch(app, &state->setup, state->address);
    }
    return NX_SUCCESS;
}
nx_result_t example_usb_send(example_usb_t* app, const uint8_t bytes[8]) {
    if (app == NULL || bytes == NULL || app->profile == NULL)
        return NX_ERROR_INVALID;
    if (!app->ready || app->stopping || app->closed)
        return NX_ERROR_STATE;
    if (app->tx_borrowed)
        return NX_ERROR_BUSY;
    nx_usb_device_state_t state;
    nx_result_t result = nx_usb_device_state(app->profile->port, &state);
    if (result != NX_SUCCESS || !state.started || state.closing ||
        state.address == 0U || state.epoch != app->observed_epoch) {
        app->ready = false;
        return result != NX_SUCCESS ? result : NX_ERROR_STATE;
    }
    memcpy(app->payload, bytes, sizeof(app->payload));
    result = nx_usb_in_prepare(&app->tx, app->payload, sizeof(app->payload),
                               NX_USB_DATA, 0U,
                               nx_deadline_after(nx_time_now_us(), 1000000U));
    if (result == NX_SUCCESS)
        result = nx_usb_endpoint_submit(app->bulk, &app->tx);
    if (result == NX_SUCCESS)
        app->tx_borrowed = true;
    return result;
}
