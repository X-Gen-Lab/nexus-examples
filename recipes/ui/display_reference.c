/**
 * \file            display_reference.c
 * \brief           One bounded tile per caller step without a framebuffer
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-22
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "display_reference.h"

/** \brief Inputs are valid nonwrapping spans before this comparison. */
static bool overlaps(const void* a, size_t an, const void* b, size_t bn) {
    uintptr_t first = (uintptr_t)a;
    uintptr_t second = (uintptr_t)b;
    return first <= second ? second - first < an : first - second < bn;
}

nx_result_t example_ui_fill_begin(example_ui_fill_t* fill,
                                  uint16_t viewport_width,
                                  uint16_t viewport_height, uint16_t x,
                                  uint16_t y, uint16_t width, uint16_t height,
                                  uint16_t color) {
    if (fill == NULL ||
        !((viewport_width == 240U && viewport_height == 320U) ||
          (viewport_width == 320U && viewport_height == 240U)) ||
        width == 0U || height == 0U || x >= viewport_width ||
        y >= viewport_height || width > viewport_width - x ||
        height > viewport_height - y)
        return NX_ERROR_INVALID;
    if (fill->active || fill->failed)
        return NX_ERROR_STATE;
    *fill = (example_ui_fill_t){.x = x,
                                .y = y,
                                .width = width,
                                .height = height,
                                .color = color,
                                .active = true};
    return NX_SUCCESS;
}

nx_result_t example_ui_fill_step(example_ui_fill_t* fill, nx_ili9341_t* display,
                                 uint16_t* tile, size_t capacity,
                                 nx_time_us_t deadline) {
    if (fill == NULL || display == NULL || tile == NULL ||
        (uintptr_t)tile % _Alignof(uint16_t) != 0U ||
        capacity > SIZE_MAX / sizeof(*tile) ||
        (uintptr_t)tile > UINTPTR_MAX - capacity * sizeof(*tile) ||
        deadline == NX_DEADLINE_NEVER)
        return NX_ERROR_INVALID;
    if (fill->failed)
        return NX_ERROR_STATE;
    if (!fill->active)
        return NX_ERROR_EMPTY;
    if (fill->width == 0U || fill->height == 0U ||
        fill->completed_pixels >= (size_t)fill->width * fill->height ||
        fill->completed_pixels % fill->width != 0U)
        return NX_ERROR_STATE;
    if (capacity < fill->width)
        return NX_ERROR_EXHAUSTED;
    size_t rows = fill->height - fill->completed_pixels / fill->width;
    if (rows > capacity / fill->width)
        rows = capacity / fill->width;
    size_t count = rows * fill->width;
    if (overlaps(tile, count * sizeof(*tile), fill, sizeof(*fill)) ||
        overlaps(tile, count * sizeof(*tile), display, sizeof(*display)))
        return NX_ERROR_INVALID;
    for (size_t i = 0U; i < count; ++i)
        tile[i] = fill->color;
    size_t written = 0U;
    nx_result_t result = nx_ili9341_write(
        display, fill->x,
        (uint16_t)(fill->y + fill->completed_pixels / fill->width), fill->width,
        (uint16_t)rows, tile, count, &written, deadline);
    fill->completed_pixels += written;
    if (result != NX_SUCCESS) {
        fill->active = false;
        fill->failed = true;
        return result;
    }
    fill->active = fill->completed_pixels != (size_t)fill->width * fill->height;
    return fill->active ? NX_ERROR_BUSY : NX_SUCCESS;
}

nx_result_t example_ui_touch(nx_xpt2046_t* touch,
                             const example_touch_calibration_t* calibration,
                             example_touch_point_t* point,
                             nx_time_us_t deadline) {
    if (touch == NULL || calibration == NULL || point == NULL ||
        deadline == NX_DEADLINE_NEVER)
        return NX_ERROR_INVALID;
    nx_xpt2046_sample_t sample = {0};
    example_touch_point_t checked;
    nx_result_t result = example_touch_map(calibration, &sample, &checked);
    if (result == NX_ERROR_INVALID)
        return result;
    result = nx_xpt2046_sample(touch, &sample, deadline);
    return result == NX_SUCCESS ? example_touch_map(calibration, &sample, point)
                                : result;
}
