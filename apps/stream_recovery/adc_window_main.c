/**
 * \file            adc_window_main.c
 * \brief           Static ADC application and real F407 provider link contract
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-23
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "adc_acquisition.h"
#include "nexus_bindings.h"
#include "nexus_factory_strong.h"

#if !defined(NX_FACTORY_HAS_ADC)
#error "adc_window requires its ADC assembly and typed samples role"
#endif

#define ADC_BANKS            8U
#define ADC_COUNTS_PER_BLOCK 200U
#define PROGRESS_STEPS       1000000U

static _Alignas(4) uint16_t samples[ADC_BANKS][ADC_COUNTS_PER_BLOCK];
static nx_stream_slot_t slots[ADC_BANKS];
static nx_stream_t stream;
static example_adc_acquisition_t acquisition;

/** \brief Run a bounded call-count envelope without revoking failed loans. */
static void progress(void) {
    for (size_t step = 0U; step < PROGRESS_STEPS && acquisition.active;
         ++step) {
        (void)example_adc_acquisition_step(&acquisition, nx_time_now_us());
    }
}

/** \brief Failed exit retains all application and hardware-reference storage.
 */
int main(void) {
    nx_platform_start_result_t started = nx_platform_start();
    if (started.primary != NX_SUCCESS || started.cleanup != NX_SUCCESS ||
        started.remaining_effects != 0U) {
        return 1;
    }
    for (size_t index = 0U; index < ADC_BANKS; ++index) {
        slots[index] = (nx_stream_slot_t){.data = (uint8_t*)samples[index],
                                          .capacity = sizeof(samples[index])};
    }
    if (nx_stream_initialize(&stream, slots, ADC_BANKS) != NX_SUCCESS) {
        (void)nx_platform_stop();
        return 2;
    }
    nx_result_t result = example_adc_acquisition_start(
        &acquisition, nx_factory_adc_ref(NX_ADC_REF_SAMPLES), &stream, 1000U,
        1U, nx_time_now_us(), 1000000U);
    if (result != NX_SUCCESS) {
        (void)nx_platform_stop();
        return 3;
    }
    progress();
    if (acquisition.active) {
        (void)example_adc_acquisition_cancel(&acquisition);
        progress();
    }
    if (acquisition.active) {
        /* No Platform stop or stack borrow is lost on the retained path. */
        return 4;
    }
    nx_result_t stopped = nx_platform_stop();
    return acquisition.result == NX_SUCCESS && stopped == NX_SUCCESS ? 0 : 5;
}
