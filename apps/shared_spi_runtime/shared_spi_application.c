/**
 * \file            shared_spi_application.c
 * \brief           Explicit producer tasks share two SPI endpoints safely
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-10-24
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "shared_spi_application.h"
#include "nexus/arch/arch.h"
#include "nexus/arch/atomic.h"
#include "nexus/components/spi_owner.h"
#include "nexus/os/storage.h"
#include "nexus_bindings.h"
#include "nexus_config.h"
#include "nexus_factory_strong.h"
#include <string.h>
#if defined(NEXUS_BACKEND_NATIVE)
#include "nexus/io/native/model.h"
static uint8_t s_registers[2][2] = {{0x31U, 0U}, {0x72U, 0U}};
#endif
#if defined(NEXUS_BACKEND_FREERTOS) || defined(EXAMPLE_FREERTOS_HOST)
typedef nx_task_t product_task_t;
#define PRODUCT_STACK_WORDS configMINIMAL_STACK_SIZE
static _Alignas(16) nx_os_stack_word_t s_stacks[3][PRODUCT_STACK_WORDS];
static product_task_t s_executor_task;
#else
typedef nx_task_t product_task_t;
static _Alignas(16) uint8_t s_stacks[2][65536];
#endif

/** \brief Each producer owns persistent descriptor and payload storage. */
typedef struct {
    nx_request_t request;
    nx_spi_owner_operation_t operation;
    nx_owner_ticket_t ticket;
    uint8_t tx[2];
    uint8_t rx[2];
    uint32_t submitted;
    nx_result_t admission;
    bool started;
} product_producer_t;

static nx_bus_owner_t s_owner;
static nx_owner_slot_t s_slots[2];
static nx_spi_owner_executor_t s_executor;
static product_producer_t s_producers[2];
static product_task_t s_producer_tasks[2];
static nx_request_t s_rejected_request;
static nx_owner_ticket_t s_rejected_ticket;
static example_spi_report_t s_report;
static int s_result;

/** \brief Restore the exact incoming mask after each short metadata guard. */
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

/** \brief Explicit cooperative progress; waiting never services the bus. */
static void pause_task(void) {
    (void)nx_sleep_until(nx_deadline_after(nx_os_now_us(), 1000U));
}

/** \brief Publish one admission and retain payload until final settlement. */
static void producer(void* context) {
    product_producer_t* item = context;
    item->admission = nx_request_prepare(
        &item->request, nx_deadline_after(nx_time_now_us(), 100000U));
    if (item->admission == NX_SUCCESS) {
        item->admission = nx_bus_owner_submit(
            &s_owner, &item->request, &item->operation, NULL, &item->ticket);
    }
    nx_atomic_u32_store_release(&item->submitted, 1U);
    if (item->admission == NX_SUCCESS) {
        while (nx_request_state(&item->request) != NX_REQUEST_SETTLED) {
            pause_task();
        }
    }
}

/** \brief Start an explicitly stored producer using the selected backend. */
static nx_result_t start_producer(size_t index) {
    product_producer_t* item = &s_producers[index];
    nx_atomic_u32_store_release(&item->submitted, 0U);
    uint32_t priority = NX_OS_PRIORITY_LEVELS > 3U ? 3U : 0U;
    nx_result_t result =
        nx_task_start(&s_producer_tasks[index], "spi-producer", s_stacks[index],
                      sizeof(s_stacks[index]), priority, producer, item);
    item->started = result == NX_SUCCESS;
    return result;
}

/** \brief A successful join is the final access to producer task storage. */
static nx_result_t join_producer(size_t index) {
    return nx_task_join_until(&s_producer_tasks[index], NX_DEADLINE_NEVER);
}

/** \brief Complete one real round, including full-queue rejection or cancel. */
static int run_round(bool cancel_second) {
    int result = 0;
    for (size_t index = 0U; index < 2U; ++index) {
        if (start_producer(index) != NX_SUCCESS) {
            result = 1;
            break;
        }
    }
    for (size_t index = 0U; index < 2U; ++index) {
        product_producer_t* item = &s_producers[index];
        while (item->started &&
               nx_atomic_u32_load_acquire(&item->submitted) == 0U) {
            pause_task();
        }
        if (item->started && item->admission != NX_SUCCESS) {
            result = 2;
        }
    }
    if (result == 0) {
        nx_result_t rejected =
            nx_request_prepare(&s_rejected_request, NX_DEADLINE_NEVER);
        if (rejected == NX_SUCCESS) {
            rejected = nx_bus_owner_submit(&s_owner, &s_rejected_request,
                                           &s_producers[0].operation, NULL,
                                           &s_rejected_ticket);
        }
        if (rejected != NX_ERROR_EXHAUSTED ||
            nx_request_state(&s_rejected_request) != NX_REQUEST_READY) {
            result = 3;
        } else {
            ++s_report.rejected;
        }
        if (cancel_second &&
            nx_bus_owner_cancel(&s_owner, s_producers[1].ticket) !=
                NX_SUCCESS) {
            result = 4;
        }
    }
    if (result != 0) {
        nx_bus_owner_stop(&s_owner);
    }
    /* This is the only bus executor. Stop and timeout do not revoke loans. */
    while (!nx_bus_owner_idle(&s_owner)) {
        (void)nx_bus_owner_service(&s_owner);
        pause_task();
    }
    for (size_t index = 0U; index < 2U; ++index) {
        product_producer_t* item = &s_producers[index];
        if (!item->started) {
            continue;
        }
        if (join_producer(index) != NX_SUCCESS) {
            return 5;
        }
        item->started = false;
        if (item->admission == NX_SUCCESS) {
            nx_result_t transfer;
            size_t count;
            if (nx_request_result(&item->request, &transfer, &count) !=
                NX_SUCCESS) {
                result = 6;
            } else if (transfer == NX_SUCCESS && count == 2U) {
#if defined(NEXUS_BACKEND_NATIVE)
                if (item->rx[1] != s_registers[index][0]) {
                    result = 9;
                }
#endif
                ++s_report.completed;
            } else if (cancel_second && index == 1U &&
                       transfer == NX_ERROR_CANCELLED && count == 0U) {
                ++s_report.cancelled;
            } else {
                result = 7;
            }
        }
    }
    return result;
}

/** \brief The application owns the executor and joins every producer. */
static void coordinator(void* context) {
    (void)context;
    s_result = run_round(false);
    if (s_result == 0) {
        s_result = run_round(true);
    }
    nx_bus_owner_stop(&s_owner);
    while (!nx_bus_owner_idle(&s_owner)) {
        (void)nx_bus_owner_service(&s_owner);
        pause_task();
    }
    s_report.joined = !s_producers[0].started && !s_producers[1].started;
#if defined(NEXUS_BACKEND_FREERTOS)
    /* A live MCU scheduler retains clocks and Tick. Joining producers does not
     * establish a scheduler-stop contract. This task remains a static keeper.
     */
    s_report.platform_retained = true;
    if (!s_report.joined) {
        s_result = 8;
    }
    for (;;) {
        pause_task();
    }
#else
    s_report.stopped = s_report.joined && nx_platform_stop() == NX_SUCCESS;
    if (!s_report.stopped) {
        s_result = 8;
    }
#endif
#if defined(EXAMPLE_FREERTOS_HOST)
    vTaskEndScheduler();
#endif
}

#if defined(NEXUS_BACKEND_FREERTOS)
/** \brief This MCU composition has already prepared its static task storage. */
static void kernel_startup(void* context) {
    (void)context;
}
#endif

/** \brief Static application state remains valid even on retained failure. */
int example_shared_spi_run(example_spi_report_t* report) {
    if (report == NULL) {
        return 1;
    }
    nx_platform_start_result_t started = nx_platform_start();
    if (started.primary != NX_SUCCESS || started.cleanup != NX_SUCCESS ||
        started.remaining_effects != 0U) {
        return 2;
    }
    memset(&s_report, 0, sizeof(s_report));
    const nx_spi_endpoint_t* endpoints[2] = {
        nx_factory_spi_device_ref(NX_SPI_DEVICE_REF_SENSOR0),
        nx_factory_spi_device_ref(NX_SPI_DEVICE_REF_SENSOR1)};
    const nx_spi_port_t* port = nx_factory_spi_ref(NX_SPI_REF_SPI0);
    nx_owner_executor_port_t executor =
        nx_spi_owner_executor_port(&s_executor, port);
    nx_owner_guard_port_t guard = {NULL, enter, leave};
    nx_clock_t clock = {.read = now, .context = NULL};
    if (nx_bus_owner_init(&s_owner, s_slots, 2U, guard, executor, clock,
                          NULL) != NX_SUCCESS) {
        (void)nx_platform_stop();
        return 3;
    }
    for (size_t index = 0U; index < 2U; ++index) {
        product_producer_t* item = &s_producers[index];
        nx_request_initialize(&item->request);
        item->tx[0] = 0x80U;
        item->tx[1] = (uint8_t)index;
        item->operation = (nx_spi_owner_operation_t){
            endpoints[index], item->tx, item->rx, sizeof(item->tx)};
    }
#if defined(NEXUS_BACKEND_NATIVE)
    for (size_t index = 0U; index < 2U; ++index) {
        if (nx_native_spi_model_configure(endpoints[index], s_registers[index],
                                          sizeof(s_registers[index])) !=
            NX_SUCCESS) {
            (void)nx_platform_stop();
            return 5;
        }
    }
#endif
    nx_request_initialize(&s_rejected_request);
#if defined(NEXUS_BACKEND_FREERTOS) || defined(EXAMPLE_FREERTOS_HOST)
    if (nx_task_start(&s_executor_task, "spi-owner", s_stacks[2],
                      sizeof(s_stacks[0]), 2U, coordinator,
                      NULL) != NX_SUCCESS) {
        (void)nx_platform_stop();
        return 4;
    }
#if defined(EXAMPLE_FREERTOS_HOST)
    /* Only the reviewed POSIX test port supports returning scheduler teardown.
     */
    vTaskStartScheduler();
#else
    (void)nx_os_run(kernel_startup, NULL);
#endif
#if defined(NEXUS_BACKEND_FREERTOS)
    /* Unexpected return is not proof of scheduler teardown; retain all state.
     */
    s_result = 10;
    s_report.platform_retained = true;
    for (;;) {
    }
#endif
#else
    coordinator(NULL);
#endif
    *report = s_report;
    return s_result;
}
