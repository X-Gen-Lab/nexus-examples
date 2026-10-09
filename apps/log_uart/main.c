/** Application-owned typed UART logging: fixed queue, explicit pump and flush. */
#include "nexus_config.h"
#include "log/log.h"
#include "log/log_uart.h"
#include "osal/osal.h"
#include "runtime/nx_runtime.h"

#if defined(NX_CONFIG_PLATFORM_NATIVE)
#error "log_uart requires an MCU provider with typed UART TX capabilities"
#endif
#ifndef NEXUS_EXAMPLE_CONSOLE_DEVICE
#error "The application must select its board UART device"
#endif

enum { EXAMPLE_NOT_RUN = -1, EXAMPLE_PASSED = 0, EXAMPLE_FAILED = 1 };
enum { TX_BUDGET_MS = 100, FLUSH_BUDGET_MS = 1000, WORKER_STACK_BYTES = 2048 };
_Static_assert(LOG_UART_QUEUE_DEPTH == 4u, "This example demonstrates four slots");
_Static_assert(LOG_UART_MESSAGE_SIZE == 256u, "This example budgets 256-byte slots");

/* Permanent application-owned storage. A failed flush/cancel/close may retain
 * a provider buffer lease; never clear this object or let it leave scope. */
static log_uart_backend_t uart_sink;
volatile int log_uart_example_result = EXAMPLE_NOT_RUN;
volatile log_status_t log_uart_example_status = LOG_OK;
const char* volatile log_uart_example_failure;
volatile log_uart_stats_t log_uart_example_stats;

static bool checked(log_status_t status, const char* operation) {
    if (status == LOG_OK) return true;
    log_uart_example_status = status;
    log_uart_example_failure = operation;
    return false;
}

static bool snapshot(void) {
    log_uart_stats_t stats = {0};
    if (!checked(log_backend_uart_get_stats(&uart_sink, &stats), "UART stats"))
        return false;
    log_uart_example_stats = stats;
    return true;
}

static bool assertion(bool passed, const char* detail) {
    if (passed) return true;
    log_uart_example_status = LOG_ERROR_BACKEND;
    log_uart_example_failure = detail;
    return false;
}

static int run_logging_example(void) {
    /* The logger dispatches synchronously into the bounded copy queue. The
     * UART pump below has one owner; no implicit async log worker is started. */
    const log_config_t config = {
        .level = LOG_LEVEL_INFO, .format = "%m\r\n", .async_mode = false,
        .max_msg_len = 64, .color_enabled = false
    };
    if (!checked(log_init(&config), "logger init") ||
        !checked(log_backend_uart_init(&uart_sink, NEXUS_EXAMPLE_CONSOLE_DEVICE,
                                      TX_BUDGET_MS, FLUSH_BUDGET_MS),
                 "typed UART open") ||
        !checked(log_backend_register(&uart_sink.backend), "register UART"))
        goto failed;

    for (unsigned int batch = 0; batch < 3; ++batch) {
        /* Four writes fit before any service call. They copy into the slots;
         * producer stack data is never borrowed by the provider. */
        for (unsigned int slot = 0; slot < 4; ++slot) {
            unsigned int sequence = batch * 4u + slot;
            if (!checked(log_write(LOG_LEVEL_INFO, "example", __FILE__, __LINE__,
                                   __func__, "nexus log_uart sequence=%u", sequence),
                         "queue sequence")) goto failed;
        }
        if (batch == 0) {
            log_status_t status = log_write_raw("queue-full-probe\r\n", 18);
            if (!assertion(status == LOG_ERROR_FULL, "fifth write reports FULL") ||
                !snapshot() ||
                !assertion(log_uart_example_stats.accepted == 4 &&
                               log_uart_example_stats.pending == 4 &&
                               log_uart_example_stats.dropped == 1 &&
                               !log_uart_example_stats.buffer_leased,
                           "bounded queue counts before first pump"))
                goto failed;
        }
        /* service() starts/polls at most one operation without waiting. Flush
         * then owns the bounded wait through provider settlement AND wire idle. */
        log_status_t pump = log_backend_uart_service(&uart_sink);
        if (pump != LOG_OK && pump != LOG_ERROR_BUSY &&
            !checked(pump, "UART pump")) goto failed;
        if (!checked(log_backend_uart_flush(&uart_sink), "UART wire-idle flush") ||
            !snapshot()) goto failed;
    }

    if (!assertion(log_uart_example_stats.accepted == 12 &&
                       log_uart_example_stats.completed == 12 &&
                       log_uart_example_stats.dropped == 1 &&
                       log_uart_example_stats.failed == 0 &&
                       log_uart_example_stats.pending == 0 &&
                       !log_uart_example_stats.buffer_leased,
                   "twelve records completed with one explicit rejected write") ||
        !checked(log_backend_unregister("uart"), "unregister and close UART") ||
        !checked(log_backend_uart_shutdown(&uart_sink, false), "settled sink shutdown") ||
        !checked(log_deinit(), "logger deinit"))
        goto failed;

    log_uart_example_result = EXAMPLE_PASSED;
    return 0;

failed:
    /* Keep all still-owned objects and buffers alive for debugger inspection.
     * This sample does not choose a discard, reset or recovery policy. A failure
     * is latched; no stdout/newlib UART retarget is assumed or used. */
    (void)snapshot();
    log_uart_example_result = EXAMPLE_FAILED;
    return 1;
}

#if defined(NX_CONFIG_OSAL_FREERTOS)
static void logging_task(void* unused) {
    (void)unused;
    (void)run_logging_example();
    /* The finite worker finishes. Its OSAL TCB and the running kernel remain
     * firmware-owned; global runtime shutdown is not a supported MCU operation. */
}
#endif

int main(void) {
    if (nx_runtime_bootstrap(NULL) != NX_OK) {
        log_uart_example_result = EXAMPLE_FAILED;
        log_uart_example_failure = "runtime bootstrap";
        return 1;
    }
#if defined(NX_CONFIG_OSAL_FREERTOS)
    const osal_task_config_t config = {
        .name = "UARTLog", .func = logging_task,
        .priority = OSAL_TASK_PRIORITY_NORMAL, .stack_size = WORKER_STACK_BYTES
    };
    osal_task_handle_t task = NULL;
    if (osal_task_create(&config, &task) != OSAL_OK) {
        log_uart_example_result = EXAMPLE_FAILED;
        log_uart_example_failure = "create logging task";
        (void)nx_runtime_shutdown(NULL);
        return 1;
    }
    osal_start();
    log_uart_example_result = EXAMPLE_FAILED;
    log_uart_example_failure = "scheduler returned";
    return 1;
#else
    /* Baremetal runs the same finite sequence from main, with no hidden task.
     * MCU clocks/platform remain owned until an application-controlled reset. */
    return run_logging_example();
#endif
}
