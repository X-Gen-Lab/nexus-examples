/**
 * \file            telemetry.c
 * \brief           Explicit little-endian records and one-attempt Task writes
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-25
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "telemetry.h"
#include "nexus/arch/arch.h"

static void put16(uint8_t* bytes, uint16_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8U);
}

static void put32(uint8_t* bytes, uint32_t value) {
    for (unsigned i = 0; i < 4U; ++i) {
        bytes[i] = (uint8_t)(value >> (8U * i));
    }
}

static void put64(uint8_t* bytes, uint64_t value) {
    for (unsigned i = 0; i < 8U; ++i) {
        bytes[i] = (uint8_t)(value >> (8U * i));
    }
}

static void encode(example_motor_telemetry_t* telemetry,
                   const example_motor_observation_t* record) {
    uint8_t* bytes = telemetry->frame;
    const nx_power_stage_sample_t* sample = &record->sample;
    const bool plan_valid = record->result == NX_SUCCESS;
    const nx_power_stage_dc_capture_t* dc = NULL;
#if defined(EXAMPLE_MOTOR_DC_OBSERVATION)
    if (record->dc_capture_valid) {
        dc = &record->dc_capture;
    }
#endif
    bytes[0] = 'N';
    bytes[1] = 'X';
    bytes[2] = 'M';
    bytes[3] = 'T';
    bytes[4] = 1U;
    bytes[5] =
        (uint8_t)((plan_valid ? 1U : 0U) | (sample->bus_valid ? 2U : 0U) |
                  (sample->encoder_valid ? 4U : 0U) | (dc != NULL ? 8U : 0U));
    put16(bytes + 6, EXAMPLE_MOTOR_TELEMETRY_BYTES);
    put32(bytes + 8, (uint32_t)record->result);
    put32(bytes + 12, telemetry->dropped);
    put64(bytes + 16, sample->sequence);
    put64(bytes + 24, sample->clock_epoch);
    put64(bytes + 32, sample->sample_at);
    put64(bytes + 40, sample->observed_at);
    put64(bytes + 48, sample->planned_apply_at);
    put32(bytes + 56, sample->encoder_valid ? sample->encoder_count : 0U);
    for (unsigned i = 0; i < 3U; ++i) {
        put16(bytes + 60U + 2U * i, sample->current_raw[i]);
        put32(bytes + 68U + 4U * i,
              plan_valid ? record->plan.duty_ticks[i] : 0U);
    }
    put16(bytes + 66, sample->bus_valid ? sample->bus_raw : 0U);
    put32(bytes + 80, plan_valid ? record->plan.trigger_ticks : 0U);
    put64(bytes + 84, plan_valid ? record->plan.sequence : 0U);
    put32(bytes + 92, sample->scan_span_us);
    for (unsigned i = 0; i < 2U; ++i) {
        put16(bytes + 96U + 2U * i, dc != NULL ? dc->code[i] : 0U);
        put32(bytes + 100U + 4U * i, dc != NULL ? dc->sample_ticks[i] : 0U);
        bytes[108U + i] = dc != NULL ? dc->switching_vector[i] : 0U;
    }
    bytes[110] = sample->current_count;
    bytes[111] = sample->current_phase_mask;
    put32(bytes + 112, telemetry->tick_hz);
}

nx_result_t
example_motor_telemetry_initialize(example_motor_telemetry_t* telemetry,
                                   example_motor_telemetry_write_t write,
                                   void* context, uint32_t tick_hz) {
    if (nx_arch_in_isr()) {
        return NX_ERROR_CONTEXT;
    }
    if (telemetry == NULL || write == NULL || tick_hz == 0U) {
        return NX_ERROR_INVALID;
    }
    *telemetry = (example_motor_telemetry_t){
        .write = write, .context = context, .tick_hz = tick_hz};
    return NX_SUCCESS;
}

nx_result_t
example_motor_telemetry_sink(void* context,
                             const example_motor_observation_t* record) {
    if (nx_arch_in_isr()) {
        return NX_ERROR_CONTEXT;
    }
    example_motor_telemetry_t* telemetry = context;
    if (telemetry == NULL || record == NULL || telemetry->write == NULL) {
        return NX_ERROR_INVALID;
    }
    if (telemetry->broken) {
        return NX_ERROR_STATE;
    }
    if (telemetry->pending) {
        if (telemetry->sequence != record->sample.sequence ||
            telemetry->epoch != record->sample.clock_epoch) {
            return NX_ERROR_STATE;
        }
    } else {
        encode(telemetry, record);
        telemetry->sequence = record->sample.sequence;
        telemetry->epoch = record->sample.clock_epoch;
        telemetry->offset = 0;
        telemetry->pending = true;
    }
    if (telemetry->offset < EXAMPLE_MOTOR_TELEMETRY_BYTES) {
        const size_t remaining =
            EXAMPLE_MOTOR_TELEMETRY_BYTES - telemetry->offset;
        size_t written = 0;
        const nx_result_t status = telemetry->write(
            telemetry->context, telemetry->frame + telemetry->offset, remaining,
            &written);
        if (written > remaining) {
            telemetry->broken = true;
            return NX_ERROR_IO;
        }
        telemetry->offset += written;
        if (status != NX_SUCCESS) {
            return status;
        }
    }
    if (telemetry->offset != EXAMPLE_MOTOR_TELEMETRY_BYTES) {
        return NX_ERROR_BUSY;
    }
    telemetry->pending = false;
    return NX_SUCCESS;
}
