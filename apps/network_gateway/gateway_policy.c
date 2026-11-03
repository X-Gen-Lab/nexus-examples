/**
 * \file            gateway_policy.c
 * \brief           Bounded retry admission and retained product telemetry
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-02
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "gateway_policy.h"
#include <string.h>

static bool overlaps(const void* a, size_t a_bytes, const void* b,
                     size_t b_bytes) {
    const uintptr_t first = (uintptr_t)a;
    const uintptr_t second = (uintptr_t)b;
    return first <= second ? second - first < a_bytes
                           : first - second < b_bytes;
}

static bool initialized(const example_gateway_policy_t* policy) {
    return policy != NULL && policy->state >= EXAMPLE_GATEWAY_READY &&
           policy->state <= EXAMPLE_GATEWAY_FAILED;
}

static nx_time_us_t bounded_after(const example_gateway_policy_t* policy,
                                  nx_time_us_t now, nx_time_us_t duration) {
    const nx_time_us_t remaining = policy->config.overall_deadline - now;
    return duration >= remaining ? policy->config.overall_deadline
                                 : now + duration;
}

nx_result_t
example_gateway_policy_init(example_gateway_policy_t* policy,
                            const example_gateway_policy_config_t* config,
                            nx_time_us_t now) {
    if (policy == NULL || config == NULL || now == UINT64_MAX ||
        overlaps(policy, sizeof(*policy), config, sizeof(*config)) ||
        config->overall_deadline == UINT64_MAX ||
        config->overall_deadline <= now || config->attempt_budget_us == 0U ||
        config->attempt_budget_us == UINT64_MAX ||
        config->retry_delay_us == 0U || config->retry_delay_us == UINT64_MAX ||
        config->attempts_limit == 0U ||
        config->attempts_limit > EXAMPLE_GATEWAY_MAX_ATTEMPTS) {
        return NX_ERROR_INVALID;
    }
    if (policy->state != EXAMPLE_GATEWAY_COLD &&
        policy->state != EXAMPLE_GATEWAY_STOPPED) {
        return NX_ERROR_STATE;
    }
    if (policy->state == EXAMPLE_GATEWAY_STOPPED && now < policy->last_time) {
        return NX_ERROR_STATE;
    }
    const example_gateway_policy_config_t copy = *config;
    memset(policy, 0, sizeof(*policy));
    policy->config = copy;
    policy->state = EXAMPLE_GATEWAY_READY;
    policy->last_time = now;
    policy->next_attempt = now;
    return NX_SUCCESS;
}

nx_result_t example_gateway_begin(example_gateway_policy_t* policy,
                                  nx_time_us_t now,
                                  example_gateway_attempt_t* attempt) {
    if (!initialized(policy) || attempt == NULL || now == UINT64_MAX ||
        overlaps(policy, sizeof(*policy), attempt, sizeof(*attempt))) {
        return NX_ERROR_INVALID;
    }
    if (policy->state != EXAMPLE_GATEWAY_READY &&
        policy->state != EXAMPLE_GATEWAY_BACKOFF) {
        if (policy->state == EXAMPLE_GATEWAY_FAILED &&
            policy->fault == NX_ERROR_TIMEOUT) {
            return NX_ERROR_TIMEOUT;
        }
        if (policy->state == EXAMPLE_GATEWAY_FAILED &&
            policy->fault == NX_ERROR_EXHAUSTED) {
            return NX_ERROR_EXHAUSTED;
        }
        return NX_ERROR_STATE;
    }
    if (now < policy->last_time) {
        policy->state = EXAMPLE_GATEWAY_FAILED;
        policy->fault = NX_ERROR_STATE;
        return NX_ERROR_STATE;
    }
    policy->last_time = now;
    if (now >= policy->config.overall_deadline) {
        policy->state = EXAMPLE_GATEWAY_FAILED;
        policy->fault = NX_ERROR_TIMEOUT;
        return NX_ERROR_TIMEOUT;
    }
    if (policy->attempts >= policy->config.attempts_limit) {
        policy->state = EXAMPLE_GATEWAY_FAILED;
        policy->fault = NX_ERROR_EXHAUSTED;
        return NX_ERROR_EXHAUSTED;
    }
    if (now < policy->next_attempt) {
        return NX_ERROR_BUSY;
    }
    policy->attempt_deadline =
        bounded_after(policy, now, policy->config.attempt_budget_us);
    ++policy->attempts;
    policy->packet_id = 0U;
    policy->fault = NX_SUCCESS;
    policy->state = EXAMPLE_GATEWAY_CONNECTING;
    const example_gateway_attempt_t value = {policy->attempt_deadline,
                                             policy->attempts};
    *attempt = value;
    return NX_SUCCESS;
}

nx_result_t example_gateway_online(example_gateway_policy_t* policy,
                                   nx_time_us_t now) {
    if (!initialized(policy) || now == UINT64_MAX) {
        return NX_ERROR_INVALID;
    }
    if (policy->state != EXAMPLE_GATEWAY_CONNECTING) {
        return NX_ERROR_STATE;
    }
    if (now < policy->last_time) {
        policy->fault = NX_ERROR_STATE;
        policy->stop_requested = true;
        policy->state = EXAMPLE_GATEWAY_DRAINING;
        return NX_ERROR_STATE;
    }
    policy->last_time = now;
    if (now >= policy->attempt_deadline) {
        policy->fault = NX_ERROR_TIMEOUT;
        policy->state = EXAMPLE_GATEWAY_DRAINING;
        return NX_ERROR_TIMEOUT;
    }
    policy->state = EXAMPLE_GATEWAY_ONLINE;
    return NX_SUCCESS;
}

nx_result_t example_gateway_queue(example_gateway_policy_t* policy,
                                  const uint8_t* payload, size_t bytes) {
    if (!initialized(policy) || payload == NULL || bytes == 0U ||
        bytes > EXAMPLE_GATEWAY_PAYLOAD_BYTES ||
        overlaps(policy, sizeof(*policy), payload, bytes)) {
        return NX_ERROR_INVALID;
    }
    if (policy->state == EXAMPLE_GATEWAY_DRAINING ||
        policy->state == EXAMPLE_GATEWAY_STOPPED ||
        policy->state == EXAMPLE_GATEWAY_FAILED) {
        return NX_ERROR_STATE;
    }
    if (policy->payload_size != 0U) {
        return NX_ERROR_BUSY;
    }
    memcpy(policy->payload, payload, bytes);
    policy->payload_size = bytes;
    return NX_SUCCESS;
}

nx_result_t example_gateway_published(example_gateway_policy_t* policy,
                                      uint16_t packet_id) {
    if (!initialized(policy) || packet_id == 0U) {
        return NX_ERROR_INVALID;
    }
    if (policy->state != EXAMPLE_GATEWAY_ONLINE || policy->payload_size == 0U ||
        policy->packet_id != 0U) {
        return NX_ERROR_STATE;
    }
    policy->packet_id = packet_id;
    return NX_SUCCESS;
}

nx_result_t example_gateway_ack(example_gateway_policy_t* policy,
                                uint32_t session, uint16_t packet_id) {
    if (!initialized(policy) || session == 0U || packet_id == 0U) {
        return NX_ERROR_INVALID;
    }
    if (policy->state != EXAMPLE_GATEWAY_ONLINE ||
        session != policy->attempts || policy->payload_size == 0U ||
        policy->packet_id != packet_id) {
        return NX_ERROR_STATE;
    }
    policy->payload_size = 0U;
    policy->packet_id = 0U;
    return NX_SUCCESS;
}

nx_result_t example_gateway_failed(example_gateway_policy_t* policy,
                                   nx_result_t reason) {
    if (!initialized(policy) || reason == NX_SUCCESS ||
        reason == NX_ERROR_BUSY || reason > NX_ERROR_CONTEXT) {
        return NX_ERROR_INVALID;
    }
    if (policy->state != EXAMPLE_GATEWAY_CONNECTING &&
        policy->state != EXAMPLE_GATEWAY_ONLINE) {
        return NX_ERROR_STATE;
    }
    policy->fault = reason;
    policy->state = EXAMPLE_GATEWAY_DRAINING;
    return NX_SUCCESS;
}

nx_result_t example_gateway_cleaned(example_gateway_policy_t* policy,
                                    nx_result_t cleanup, nx_time_us_t now) {
    if (!initialized(policy) || now == UINT64_MAX) {
        return NX_ERROR_INVALID;
    }
    if (policy->state != EXAMPLE_GATEWAY_DRAINING) {
        return NX_ERROR_STATE;
    }
    const bool reversed = now < policy->last_time;
    if (reversed) {
        policy->fault = NX_ERROR_STATE;
        policy->stop_requested = true;
    } else {
        policy->last_time = now;
    }
    if (cleanup != NX_SUCCESS) {
        return cleanup;
    }
    /* Metadata release follows actual joined cleanup, even after clock loss.
     * A clock failure never authorizes a replacement session or new epoch. */
    policy->packet_id = 0U;
    if (reversed) {
        policy->state = EXAMPLE_GATEWAY_FAILED;
        return NX_ERROR_STATE;
    }
    if (policy->stop_requested) {
        policy->state = EXAMPLE_GATEWAY_STOPPED;
    } else if (now >= policy->config.overall_deadline) {
        policy->state = EXAMPLE_GATEWAY_FAILED;
        policy->fault = NX_ERROR_TIMEOUT;
    } else {
        policy->state = EXAMPLE_GATEWAY_BACKOFF;
        policy->next_attempt =
            bounded_after(policy, now, policy->config.retry_delay_us);
    }
    return NX_SUCCESS;
}

nx_result_t example_gateway_stop(example_gateway_policy_t* policy) {
    if (!initialized(policy)) {
        return NX_ERROR_INVALID;
    }
    policy->stop_requested = true;
    if (policy->state == EXAMPLE_GATEWAY_CONNECTING ||
        policy->state == EXAMPLE_GATEWAY_ONLINE ||
        policy->state == EXAMPLE_GATEWAY_DRAINING) {
        policy->state = EXAMPLE_GATEWAY_DRAINING;
    } else {
        policy->state = EXAMPLE_GATEWAY_STOPPED;
    }
    return NX_SUCCESS;
}
