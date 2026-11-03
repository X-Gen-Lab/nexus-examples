/**
 * \file            gateway_application.c
 * \brief           Direct bounded TCP/TLS/MQTT product workflow
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-02
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "gateway_application.h"
#include <string.h>

#define EVENT_PUBLISH  (1U << NX_MQTT_EVENT_PUBLISH)
#define EVENT_PUBACK   (1U << NX_MQTT_EVENT_PUBACK)
#define EVENT_SUBACK   (1U << NX_MQTT_EVENT_SUBACK)
#define EVENT_UNSUBACK (1U << NX_MQTT_EVENT_UNSUBACK)

static bool overlaps(const void* first, size_t first_bytes, const void* second,
                     size_t second_bytes) {
    const uintptr_t a = (uintptr_t)first;
    const uintptr_t b = (uintptr_t)second;
    return a <= b ? b - a < first_bytes : a - b < second_bytes;
}
static bool disjoint(const example_gateway_application_t* application,
                     const void* value, size_t bytes) {
    return value != NULL &&
           !overlaps(application, sizeof(*application), value, bytes);
}
static bool metadata_valid(const example_gateway_application_t* application,
                           const example_gateway_config_t* config,
                           const example_gateway_report_t* report) {
    return application != NULL && config != NULL && report != NULL &&
           disjoint(application, config, sizeof(*config)) &&
           disjoint(application, report, sizeof(*report)) &&
           !overlaps(config, sizeof(*config), report, sizeof(*report));
}
static bool config_valid(const example_gateway_application_t* application,
                         const example_gateway_config_t* config) {
    return config->tcp != NULL && config->tls != NULL && config->mqtt != NULL &&
           config->mqtt_memory != NULL && config->tcp_config.network != NULL &&
           config->tcp_config.receive_budget != 0U &&
           config->tcp_config.port != 0U && config->clock.read != NULL &&
           config->wait != NULL && config->tls_config.runtime != NULL &&
           config->tls_config.transport == NULL &&
           config->tls_config.trust_der != NULL &&
           config->tls_config.trust_bytes != 0U &&
           config->tls_config.trust_bytes <= NX_TLS_DER_MAX &&
           config->tls_config.hostname != NULL &&
           config->tls_config.hostname_length != 0U &&
           config->tls_config.hostname_length <= 253U &&
           config->tls_config.tx_stage != NULL &&
           config->tls_config.tx_stage_bytes == NX_TLS_TX_STAGE_BYTES &&
           config->identity.client_id != NULL &&
           config->identity.client_id_size != 0U && config->topic != NULL &&
           config->topic_bytes != 0U && config->topic_bytes <= 64U &&
           config->payload_bytes != 0U &&
           config->payload_bytes <= EXAMPLE_GATEWAY_PAYLOAD_BYTES &&
           disjoint(application, config->payload, config->payload_bytes) &&
           config->cleanup_budget_us != 0U &&
           config->cleanup_budget_us != NX_DEADLINE_NEVER &&
           config->mqtt_io_budget != 0U && config->progress_turns != 0U &&
           config->progress_turns <= UINT16_MAX;
}
static bool input_span(const example_gateway_application_t* application,
                       const example_gateway_report_t* report,
                       const void* value, size_t bytes) {
    return bytes == 0U || (disjoint(application, value, bytes) &&
                           !overlaps(report, sizeof(*report), value, bytes));
}
static bool inputs_disjoint(const example_gateway_application_t* application,
                            const example_gateway_config_t* config,
                            const example_gateway_report_t* report) {
    return input_span(application, report, config->tcp, 1U) &&
           input_span(application, report, config->tls, 1U) &&
           input_span(application, report, config->mqtt, 1U) &&
           input_span(application, report, config->mqtt_memory, 1U) &&
           input_span(application, report, config->tcp_config.network, 1U) &&
           input_span(application, report, config->tls_config.runtime, 1U) &&
           input_span(application, report, config->tls_config.tx_stage,
                      config->tls_config.tx_stage_bytes) &&
           input_span(application, report, config->topic,
                      config->topic_bytes) &&
           input_span(application, report, config->payload,
                      config->payload_bytes) &&
           input_span(application, report, config->identity.client_id,
                      config->identity.client_id_size) &&
           input_span(application, report, config->identity.username,
                      config->identity.username_size) &&
           input_span(application, report, config->identity.password,
                      config->identity.password_size) &&
           input_span(application, report, config->tls_config.trust_der,
                      config->tls_config.trust_bytes) &&
           input_span(application, report, config->tls_config.client_der,
                      config->tls_config.client_bytes) &&
           input_span(application, report, config->tls_config.key_der,
                      config->tls_config.key_bytes) &&
           input_span(application, report, config->tls_config.hostname,
                      config->tls_config.hostname_length + 1U);
}
static bool local_released(const example_gateway_application_t* application) {
    return !application->mqtt_owned && !application->tls_owned &&
           !application->tcp_owned;
}
static void publish_report(example_gateway_application_t* application,
                           example_gateway_report_t* report) {
    application->report.attempts = application->policy.attempts;
    application->report.local_sessions_released = local_released(application);
    *report = application->report;
}
static nx_result_t guard(example_gateway_application_t* application,
                         nx_time_us_t deadline) {
    const nx_time_us_t now =
        application->config.clock.read(application->config.clock.context);
    if (now == NX_DEADLINE_NEVER || now < application->policy.last_time) {
        return NX_ERROR_STATE;
    }
    if (application->config.stop != NULL &&
        application->config.stop(application->config.wait_context)) {
        return NX_ERROR_CANCELLED;
    }
    return nx_deadline_expired(deadline, now) ? NX_ERROR_TIMEOUT : NX_SUCCESS;
}
static nx_result_t turn(example_gateway_application_t* application,
                        nx_time_us_t deadline) {
    const nx_result_t result = guard(application, deadline);
    if (result != NX_SUCCESS) {
        return result;
    }
    if (application->remaining_turns == 0U) {
        return NX_ERROR_EXHAUSTED;
    }
    --application->remaining_turns;
    ++application->report.turns;
    return NX_SUCCESS;
}
static nx_result_t wait_turn(example_gateway_application_t* application,
                             nx_time_us_t deadline, bool tls_progress) {
    nx_result_t result = turn(application, deadline);
    if (result != NX_SUCCESS) {
        return result;
    }
    if (application->tcp_owned) {
        result = nx_lwip_tcp_service(application->config.tcp);
        if (result != NX_SUCCESS && result != NX_ERROR_BUSY) {
            return result;
        }
    }
    if (tls_progress && application->tls_owned) {
        result = nx_tls_service(application->config.tls);
        if (result != NX_SUCCESS && result != NX_ERROR_BUSY) {
            return result;
        }
    }
    result =
        application->config.wait(application->config.wait_context, deadline);
    if (result != NX_SUCCESS && result != NX_ERROR_BUSY) {
        return result;
    }
    return guard(application, deadline);
}
static nx_result_t mqtt_idle(void* context, nx_time_us_t deadline) {
    return wait_turn(context, deadline, true);
}
static nx_result_t feed(example_gateway_application_t* application) {
    if (application->config.watchdog == NULL) {
        return NX_SUCCESS;
    }
    const nx_result_t result =
        nx_watchdog_port_feed(application->config.watchdog);
    if (result == NX_SUCCESS) {
        ++application->report.watchdog_feeds;
    }
    return result;
}
static void event_failure(example_gateway_application_t* application,
                          nx_result_t reason) {
    if (!application->event_failed) {
        application->report.primary = reason;
        application->event_failed = true;
    }
}
static void mqtt_event(void* context, const nx_mqtt_event_t* event) {
    example_gateway_application_t* application = context;
    if (event->status != NX_SUCCESS) {
        event_failure(application, event->status);
        return;
    }
    switch (event->kind) {
        case NX_MQTT_EVENT_SUBACK:
            if (event->packet_id == application->subscribed) {
                if (event->qos != 1U) {
                    event_failure(application, NX_ERROR_PERMISSION);
                } else {
                    application->report.mqtt_events |= EVENT_SUBACK;
                }
            }
            break;
        case NX_MQTT_EVENT_UNSUBACK:
            if (event->packet_id == application->unsubscribed) {
                application->report.mqtt_events |= EVENT_UNSUBACK;
            }
            break;
        case NX_MQTT_EVENT_PUBACK:
            if (event->packet_id == application->published) {
                const nx_result_t result = example_gateway_ack(
                    &application->policy, application->attempt.session,
                    event->packet_id);
                if (result != NX_SUCCESS) {
                    event_failure(application, result);
                } else {
                    application->report.delivered = true;
                    application->report.mqtt_events |= EVENT_PUBACK;
                    const nx_result_t health = feed(application);
                    if (health != NX_SUCCESS) {
                        event_failure(application, health);
                    }
                }
            }
            break;
        case NX_MQTT_EVENT_PUBLISH:
            if (event->qos == 1U &&
                event->topic_size == application->config.topic_bytes &&
                event->payload_size == application->expected_payload_bytes &&
                memcmp(event->topic, application->config.topic,
                       event->topic_size) == 0 &&
                memcmp(event->payload, application->policy.payload,
                       event->payload_size) == 0) {
                application->report.echoed = true;
                application->report.mqtt_events |= EVENT_PUBLISH;
            }
            break;
        default:
            event_failure(application, NX_ERROR_STATE);
            break;
    }
}
static nx_mqtt_call_t
mqtt_call(const example_gateway_application_t* application) {
    const nx_mqtt_call_t call = {application->attempt.deadline,
                                 application->config.mqtt_io_budget};
    return call;
}
static nx_result_t events(example_gateway_application_t* application,
                          uint32_t wanted) {
    while ((application->report.mqtt_events & wanted) != wanted) {
        nx_result_t result = turn(application, application->attempt.deadline);
        if (result != NX_SUCCESS) {
            return result;
        }
        result =
            nx_mqtt_process(application->config.mqtt, mqtt_call(application));
        if (result != NX_SUCCESS && result != NX_ERROR_BUSY) {
            return result;
        }
        if (application->event_failed) {
            return application->report.primary;
        }
        if ((application->report.mqtt_events & wanted) == wanted) {
            break;
        }
        result = wait_turn(application, application->attempt.deadline, true);
        if (result != NX_SUCCESS) {
            return result;
        }
    }
    return NX_SUCCESS;
}
static nx_result_t session(example_gateway_application_t* application) {
    const nx_time_us_t deadline = application->attempt.deadline;
    nx_result_t result = guard(application, deadline);
    if (result != NX_SUCCESS) {
        return result;
    }
    result = nx_lwip_tcp_start(application->config.tcp,
                               &application->config.tcp_config, deadline);
    if (result != NX_SUCCESS) {
        return result;
    }
    application->tcp_owned = true;
    for (;;) {
        result = turn(application, deadline);
        if (result != NX_SUCCESS) {
            return result;
        }
        result = nx_lwip_tcp_service(application->config.tcp);
        if (result != NX_SUCCESS && result != NX_ERROR_BUSY) {
            return result;
        }
        nx_lwip_tcp_diagnostic_t diagnostic = {0};
        result = nx_lwip_tcp_diagnostic(application->config.tcp, &diagnostic);
        if (result != NX_SUCCESS) {
            return result;
        }
        if (diagnostic.state == NX_LWIP_TCP_ESTABLISHED) {
            break;
        }
        if (diagnostic.state == NX_LWIP_TCP_FAILED) {
            return diagnostic.result == NX_SUCCESS ? NX_ERROR_IO
                                                   : diagnostic.result;
        }
        result = wait_turn(application, deadline, false);
        if (result != NX_SUCCESS) {
            return result;
        }
    }
    nx_tls_config_t tls = application->config.tls_config;
    tls.transport = nx_lwip_tcp_stream(application->config.tcp);
    if (tls.transport == NULL) {
        return NX_ERROR_STATE;
    }
    result = nx_tls_initialize(application->config.tls, &tls, deadline);
    if (result != NX_SUCCESS) {
        return result;
    }
    application->tls_owned = true;
    for (;;) {
        result = turn(application, deadline);
        if (result != NX_SUCCESS) {
            return result;
        }
        result = nx_tls_handshake_step(application->config.tls, deadline);
        if (result != NX_ERROR_BUSY) {
            break;
        }
        result = wait_turn(application, deadline, false);
        if (result != NX_SUCCESS) {
            return result;
        }
    }
    if (result != NX_SUCCESS) {
        return result;
    }
    application->report.authenticated = true;
    const nx_byte_stream_t* secured = nx_tls_stream(application->config.tls);
    if (secured == NULL) {
        return NX_ERROR_STATE;
    }
    const nx_mqtt_config_t config = {*secured, mqtt_event, application,
                                     mqtt_idle, application};
    result = nx_mqtt_init(application->config.mqtt, &config,
                          application->config.mqtt_memory);
    if (result != NX_SUCCESS) {
        return result;
    }
    application->mqtt_owned = true;
    result =
        nx_mqtt_connect(application->config.mqtt, &application->config.identity,
                        mqtt_call(application));
    if (result != NX_SUCCESS) {
        return result;
    }
    result = example_gateway_online(
        &application->policy,
        application->config.clock.read(application->config.clock.context));
    if (result != NX_SUCCESS) {
        return result;
    }
    result =
        nx_mqtt_subscribe(application->config.mqtt, application->config.topic,
                          application->config.topic_bytes, 1U,
                          &application->subscribed, mqtt_call(application));
    if (result == NX_SUCCESS) {
        result = events(application, EVENT_SUBACK);
    }
    if (result != NX_SUCCESS) {
        return result;
    }
    const nx_mqtt_publish_t publication = {application->config.topic,
                                           application->config.topic_bytes,
                                           application->policy.payload,
                                           application->policy.payload_size,
                                           1U,
                                           false};
    result = nx_mqtt_publish(application->config.mqtt, &publication,
                             &application->published, mqtt_call(application));
    if (result == NX_SUCCESS) {
        result = example_gateway_published(&application->policy,
                                           application->published);
    }
    if (result == NX_SUCCESS) {
        result = events(application, EVENT_PUBACK | EVENT_PUBLISH);
    }
    if (result != NX_SUCCESS) {
        return result;
    }
    result =
        nx_mqtt_unsubscribe(application->config.mqtt, application->config.topic,
                            application->config.topic_bytes,
                            &application->unsubscribed, mqtt_call(application));
    if (result == NX_SUCCESS) {
        result = events(application, EVENT_UNSUBACK);
    }
    return result == NX_SUCCESS ? nx_mqtt_disconnect(application->config.mqtt,
                                                     mqtt_call(application))
                                : result;
}
static nx_result_t cleanup_local(example_gateway_application_t* application,
                                 nx_time_us_t deadline) {
    bool graceful = application->report.primary == NX_SUCCESS &&
                    application->report.cleanup == NX_SUCCESS &&
                    application->report.delivered;
    nx_result_t first = NX_SUCCESS;
    nx_result_t result = NX_SUCCESS;
    if (application->mqtt_owned) {
        result = nx_mqtt_deinit(application->config.mqtt);
        if (result != NX_SUCCESS) {
            application->report.cleanup = result;
            return result;
        }
        application->mqtt_owned = false;
    }
    if (application->tls_owned) {
        if (graceful) {
            for (;;) {
                result = nx_tls_close_step(application->config.tls, deadline);
                if (result != NX_ERROR_BUSY) {
                    break;
                }
                result = wait_turn(application, deadline, true);
                if (result != NX_SUCCESS) {
                    break;
                }
            }
            if (result == NX_SUCCESS) {
                uint8_t discarded[32];
                for (;;) {
                    size_t bytes = 0U;
                    result =
                        nx_tls_try_read(application->config.tls, discarded,
                                        sizeof(discarded), &bytes, deadline);
                    if (result == NX_SUCCESS && bytes == 0U) {
                        break;
                    }
                    if (result != NX_SUCCESS && result != NX_ERROR_BUSY) {
                        break;
                    }
                    result = wait_turn(application, deadline, true);
                    if (result != NX_SUCCESS) {
                        break;
                    }
                }
            }
            if (result != NX_SUCCESS) {
                first = result;
                graceful = false;
            }
        }
        result = nx_tls_abort(application->config.tls);
        if (result != NX_SUCCESS) {
            application->report.cleanup = result;
            return result;
        }
        application->tls_owned = false;
    }
    if (application->tcp_owned) {
        if (graceful) {
            for (;;) {
                result = nx_lwip_tcp_close(application->config.tcp, deadline);
                if (result != NX_ERROR_BUSY) {
                    break;
                }
                result = wait_turn(application, deadline, false);
                if (result != NX_SUCCESS) {
                    break;
                }
            }
            if (result == NX_SUCCESS) {
                application->tcp_owned = false;
            } else {
                first = result;
                graceful = false;
            }
        }
        if (application->tcp_owned) {
            result = nx_lwip_tcp_abort(application->config.tcp);
            if (result != NX_SUCCESS) {
                application->report.cleanup = result;
                return result;
            }
            application->tcp_owned = false;
        }
    }
    application->report.graceful_close = graceful;
    application->report.cleanup = first;
    return first;
}
static nx_result_t watchdog_admit(example_gateway_application_t* application) {
    if (application->config.watchdog == NULL) {
        return NX_SUCCESS;
    }
    nx_watchdog_state_t state = {0};
    const nx_result_t result =
        nx_watchdog_port_state(application->config.watchdog, &state);
    if (result != NX_SUCCESS) {
        return result;
    }
    if (!state.enabled) {
        return NX_ERROR_STATE;
    }
    if (state.minimum_timeout_us == 0U) {
        return NX_ERROR_UNSUPPORTED;
    }
    const nx_time_us_t now =
        application->config.clock.read(application->config.clock.context);
    if (now >= application->config.policy.overall_deadline) {
        return NX_ERROR_TIMEOUT;
    }
    const nx_time_us_t remaining =
        application->config.policy.overall_deadline - now;
    if (application->config.cleanup_budget_us > UINT64_MAX - remaining ||
        remaining + application->config.cleanup_budget_us >
            state.minimum_timeout_us) {
        return NX_ERROR_INVALID;
    }
    return feed(application);
}
static bool retryable(nx_result_t result) {
    return result == NX_ERROR_IO || result == NX_ERROR_TIMEOUT ||
           result == NX_ERROR_EXHAUSTED;
}
static nx_time_us_t
cleanup_deadline(example_gateway_application_t* application) {
    const nx_time_us_t now =
        application->config.clock.read(application->config.clock.context);
    const nx_time_us_t result =
        nx_deadline_after(now, application->config.cleanup_budget_us);
    return result == NX_DEADLINE_NEVER ? NX_DEADLINE_NEVER - 1U : result;
}

nx_result_t example_gateway_run(example_gateway_application_t* application,
                                const example_gateway_config_t* config,
                                example_gateway_report_t* report) {
    if (!metadata_valid(application, config, report) ||
        !config_valid(application, config) ||
        !inputs_disjoint(application, config, report)) {
        return NX_ERROR_INVALID;
    }
    if (application->running || !local_released(application)) {
        return NX_ERROR_STATE;
    }
    const example_gateway_config_t copy = *config;
    const nx_time_us_t now = copy.clock.read(copy.clock.context);
    example_gateway_policy_t policy = {0};
    nx_result_t result =
        example_gateway_policy_init(&policy, &copy.policy, now);
    if (result != NX_SUCCESS) {
        return result;
    }
    result = example_gateway_queue(&policy, copy.payload, copy.payload_bytes);
    if (result != NX_SUCCESS) {
        return result;
    }
    memset(application, 0, sizeof(*application));
    application->config = copy;
    application->policy = policy;
    application->expected_payload_bytes = copy.payload_bytes;
    application->remaining_turns = copy.progress_turns;
    application->running = true;
    result = guard(application, copy.policy.overall_deadline);
    if (result == NX_SUCCESS) {
        result = watchdog_admit(application);
    }
    application->report.primary = result;
    while (result == NX_SUCCESS) {
        result = guard(application, copy.policy.overall_deadline);
        if (result != NX_SUCCESS || application->remaining_turns == 0U) {
            if (result == NX_SUCCESS) {
                result = NX_ERROR_EXHAUSTED;
            }
            application->report.primary = result;
            break;
        }
        result = example_gateway_begin(&application->policy,
                                       copy.clock.read(copy.clock.context),
                                       &application->attempt);
        if (result == NX_ERROR_BUSY) {
            result =
                wait_turn(application, copy.policy.overall_deadline, false);
            if (result != NX_SUCCESS) {
                application->report.primary = result;
            }
            continue;
        }
        if (result != NX_SUCCESS) {
            application->report.primary = result;
            break;
        }
        application->report.primary = NX_SUCCESS;
        application->report.mqtt_events = 0U;
        application->report.echoed = false;
        application->event_failed = false;
        application->subscribed = application->published =
            application->unsubscribed = 0U;
        result = session(application);
        application->report.primary = result;
        if (application->tls_owned) {
            nx_tls_diagnostic_t diagnostic = {0};
            if (nx_tls_diagnostic(application->config.tls, &diagnostic) ==
                NX_SUCCESS) {
                application->report.tls_before_cleanup = diagnostic;
                application->report.tls_snapshot_valid = true;
            }
        }
        if (result != NX_SUCCESS &&
            application->report.first_attempt_failure == NX_SUCCESS) {
            application->report.first_attempt_failure = result;
        }
        const bool again = result != NX_SUCCESS && retryable(result) &&
                           !application->report.delivered &&
                           application->remaining_turns != 0U &&
                           application->policy.attempts <
                               application->config.policy.attempts_limit;
        if (!again) {
            (void)example_gateway_stop(&application->policy);
        } else if (application->policy.state != EXAMPLE_GATEWAY_DRAINING) {
            (void)example_gateway_failed(&application->policy, result);
        }
        const nx_result_t cleaned =
            cleanup_local(application, cleanup_deadline(application));
        if (!local_released(application)) {
            result = cleaned;
            break;
        }
        if (application->policy.state == EXAMPLE_GATEWAY_DRAINING) {
            const nx_result_t observed =
                example_gateway_cleaned(&application->policy, NX_SUCCESS,
                                        copy.clock.read(copy.clock.context));
            if (observed != NX_SUCCESS) {
                application->report.cleanup = observed;
                result = observed;
                break;
            }
        }
        if (result == NX_SUCCESS) {
            result = cleaned;
            break;
        }
        if (!again) {
            break;
        }
        result = NX_SUCCESS;
    }
    if (local_released(application)) {
        (void)example_gateway_stop(&application->policy);
    }
    application->running = false;
    publish_report(application, report);
    return result;
}

nx_result_t example_gateway_cleanup(example_gateway_application_t* application,
                                    nx_time_us_t deadline,
                                    example_gateway_report_t* report) {
    if (application == NULL || report == NULL ||
        deadline == NX_DEADLINE_NEVER ||
        !disjoint(application, report, sizeof(*report))) {
        return NX_ERROR_INVALID;
    }
    if (application->running) {
        return NX_ERROR_BUSY;
    }
    if (application->config.clock.read == NULL) {
        return NX_ERROR_STATE;
    }
    application->running = true;
    const nx_result_t result = cleanup_local(application, deadline);
    if (local_released(application) &&
        application->policy.state == EXAMPLE_GATEWAY_DRAINING) {
        (void)example_gateway_cleaned(
            &application->policy, NX_SUCCESS,
            application->config.clock.read(application->config.clock.context));
    }
    application->running = false;
    publish_report(application, report);
    return result;
}
