/**
 * \file            main.c
 * \brief           Two producers share one explicit UART execution owner
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-10-11
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "fault_policy.h"
#include "nexus_config.h"
#include "quarantine_policy.h"

#include "backend_policy.h"
#include "bootstrap.h"
#include "lifecycle.h"
#include "nexus/arch/arch.h"
#include "nexus/arch/atomic.h"
#include "nexus/components/uart_owner.h"
#include "nexus_bindings.h"
#include "nexus_factory_strong.h"
#include "scheduler_return.h"
#if defined(NEXUS_BACKEND_NATIVE) ||                                           \
    (defined(NEXUS_OS_HOST_TICK) && NEXUS_OS_HOST_TICK)
#include "nexus/io/native/model.h"
#endif
#if defined(NEXUS_BACKEND_NATIVE)
#include "nexus/os/storage.h"
#include <stdio.h>
#elif EXAMPLE_KERNEL_BACKEND
#include "nexus/os/storage.h"
#if defined(NEXUS_BACKEND_THREADX)
#include "nexus/os/threadx.h"
#endif
#else
#include "nexus/os/baremetal.h"
#endif

#define PRODUCERS        2U
#define TASK_STACK_WORDS 256U

#if defined(NEXUS_BACKEND_NATIVE)
typedef nx_notify_t notification_t;
typedef nx_task_t application_task_t;
static _Alignas(16) uint8_t s_host_stacks[PRODUCERS + 1U][65536];
#elif EXAMPLE_KERNEL_BACKEND
typedef nx_notify_t notification_t;
typedef nx_task_t application_task_t;
static _Alignas(8)
    nx_os_stack_word_t s_mcu_stacks[PRODUCERS + 2U][TASK_STACK_WORDS];
#else
typedef nx_baremetal_notify_t notification_t;
#endif

/** \brief Stable producer request, descriptor and payload outlive settlement.
 */
typedef struct {
    nx_request_t request;
    nx_uart_owner_operation_t operation;
    nx_owner_ticket_t ticket;
    notification_t completed;
    nx_wait_port_t completion;
    uint32_t submitted;
    nx_result_t result;
} producer_t;

/** \brief The external application supplies every queue slot and OS object. */
typedef struct {
    nx_bus_owner_t owner;
    example_owner_recovery_t recovery;
    uint32_t recovery_stage;
    nx_owner_slot_t slots[PRODUCERS];
    nx_uart_owner_executor_t uart;
    notification_t owner_event;
    notification_t coordinator_event;
    nx_wait_port_t owner_wake;
    nx_wait_port_t coordinator_wake;
    nx_irq_wake_t irq_wake;
    producer_t producer[PRODUCERS];
    example_owner_phase_t phase;
    uint32_t owner_stop;
    uint32_t status;
    uint32_t scheduler_return_stage;
    example_fault_policy_t fault_policy;
    size_t reclaimed_notifications;
    example_owner_bootstrap_t bootstrap;
} application_t;
static application_t s_application;
#if !defined(NEXUS_BACKEND_BAREMETAL)
/** \brief Kernel-written controls remain outside every borrowed entry context.
 */
static struct {
    application_task_t owner;
    application_task_t producer[PRODUCERS];
    bool owner_retained;
    bool producer_retained[PRODUCERS];
#if EXAMPLE_KERNEL_BACKEND
    application_task_t coordinator;
    bool coordinator_retained;
#endif
} s_execution;
#endif
static const uint8_t s_payload[PRODUCERS][8] = {
    {'p', 'r', 'o', 'd', 'u', 'c', 'e', '0'},
    {'p', 'r', 'o', 'd', 'u', 'c', 'e', '1'}};

/** \brief Select only an already generated public UART interface. */
static const nx_uart_port_t* console(void) {
    return nx_factory_uart_ref(NX_UART_REF_CONSOLE);
}

/** \brief One CPU-local metadata guard; no OS call while interrupts masked. */
static uintptr_t enter(void* context) {
    (void)context;
    return nx_arch_irq_save().value;
}
static void leave(void* context, uintptr_t value) {
    (void)context;
    nx_arch_irq_restore((nx_arch_irq_state_t){(uint32_t)value});
}
static nx_time_us_t now(void* context) {
    (void)context;
    return nx_time_now_us();
}

/** \brief Publish platform fault stages while all retained storage stays live.
 */
static void recovery_fault(void* context, example_recovery_stage_t stage,
                           nx_result_t reason) {
    application_t* app = context;
    nx_atomic_u32_store_release(&app->recovery_stage, (uint32_t)stage);
    if (reason != NX_SUCCESS) {
        nx_atomic_u32_store_release(&app->status, (uint32_t)reason);
        nx_fault_observe(NX_FAULT_PLATFORM, reason, __FILE__, __LINE__);
    }
    (void)app->coordinator_wake.wake(app->coordinator_wake.context);
}

/** \brief Initialize exact per-object storage without hidden workers. */
static nx_result_t notification_init(notification_t* notification) {
#if !defined(NEXUS_BACKEND_BAREMETAL)
    return nx_notify_init(notification);
#else
    return nx_baremetal_notify_init(notification, now, NULL);
#endif
}
static nx_wait_port_t notification_port(notification_t* notification) {
#if !defined(NEXUS_BACKEND_BAREMETAL)
    return nx_notify_port(notification);
#else
    return nx_baremetal_notify_port(notification);
#endif
}
static nx_result_t notification_destroy(notification_t* notification) {
#if !defined(NEXUS_BACKEND_BAREMETAL)
    return nx_notify_destroy(notification);
#else
    (void)notification;
    return NX_SUCCESS;
#endif
}

/** \brief IRQ hint is bounded; the real provider latches completion first. */
static nx_result_t irq_hint(void* context) {
    nx_wait_port_t* wake = context;
    return wake->wake(wake->context);
}
static bool settled(void* context) {
    producer_t* producer = context;
    return nx_request_state(&producer->request) == NX_REQUEST_SETTLED;
}
#if !defined(NEXUS_BACKEND_BAREMETAL)
static bool started(void* context) {
    application_t* app = context;
    for (size_t i = 0; i < PRODUCERS; ++i) {
        if (nx_atomic_u32_load_acquire(&app->producer[i].submitted) == 0U) {
            return false;
        }
    }
    return true;
}
#endif
#if defined(NEXUS_BACKEND_NATIVE)
static bool active(void* context) {
    application_t* app = context;
    return nx_request_state(&app->producer[0].request) == NX_REQUEST_ACTIVE ||
           nx_request_state(&app->producer[1].request) == NX_REQUEST_ACTIVE;
}

#endif

/** \brief Publish one stable admission; rejection never borrows the payload. */
static void producer_submit(producer_t* producer) {
    producer->result =
        nx_request_prepare(&producer->request, NX_DEADLINE_NEVER);
    if (producer->result == NX_SUCCESS) {
        producer->result = nx_bus_owner_submit(
            &s_application.owner, &producer->request, &producer->operation,
            &producer->completion, &producer->ticket);
    }
    nx_atomic_u32_store_release(&producer->submitted, 1U);
    (void)s_application.coordinator_wake.wake(
        s_application.coordinator_wake.context);
}

#if !defined(NEXUS_BACKEND_BAREMETAL)
/** \brief Producers wait on predicates and never service the shared UART. */
static void producer_entry(void* context) {
    producer_t* producer = context;
    producer_submit(producer);
    if (producer->result == NX_SUCCESS) {
        producer->result = nx_wait_until(&producer->completion, settled,
                                         producer, NX_DEADLINE_NEVER);
    }
}

/** \brief Periodic progress covers coalesced or failed hints without a daemon.
 */
static void owner_entry(void* context) {
    application_t* app = context;
    for (;;) {
        (void)example_owner_recovery_progress(&app->recovery, nx_time_now_us());
        (void)app->coordinator_wake.wake(app->coordinator_wake.context);
        uint32_t sequence = app->owner_wake.arm(app->owner_wake.context);
        if (nx_atomic_u32_load_acquire(&app->owner_stop) != 0U &&
            nx_bus_owner_idle(&app->owner)) {
            return;
        }
        (void)app->owner_wake.wait(app->owner_wake.context, sequence,
                                   nx_deadline_after(nx_time_now_us(), 1000U));
    }
}
#endif

/** \brief Keep the execution owner running while observers finish their loans.
 */
static nx_result_t join_producers(void* context) {
#if defined(NEXUS_BACKEND_BAREMETAL)
    application_t* app = context;
#else
    (void)context;
#endif
    for (size_t i = 0; i < PRODUCERS; ++i) {
#if !defined(NEXUS_BACKEND_BAREMETAL)
        nx_result_t result = s_execution.producer_retained[i]
                                 ? nx_task_join_until(&s_execution.producer[i],
                                                      NX_DEADLINE_NEVER)
                                 : NX_SUCCESS;
        if (result == NX_SUCCESS) {
            s_execution.producer_retained[i] = false;
        }
#else
        producer_t* producer = &app->producer[i];
        while (producer->result == NX_SUCCESS && !settled(producer)) {
            (void)example_owner_recovery_progress(&app->recovery,
                                                  nx_time_now_us());
        }
        nx_result_t result = NX_SUCCESS;
#endif
        if (result != NX_SUCCESS) {
            return result;
        }
    }
    return NX_SUCCESS;
}
static nx_result_t join_owner(void* context) {
    application_t* app = context;
    nx_atomic_u32_store_release(&app->owner_stop, 1U);
    (void)app->owner_wake.wake(app->owner_wake.context);
#if !defined(NEXUS_BACKEND_BAREMETAL)
    nx_result_t result =
        s_execution.owner_retained
            ? nx_task_join_until(&s_execution.owner, NX_DEADLINE_NEVER)
            : NX_SUCCESS;
    if (result == NX_SUCCESS) {
        s_execution.owner_retained = false;
    }
    return result;
#else
    while (!nx_bus_owner_idle(&app->owner)) {
        (void)example_owner_recovery_progress(&app->recovery, nx_time_now_us());
    }
    return NX_SUCCESS;
#endif
}

/** \brief Hardware stop/detachment precede destruction of borrowed IRQ sink. */
static nx_result_t quiesce_publishers(void* context) {
    (void)context;
    nx_result_t result = nx_uart_port_attach_wake(console(), NULL, 0U);
    if (result != NX_SUCCESS) {
        return result;
    }
    return nx_uart_port_stop(console());
}

/** \brief Destroy only after publisher, owner and producer quiescence. */
static nx_result_t reclaim(void* context) {
    application_t* app = context;
    while (app->reclaimed_notifications < PRODUCERS + 2U) {
        size_t i = app->reclaimed_notifications;
        notification_t* notification =
            i < PRODUCERS    ? &app->producer[i].completed
            : i == PRODUCERS ? &app->coordinator_event
                             : &app->owner_event;
        nx_result_t result = notification_destroy(notification);
        if (result != NX_SUCCESS) {
            return result;
        }
        ++app->reclaimed_notifications;
    }
    return NX_SUCCESS;
}

/** \brief Native holds TX completion to exercise real active cancellation. */
static nx_result_t configure_model(void) {
#if defined(NEXUS_BACKEND_NATIVE) ||                                           \
    (defined(NEXUS_OS_HOST_TICK) && NEXUS_OS_HOST_TICK)
    static nx_uart_rx_event_t receive[8];
    static uint8_t wire[32];
    const nx_native_uart_config_t config = {
        NX_UART_RX_EVENTS, receive, 8U, wire, sizeof(wire), false};
    nx_result_t result = nx_native_clock_configure(false, 0U);
    if (result != NX_SUCCESS) {
        return result;
    }
    return nx_native_uart_model_configure(console(), &config);
#else
    return NX_SUCCESS;
#endif
}

static nx_result_t initialize(application_t* app) {
    nx_result_t result = configure_model();
    if (result != NX_SUCCESS) {
        return result;
    }
    result = notification_init(&app->owner_event);
    if (result != NX_SUCCESS) {
        return result;
    }
    ++app->bootstrap.notifications;
    result = notification_init(&app->coordinator_event);
    if (result != NX_SUCCESS) {
        return result;
    }
    ++app->bootstrap.notifications;
    app->owner_wake = notification_port(&app->owner_event);
    app->coordinator_wake = notification_port(&app->coordinator_event);
    for (size_t i = 0; i < PRODUCERS; ++i) {
        producer_t* producer = &app->producer[i];
        nx_request_initialize(&producer->request);
        producer->operation =
            (nx_uart_owner_operation_t){s_payload[i], sizeof(s_payload[i])};
        result = notification_init(&producer->completed);
        if (result != NX_SUCCESS) {
            return result;
        }
        ++app->bootstrap.notifications;
        producer->completion = notification_port(&producer->completed);
    }
    nx_owner_guard_port_t guard = {NULL, enter, leave};
    nx_owner_executor_port_t executor =
        nx_uart_owner_executor_port(&app->uart, console());
    result =
        nx_bus_owner_init(&app->owner, app->slots, PRODUCERS, guard, executor,
                          (nx_clock_t){now, NULL}, &app->owner_wake);
    if (result != NX_SUCCESS) {
        return result;
    }
    app->bootstrap.owner_initialized = true;
    /* Application escalation budget; the worker keeps draining after it
     * expires.
     */
    result = example_owner_recovery_initialize(
        &app->recovery, &app->owner, 100U, 100000U,
        (example_recovery_fault_port_t){app, recovery_fault});
    if (result != NX_SUCCESS) {
        return result;
    }
    app->irq_wake = (nx_irq_wake_t) {
        &app->owner_wake, irq_hint,
#if EXAMPLE_KERNEL_BACKEND
            true
#else
            false
#endif
    };
    return nx_uart_port_attach_wake(console(), &app->irq_wake,
#if defined(NEXUS_BACKEND_FREERTOS)
                                    configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY
#elif defined(NEXUS_BACKEND_THREADX)
                                    NEXUS_IRQ_SYSCALL_PRIORITY
#else
                                    0U
#endif
    );
}

/** \brief Stop platform clocks only after kernel-independent execution stops.
 */
static nx_result_t stop_platform(void* context) {
    (void)context;
    return nx_platform_stop();
}

/** \brief Retire actual workers according to their execution phase. */
static nx_result_t retire_startup_workers(void* context,
                                          example_owner_workers_t execution,
                                          size_t count) {
    application_t* app = context;
    (void)count;
#if defined(NEXUS_BACKEND_NATIVE)
    if (execution != EXAMPLE_WORKERS_NATIVE_RUNNING) {
        return NX_ERROR_CONTEXT;
    }
    nx_result_t result = join_producers(app);
    return result == NX_SUCCESS ? join_owner(app) : result;
#elif EXAMPLE_KERNEL_BACKEND
    (void)app;
    if (execution != EXAMPLE_WORKERS_KERNEL_UNSTARTED) {
        return NX_ERROR_CONTEXT;
    }
    /* These tasks have not executed their entries. No request can be borrowed,
     * and scheduler-not-started discard must not wait for entry completion. */
    application_task_t* tasks[PRODUCERS + 2U] = {
        &s_execution.owner, &s_execution.producer[0], &s_execution.producer[1],
        &s_execution.coordinator};
    bool* live[PRODUCERS + 2U] = {
        &s_execution.owner_retained, &s_execution.producer_retained[0],
        &s_execution.producer_retained[1], &s_execution.coordinator_retained};
    for (size_t i = PRODUCERS + 2U; i != 0U; --i) {
        if (*live[i - 1U]) {
            nx_result_t result =
                nx_task_discard_before_scheduler(tasks[i - 1U]);
            if (result != NX_SUCCESS) {
                return result;
            }
            *live[i - 1U] = false;
        }
    }
    return NX_SUCCESS;
#else
    (void)app;
    (void)execution;
    return NX_ERROR_CONTEXT;
#endif
}

/** \brief Reverse only notification creations recorded as successful. */
static nx_result_t destroy_startup_notification(void* context, size_t index) {
    application_t* app = context;
    if (index >= PRODUCERS + 2U) {
        return NX_ERROR_INVALID;
    }
    notification_t* notification = index == 0U ? &app->owner_event
                                   : index == 1U
                                       ? &app->coordinator_event
                                       : &app->producer[index - 2U].completed;
    return notification_destroy(notification);
}

#if EXAMPLE_KERNEL_BACKEND
static _Noreturn void scheduler_failure_sink(void* context);
#endif

/** \brief Record a boot failure and roll back only established effects. */
static int startup_failed(application_t* app, int code) {
    example_owner_bootstrap_port_t port = {
        app, retire_startup_workers, quiesce_publishers,
        destroy_startup_notification, stop_platform};
    nx_result_t result =
        example_owner_bootstrap_abort(&app->bootstrap, &app->owner, port);
    nx_atomic_u32_store_release(
        &app->status, (uint32_t)(result == NX_SUCCESS ? NX_ERROR_IO : result));
    if (result != NX_SUCCESS) {
        /* A product supplies recovery or reset. Keep callbacks, IRQ sinks,
         * caller stacks and any quarantined bytes alive without claiming stop.
         */
#if EXAMPLE_KERNEL_BACKEND
        scheduler_failure_sink(app);
#else
        for (;;) {
        }
#endif
    }
    return code;
}

/** \brief Application policy retains all static storage on any drain failure.
 */
static nx_result_t shutdown_application(application_t* app) {
    const example_owner_shutdown_port_t port = {app, join_producers, join_owner,
                                                quiesce_publishers, reclaim};
    nx_result_t result = example_owner_shutdown(&app->phase, &app->owner, port);
    if (result != NX_SUCCESS) {
        return result;
    }
    for (size_t i = 0; i < PRODUCERS; ++i) {
        nx_result_t operation_result;
        size_t transferred;
        result = nx_request_result(&app->producer[i].request, &operation_result,
                                   &transferred);
        if (result != NX_SUCCESS || (operation_result != NX_SUCCESS &&
                                     operation_result != NX_ERROR_CANCELLED)) {
            return NX_ERROR_IO;
        }
    }
    return example_owner_finish(&app->phase, EXAMPLE_KEEP_PLATFORM_RUNNING,
                                stop_platform, app);
}

#if EXAMPLE_KERNEL_BACKEND
/** \brief This external task owns the entire product stop policy. */
static void coordinator_entry(void* context) {
    application_t* app = context;
    nx_result_t result =
        nx_wait_until(&app->coordinator_wake, started, app, NX_DEADLINE_NEVER);
    if (result == NX_SUCCESS) {
        result = shutdown_application(app);
    }
    nx_atomic_u32_store_release(&app->status, (uint32_t)result);
    /* An error retains global request/bytes/sinks. A product chooses recovery;
     * no example silently frees storage or resets equipment. */
}
#endif

#if EXAMPLE_KERNEL_BACKEND
/** \brief Retain all startup effects through a product-owned terminal hook. */
static _Noreturn void scheduler_failure_sink(void* context) {
    (void)context;
    nx_fault_observe(NX_FAULT_PLATFORM, NX_ERROR_IO, __FILE__, __LINE__);
    /* Kernel startup may have configured Tick, priorities and task contexts.
     * Only a product reset or proved whole-port teardown may reclaim them.
     */
    (void)nx_arch_irq_save();
#if defined(NEXUS_BACKEND_FREERTOS)
    nx_freertos_assert_failed(__FILE__, __LINE__);
#else
    nx_threadx_fail_stop(NX_ERROR_IO);
#endif
    for (;;) {
    }
}
#endif

#if EXAMPLE_KERNEL_BACKEND
/** \brief A failed void bootstrap rolls back before retaining terminal state.
 */
static _Noreturn void bootstrap_failed(application_t* app, int code) {
    (void)startup_failed(app, code);
    scheduler_failure_sink(app);
}

/** \brief Construct all kernel objects after the common cold-entry checks. */
static void kernel_startup(void* context) {
    application_t* app = context;
    app->bootstrap.execution = EXAMPLE_WORKERS_KERNEL_UNSTARTED;
    app->bootstrap.publishers_live = true;
    if (initialize(app) != NX_SUCCESS) {
        bootstrap_failed(app, 1);
    }
    /* A failed start may retain a resumed control. Record the candidate before
     * crossing the kernel boundary; only proved discard releases its effects.
     */
    s_execution.owner_retained = true;
    ++app->bootstrap.workers;
    if (nx_task_start(&s_execution.owner, "owner", s_mcu_stacks[0],
                      sizeof(s_mcu_stacks[0]), 2U, owner_entry,
                      app) != NX_SUCCESS) {
        bootstrap_failed(app, 5);
    }
    for (size_t i = 0U; i < PRODUCERS; ++i) {
        s_execution.producer_retained[i] = true;
        ++app->bootstrap.workers;
        if (nx_task_start(&s_execution.producer[i], "producer",
                          s_mcu_stacks[i + 1U], sizeof(s_mcu_stacks[i + 1U]),
                          3U, producer_entry,
                          &app->producer[i]) != NX_SUCCESS) {
            bootstrap_failed(app, 6);
        }
    }
    s_execution.coordinator_retained = true;
    ++app->bootstrap.workers;
    if (nx_task_start(&s_execution.coordinator, "stop",
                      s_mcu_stacks[PRODUCERS + 1U],
                      sizeof(s_mcu_stacks[PRODUCERS + 1U]), 1U,
                      coordinator_entry, app) != NX_SUCCESS) {
        bootstrap_failed(app, 7);
    }
    app->bootstrap.scheduler_started = true;
}
#endif

/** \brief No board-specific output action is inferred by this reference app. */
static nx_result_t unbound_safe_outputs(void* context, nx_fault_domain_t domain,
                                        nx_result_t reason) {
    (void)context;
    (void)domain;
    (void)reason;
    return NX_ERROR_UNSUPPORTED;
}

/** \brief Product composition owns one stable first-fault record. */
example_fault_policy_t* example_owner_fault_policy(void) {
    return &s_application.fault_policy;
}

int main(void) {
    if (example_fault_policy_initialize(
            &s_application.fault_policy,
            (example_safe_output_port_t){NULL, unbound_safe_outputs}) !=
        NX_SUCCESS) {
        return 1;
    }
    nx_platform_start_result_t startup = nx_platform_start();
    s_application.bootstrap.platform_live =
        startup.primary == NX_SUCCESS || startup.remaining_effects != 0U;
    if (startup.primary != NX_SUCCESS || startup.cleanup != NX_SUCCESS ||
        startup.remaining_effects != 0U) {
        return startup_failed(&s_application, 1);
    }
#if !EXAMPLE_KERNEL_BACKEND
    s_application.bootstrap.publishers_live = true;
    if (initialize(&s_application) != NX_SUCCESS) {
        return startup_failed(&s_application, 1);
    }
#endif
#if defined(NEXUS_BACKEND_NATIVE)
    s_application.bootstrap.execution = EXAMPLE_WORKERS_NATIVE_RUNNING;
    if (nx_task_start(&s_execution.owner, "application", s_host_stacks[0],
                      sizeof(s_host_stacks[0]), 0U, owner_entry,
                      &s_application) != NX_SUCCESS) {
        return startup_failed(&s_application, 2);
    }
    s_execution.owner_retained = true;
    ++s_application.bootstrap.workers;
    for (size_t i = 0; i < PRODUCERS; ++i) {
        if (nx_task_start(&s_execution.producer[i], "application",
                          s_host_stacks[i + 1U], sizeof(s_host_stacks[i + 1U]),
                          0U, producer_entry,
                          &s_application.producer[i]) != NX_SUCCESS) {
            return startup_failed(&s_application, 3);
        }
        s_execution.producer_retained[i] = true;
        ++s_application.bootstrap.workers;
    }
    nx_time_us_t deadline = nx_deadline_after(nx_time_now_us(), 1000000U);
    if (nx_wait_until(&s_application.coordinator_wake, started, &s_application,
                      deadline) != NX_SUCCESS ||
        nx_wait_until(&s_application.coordinator_wake, active, &s_application,
                      deadline) != NX_SUCCESS) {
        return startup_failed(&s_application, 4);
    }
    nx_result_t result = shutdown_application(&s_application);
    if (result != NX_SUCCESS) {
        nx_atomic_u32_store_release(&s_application.status, (uint32_t)result);
        /* Normal shutdown has its own completed milestones. A failed drain
         * cannot reuse the startup record to destroy already reclaimed objects.
         */
        for (;;) {
        }
    }
    puts("UARTOwner: two producers, active cancel/drain, joined publishers");
    return 0;
#elif EXAMPLE_KERNEL_BACKEND
    (void)nx_os_run(kernel_startup, &s_application);
    example_owner_scheduler_returned(
        &s_application.bootstrap, &s_application.status,
        &s_application.scheduler_return_stage,
        (example_owner_fail_stop_port_t){NULL, scheduler_failure_sink});
#else
    for (size_t i = 0; i < PRODUCERS; ++i) {
        producer_submit(&s_application.producer[i]);
    }
    (void)example_owner_recovery_progress(&s_application.recovery,
                                          nx_time_now_us());
    return shutdown_application(&s_application) == NX_SUCCESS ? 0 : 9;
#endif
}
