/**
 * \file            axis_parameters.c
 * \brief           Explicit versioned axis parameters over existing Settings
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-22
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "axis_control.h"
#include "nexus/algorithms/endian.h"
#include <float.h>
#include <string.h>

_Static_assert(sizeof(float) == 4 && FLT_RADIX == 2 && FLT_MANT_DIG == 24 &&
                   FLT_MAX_EXP == 128,
               "The selected parameter record requires IEEE binary32");
enum { AXIS_RECORD_BYTES = 64, AXIS_RECORD_FLOATS = 12 };
#define AXIS_RECORD_MAGIC UINT32_C(0x31305841)

static bool quiesced(const example_axis_t* axis) {
    return axis != NULL && axis->initialized &&
           axis->state == EXAMPLE_AXIS_DISABLED && !axis->encoder_started &&
           !axis->pwm_started && !axis->parameters_pending;
}

static uint32_t get_word(const uint8_t* input) {
    uint64_t value = 0;
    (void)nx_endian_read(input, 4, true, &value);
    return (uint32_t)value;
}

static void put_word(uint8_t* output, uint32_t value) {
    (void)nx_endian_write(output, 4, true, value);
}

static void encode(const example_axis_t* axis, uint8_t* record) {
    const nx_pid_config_t* pid = &axis->parameters.pid;
    const float values[AXIS_RECORD_FLOATS] = {
        pid->kp,
        pid->ki,
        pid->kd,
        pid->tau_s,
        pid->output_minimum,
        pid->output_maximum,
        pid->integral_minimum,
        pid->integral_maximum,
        axis->parameters.minimum_position_rad,
        axis->parameters.maximum_position_rad,
        axis->parameters.maximum_velocity_rad_s,
        axis->parameters.acceleration_rad_s2};
    put_word(record, AXIS_RECORD_MAGIC);
    put_word(record + 4, axis->config.hardware_identity);
    put_word(record + 8, axis->config.calibration_revision);
    put_word(record + 12, axis->parameters.revision);
    for (size_t index = 0; index < AXIS_RECORD_FLOATS; ++index) {
        uint32_t bits;
        memcpy(&bits, &values[index], sizeof(bits));
        put_word(record + 16 + 4 * index, bits);
    }
}

static void decode(const uint8_t* record,
                   example_axis_parameters_t* parameters) {
    float values[AXIS_RECORD_FLOATS];
    for (size_t index = 0; index < AXIS_RECORD_FLOATS; ++index) {
        const uint32_t bits = get_word(record + 16 + 4 * index);
        memcpy(&values[index], &bits, sizeof(bits));
    }
    parameters->revision = get_word(record + 12);
    parameters->pid =
        (nx_pid_config_t){values[0], values[1], values[2], values[3],
                          values[4], values[5], values[6], values[7]};
    parameters->minimum_position_rad = values[8];
    parameters->maximum_position_rad = values[9];
    parameters->maximum_velocity_rad_s = values[10];
    parameters->acceleration_rad_s2 = values[11];
}

nx_storage_status_t example_axis_save_parameters(example_axis_t* axis,
                                                 nx_settings_t* settings,
                                                 uint32_t key) {
    if (!quiesced(axis) || settings == NULL) {
        return NX_STORAGE_INVALID;
    }
    uint8_t record[AXIS_RECORD_BYTES];
    encode(axis, record);
    nx_storage_status_t result =
        nx_settings_set(settings, key, record, sizeof(record));
    if (result == NX_STORAGE_OK) {
        result = nx_settings_commit(settings);
    }
    if (result == NX_STORAGE_OK) {
        axis->persisted_revision = axis->parameters.revision;
    }
    return result;
}

nx_storage_status_t example_axis_load_parameters(example_axis_t* axis,
                                                 const nx_settings_t* settings,
                                                 uint32_t key) {
    if (!quiesced(axis) || settings == NULL) {
        return NX_STORAGE_INVALID;
    }
    uint8_t record[AXIS_RECORD_BYTES];
    size_t bytes = sizeof(record);
    const nx_storage_status_t result =
        nx_settings_get(settings, key, record, &bytes);
    if (result != NX_STORAGE_OK) {
        return result;
    }
    if (bytes != sizeof(record)) {
        return NX_STORAGE_CORRUPT;
    }
    if (get_word(record) != AXIS_RECORD_MAGIC ||
        get_word(record + 4) != axis->config.hardware_identity ||
        get_word(record + 8) != axis->config.calibration_revision) {
        return NX_STORAGE_UNSUPPORTED;
    }
    example_axis_parameters_t parameters;
    decode(record, &parameters);
    example_axis_t candidate;
    if (example_axis_initialize(&candidate, &axis->config, &parameters) !=
        NX_SUCCESS) {
        return NX_STORAGE_CORRUPT;
    }
    axis->parameters = candidate.parameters;
    axis->pid = candidate.pid;
    axis->persisted_revision = parameters.revision;
    return NX_STORAGE_OK;
}
