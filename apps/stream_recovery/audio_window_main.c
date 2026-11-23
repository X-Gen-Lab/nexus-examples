/**
 * \file            audio_window_main.c
 * \brief           Static duplex pattern and real F407 audio link contract
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-23
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "audio_acquisition.h"
#include "nexus_bindings.h"
#include "nexus_factory_strong.h"

#if !defined(NX_FACTORY_HAS_AUDIO)
#error "audio_window requires its audio assembly and typed sound role"
#endif

#define AUDIO_BANKS    3U
#define AUDIO_WORDS    128U
#define PROGRESS_STEPS 1000000U

static _Alignas(4) uint16_t tx_samples[AUDIO_BANKS][AUDIO_WORDS];
static _Alignas(4) uint16_t rx_samples[AUDIO_BANKS][AUDIO_WORDS];
static nx_stream_slot_t tx_slots[AUDIO_BANKS];
static nx_stream_slot_t rx_slots[AUDIO_BANKS];
static nx_stream_t tx;
static nx_stream_t rx;
static example_audio_playback_t playback;
static example_audio_acquisition_t acquisition;
static nx_stream_fill_t pending_fill;
static bool has_pending_fill;

/** \brief Bound progress calls; failed drain never ends static lifetimes. */
static void progress(void) {
    for (size_t step = 0U; step < PROGRESS_STEPS && acquisition.active;
         ++step) {
        (void)example_audio_acquisition_step(&acquisition, nx_time_now_us());
    }
}

/** \brief Explicitly publish a reference ramp in mutable DMA-visible SRAM. */
static nx_result_t prepare(const nx_audio_port_t* port) {
    nx_audio_layout_t layout;
    nx_result_t result = nx_audio_port_layout(port, &layout);
    if (result != NX_SUCCESS) {
        return result;
    }
    if (layout.container_bits != 16U || layout.sample_bits != 16U ||
        layout.slots != 2U || layout.active_slots != 3U ||
        layout.storage_order != NX_AUDIO_STORAGE_LITTLE_ENDIAN) {
        return NX_ERROR_UNSUPPORTED;
    }
    for (size_t bank = 0U; bank < AUDIO_BANKS; ++bank) {
        for (size_t word = 0U; word < AUDIO_WORDS; ++word) {
            tx_samples[bank][word] = (uint16_t)((word % 32U) * 256U);
        }
        tx_slots[bank] =
            (nx_stream_slot_t){.data = (uint8_t*)tx_samples[bank],
                               .capacity = sizeof(tx_samples[bank])};
        rx_slots[bank] =
            (nx_stream_slot_t){.data = (uint8_t*)rx_samples[bank],
                               .capacity = sizeof(rx_samples[bank])};
    }
    result = nx_stream_initialize(&tx, tx_slots, AUDIO_BANKS);
    if (result == NX_SUCCESS) {
        result = nx_stream_initialize(&rx, rx_slots, AUDIO_BANKS);
    }
    for (size_t bank = 0U; result == NX_SUCCESS && bank < AUDIO_BANKS; ++bank) {
        result = nx_stream_reserve(&tx, &pending_fill);
        if (result == NX_SUCCESS) {
            has_pending_fill = true;
            result = nx_stream_publish(&tx, &pending_fill,
                                       sizeof(tx_samples[bank]), 0U);
            if (result == NX_SUCCESS) {
                has_pending_fill = false;
            }
        }
        if (result != NX_SUCCESS && has_pending_fill &&
            nx_stream_abort(&tx, &pending_fill, true) == NX_SUCCESS) {
            /* This unpublished fill was never passed to the audio owner. */
            has_pending_fill = false;
        }
    }
    return result;
}

/** \brief Link and exercise the public composition without a vendor Handle. */
int main(void) {
    nx_platform_start_result_t started = nx_platform_start();
    if (started.primary != NX_SUCCESS || started.cleanup != NX_SUCCESS ||
        started.remaining_effects != 0U) {
        return 1;
    }
    const nx_audio_port_t* port = nx_factory_audio_ref(NX_AUDIO_REF_SOUND);
    if (prepare(port) != NX_SUCCESS) {
        (void)nx_platform_stop();
        return 2;
    }
    playback.stream = &tx;
    nx_result_t result = example_audio_acquisition_start_streams(
        &acquisition, port, &playback, &rx, sizeof(rx_samples[0]), 1U,
        nx_time_now_us(), 100000U);
    if (result != NX_SUCCESS) {
        (void)nx_platform_stop();
        return 3;
    }
    progress();
    if (acquisition.active) {
        (void)example_audio_acquisition_cancel(&acquisition);
        progress();
    }
    if (acquisition.active) {
        return 4;
    }
    nx_result_t stopped = nx_platform_stop();
    return acquisition.result == NX_SUCCESS && stopped == NX_SUCCESS ? 0 : 5;
}
