/**
 * \file            touch_reference.c
 * \brief           Caller-selected raw touch viewport to logical pixel mapping
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-21
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "touch_reference.h"

/** \brief Restrict arithmetic to the measured 12-bit native-panel viewport. */
static bool valid(const example_touch_calibration_t* calibration) {
    return calibration->raw_x_min < calibration->raw_x_max &&
           calibration->raw_y_min < calibration->raw_y_max &&
           calibration->raw_x_max <= 4095U && calibration->raw_y_max <= 4095U &&
           ((calibration->width == 240U && calibration->height == 320U) ||
            (calibration->width == 320U && calibration->height == 240U));
}

/** \brief Bounded positive ratios round before logical orientation changes. */
static uint16_t scale(uint16_t value, uint16_t low, uint16_t high,
                      uint16_t extent) {
    uint32_t span = (uint32_t)high - low;
    uint32_t numerator = ((uint32_t)value - low) * ((uint32_t)extent - 1U);
    return (uint16_t)((numerator + span / 2U) / span);
}

nx_result_t example_touch_map(const example_touch_calibration_t* calibration,
                              const nx_xpt2046_sample_t* sample,
                              example_touch_point_t* point) {
    if (calibration == NULL || sample == NULL || point == NULL ||
        !valid(calibration) || sample->x > 4095U || sample->y > 4095U ||
        sample->z1 > 4095U || sample->z2 > 4095U) {
        return NX_ERROR_INVALID;
    }
    if (sample->x < calibration->raw_x_min ||
        sample->x > calibration->raw_x_max ||
        sample->y < calibration->raw_y_min ||
        sample->y > calibration->raw_y_max) {
        return NX_ERROR_EMPTY;
    }
    uint16_t x = calibration->swap_axes
                     ? scale(sample->y, calibration->raw_y_min,
                             calibration->raw_y_max, calibration->width)
                     : scale(sample->x, calibration->raw_x_min,
                             calibration->raw_x_max, calibration->width);
    uint16_t y = calibration->swap_axes
                     ? scale(sample->x, calibration->raw_x_min,
                             calibration->raw_x_max, calibration->height)
                     : scale(sample->y, calibration->raw_y_min,
                             calibration->raw_y_max, calibration->height);
    if (calibration->invert_x) {
        x = (uint16_t)(calibration->width - 1U - x);
    }
    if (calibration->invert_y) {
        y = (uint16_t)(calibration->height - 1U - y);
    }
    example_touch_point_t output = {.x = x, .y = y, .raw = *sample};
    *point = output;
    return NX_SUCCESS;
}
