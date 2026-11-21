/**
 * \file            industrial_clock.c
 * \brief           Two-task model clock for the installed Native consumer test
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-18
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "nexus/core/time.h"
#include "nexus/io/native/model.h"
#include "nexus/os/storage.h"
#include <pthread.h>

nx_wait_port_t __real_nx_notify_port(nx_notify_t* notification);

typedef struct {
    void* context;
    uint32_t sequence;
    nx_time_us_t deadline;
} model_wait_t;

static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_changed = PTHREAD_COND_INITIALIZER;
static model_wait_t* s_waits[2];

nx_result_t __wrap_example_industrial_model_clock(void) {
    return nx_native_clock_configure(true, 0U);
}

static uint32_t sequence(void* context) {
    return __real_nx_notify_port(context).arm(context);
}

/** \brief Advance to the first timer only when both real tasks are asleep. */
static void advance_idle_clock(nx_time_us_t now) {
    const model_wait_t* first = s_waits[0];
    const model_wait_t* second = s_waits[1];
    if (first == NULL || second == NULL ||
        sequence(first->context) != first->sequence ||
        sequence(second->context) != second->sequence) {
        return;
    }
    nx_time_us_t next =
        first->deadline < second->deadline ? first->deadline : second->deadline;
    if (next != NX_DEADLINE_NEVER && next > now) {
        (void)nx_native_clock_advance(next - now);
        pthread_cond_broadcast(&s_changed);
    }
}

static nx_result_t model_wait(void* context, uint32_t armed,
                              nx_time_us_t deadline) {
    model_wait_t wait = {context, armed, deadline};
    pthread_mutex_lock(&s_lock);
    size_t slot = s_waits[0] == NULL ? 0U : 1U;
    if (s_waits[slot] != NULL) {
        pthread_mutex_unlock(&s_lock);
        return NX_ERROR_BUSY;
    }
    s_waits[slot] = &wait;
    nx_result_t result = NX_SUCCESS;
    for (;;) {
        nx_time_us_t now = nx_time_now_us();
        if (nx_deadline_expired(deadline, now)) {
            result = NX_ERROR_TIMEOUT;
            break;
        }
        if (sequence(context) != armed) {
            break;
        }
        advance_idle_clock(now);
        if (!nx_deadline_expired(deadline, nx_time_now_us())) {
            pthread_cond_wait(&s_changed, &s_lock);
        }
    }
    s_waits[slot] = NULL;
    pthread_mutex_unlock(&s_lock);
    return result;
}

static nx_result_t model_wake(void* context) {
    pthread_mutex_lock(&s_lock);
    nx_result_t result = __real_nx_notify_port(context).wake(context);
    pthread_cond_broadcast(&s_changed);
    pthread_mutex_unlock(&s_lock);
    return result;
}

nx_wait_port_t __wrap_nx_notify_port(nx_notify_t* notification) {
    nx_wait_port_t port = __real_nx_notify_port(notification);
    port.wait = model_wait;
    port.wake = model_wake;
    return port;
}
