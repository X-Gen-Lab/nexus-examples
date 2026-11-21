/**
 * \file            touch_reference.h
 * \brief           Explicit XPT2046 viewport calibration for ILI9341 examples
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-21
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#ifndef EXAMPLE_TOUCH_REFERENCE_H
#define EXAMPLE_TOUCH_REFERENCE_H

#include "nexus/components/xpt2046.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \brief           Product-measured raw viewport and current logical dimensions
 * \note            Raw endpoints are increasing 12-bit values measured for the
 *                  fitted panel. They are not generic XPT2046 defaults. Width
 *                  and height are exactly 240x320 or 320x240, matching the
 *                  selected ILI9341 logical orientation. Swap maps raw Y to
 *                  logical X before the independent logical inversions.
 */
typedef struct {
    uint16_t raw_x_min;
    uint16_t raw_x_max;
    uint16_t raw_y_min;
    uint16_t raw_y_max;
    uint16_t width;
    uint16_t height;
    bool swap_axes;
    bool invert_x;
    bool invert_y;
} example_touch_calibration_t;

/** \brief Independent coordinate and original copied raw/time observations. */
typedef struct {
    uint16_t x;
    uint16_t y;
    nx_xpt2046_sample_t raw;
} example_touch_point_t;

/**
 * \brief           Map one successfully sampled raw point without device I/O
 * \param[in]       calibration: Explicit measured viewport and orientation.
 * \param[in]       sample: Complete successful XPT2046 sample, never failed RX.
 * \param[out]      point: Separate caller output, unchanged on every rejection.
 * \return          SUCCESS, EMPTY outside the viewport, or INVALID for null,
 *                  malformed calibration or ADC values outside 12 bits.
 * \note            Fixed C11 arithmetic, no allocation, clock or hidden worker.
 *                  Each normalized axis rounds to nearest, with ties upward,
 *                  before inversion. No clipping, filtering, pressure scaling
 *                  or claim of physical accuracy. The complete raw sample and
 *                  its observation provenance are copied without a new clock
 *                  reading. All three object extents are externally disjoint.
 */
nx_result_t example_touch_map(const example_touch_calibration_t* calibration,
                              const nx_xpt2046_sample_t* sample,
                              example_touch_point_t* point);

#ifdef __cplusplus
}
#endif
#endif /* EXAMPLE_TOUCH_REFERENCE_H */
