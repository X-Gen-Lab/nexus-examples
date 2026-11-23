/**
 * \file            main.c
 * \brief           Application-owned LED and UART reference composition
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-10-09
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "finite_uart.h"
#include "nexus_bindings.h"
#include "nexus_config.h"
#include "nexus_factory_strong.h"
#include "rx_policy.h"
#if defined(NEXUS_BACKEND_FREERTOS)
#include "nexus/os/storage.h"
#endif
#if defined(NEXUS_BACKEND_NATIVE)
#include "nexus/io/native/model.h"
#include <stdio.h>
#endif

static example_finite_uart_t s_transmit;
static uint8_t s_echo[8];
static bool s_status_active;
static nx_time_us_t s_next_iteration;

#if defined(NEXUS_BACKEND_NATIVE)
/** \brief Print host diagnostics without turning metadata into release rights.
 */
static void report_lifecycle(nx_result_t primary, nx_result_t cleanup,
                             uint32_t remaining_effects,
                             const nx_platform_diagnostic_t* diagnostic) {
    (void)fprintf(stderr,
                  "platform primary=%u cleanup=%u remaining_effects=%lu "
                  "phase=%u controller_index=%zu cleanup_phase=%u "
                  "cleanup_controller_index=%zu\n",
                  (unsigned)primary, (unsigned)cleanup,
                  (unsigned long)remaining_effects, (unsigned)diagnostic->phase,
                  diagnostic->controller_index,
                  (unsigned)diagnostic->cleanup_phase,
                  diagnostic->cleanup_controller_index);
}
#endif

/** \brief Map an application console role to one generated physical port. */
static const nx_uart_port_t* console(void) {
    return nx_factory_uart_ref(NX_UART_REF_CONSOLE);
}

/** \brief Admit a finite transfer; the outer application advances it. */
static nx_result_t send_bytes(const uint8_t* bytes, size_t length) {
    return example_finite_uart_start(&s_transmit, console(), bytes, length,
                                     nx_time_now_us(), 100000U);
}

/** \brief Execute a serialized iteration with explicit RX error decisions. */
static nx_result_t iteration(void) {
    if (s_transmit.pending) {
        return example_finite_uart_step(&s_transmit);
    }
    nx_time_us_t now = nx_time_now_us();
    if (now < s_next_iteration) {
        return NX_SUCCESS;
    }
    nx_result_t timing =
        nx_deadline_finite_after(now, 100000U, &s_next_iteration);
    if (timing != NX_SUCCESS) {
        return timing;
    }
    s_status_active = !s_status_active;
    nx_result_t result = nx_gpio_signal_write(
        nx_factory_gpio_signal_ref(NX_GPIO_REF_STATUS), s_status_active);
    if (result != NX_SUCCESS) {
        return result;
    }
    size_t count = 0U;
    result = example_reference_read(console(), s_echo, sizeof(s_echo), &count);
    if (result == NX_ERROR_EMPTY) {
        return NX_SUCCESS;
    }
    if (result != NX_SUCCESS) {
        return result;
    }
    return count == 0U ? NX_SUCCESS : send_bytes(s_echo, count);
}

#if defined(NEXUS_BACKEND_FREERTOS)
static nx_task_t s_application;
NX_OS_STACK_DEFINE(s_stack, 1024U);

/** \brief The external application creates and owns this sole worker. */
static void application(void* context) {
    (void)context;
    static const uint8_t greeting[] = "Nexus static factory\r\n";
    if (send_bytes(greeting, sizeof(greeting) - 1U) != NX_SUCCESS) {
        return;
    }
    for (;;) {
        nx_result_t result = iteration();
        if (result != NX_SUCCESS && result != NX_ERROR_BUSY) {
            return;
        }
        nx_time_us_t wake;
        if (nx_deadline_finite_after(nx_os_now_us(),
                                     s_transmit.pending ? 1000U : 100000U,
                                     &wake) != NX_SUCCESS) {
            /* Static transfer storage and infrastructure remain retained. */
            return;
        }
        (void)nx_sleep_until(wake);
    }
}

/** \brief Create kernel objects inside the explicit OS bootstrap boundary. */
static void kernel_startup(void* context) {
    (void)context;
    if (nx_task_start(&s_application, "reference", s_stack, sizeof(s_stack), 2U,
                      application, NULL) != NX_SUCCESS) {
        /* Failure retains the platform and all static task references. */
        for (;;) {
        }
    }
}
#endif

/** \brief Start the selected platform without an implicit product scheduler. */
int main(void) {
    nx_platform_diagnostic_t diagnostic;
    nx_platform_start_result_t startup =
        nx_platform_start_diagnosed(&diagnostic);
    if (startup.primary != NX_SUCCESS || startup.cleanup != NX_SUCCESS ||
        startup.remaining_effects != 0U) {
#if defined(NEXUS_BACKEND_NATIVE)
        report_lifecycle(startup.primary, startup.cleanup,
                         startup.remaining_effects, &diagnostic);
#endif
        /* Metadata never authorizes a reset, retry or borrowed-storage reuse.
         * The external product owns recovery when effects remain outstanding.
         */
        return 1;
    }
#if defined(NEXUS_BACKEND_FREERTOS)
    (void)nx_os_run(kernel_startup, NULL);
    return 3;
#elif defined(NEXUS_BACKEND_NATIVE)
    static const uint8_t ping[] = {'p', 'i', 'n', 'g'};
    int exit_code = 0;
    for (size_t i = 0U; i < sizeof(ping); ++i) {
        if (nx_native_uart_model_receive(console(), ping[i], 0U) !=
            NX_SUCCESS) {
            exit_code = 4;
            break;
        }
    }
    nx_result_t iteration_result = iteration();
    for (unsigned step = 0U; s_transmit.pending && step < 64U; ++step) {
        iteration_result = example_finite_uart_step(&s_transmit);
    }
    if (s_transmit.pending) {
        /* Host failure retains every static loan and does not stop Platform. */
        return 9;
    }
    if (exit_code == 0 && (iteration_result != NX_SUCCESS ||
                           nx_native_gpio_model_output(nx_factory_gpio_ref(
                               NX_GPIO_REF_STATUS)) != NEXUS_STATUS_MASK)) {
        exit_code = 5;
    }
    nx_result_t stopped = nx_platform_stop_diagnosed(&diagnostic);
    if (stopped != NX_SUCCESS) {
        (void)fprintf(stderr,
                      "platform stop=%u phase=%u controller_index=%zu\n",
                      (unsigned)stopped, (unsigned)diagnostic.phase,
                      diagnostic.controller_index);
        return 6;
    }
    return exit_code;
#else
    static const uint8_t greeting[] = "Nexus static factory\r\n";
    if (send_bytes(greeting, sizeof(greeting) - 1U) != NX_SUCCESS) {
        return 7;
    }
    for (;;) {
        nx_result_t result = iteration();
        if (result != NX_SUCCESS && result != NX_ERROR_BUSY) {
            return 8;
        }
        /* One bounded pass leaves room for the product's other cooperative
         * work. A quarantined transfer keeps its static storage and owner.
         */
    }
#endif
}
