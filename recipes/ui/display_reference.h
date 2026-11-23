/**
 * \file            display_reference.h
 * \brief           Caller-sized ILI9341 tiles and calibrated XPT2046 input
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-22
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#ifndef EXAMPLE_UI_DISPLAY_REFERENCE_H
#define EXAMPLE_UI_DISPLAY_REFERENCE_H
#include "../control/touch_reference.h"
#include "nexus/components/ili9341.h"
#ifdef __cplusplus
extern "C" {
#endif

/** \brief Product drawing progress, separate from the display hardware owner.
 * \note Zero before first use. One task serializes drawing and device calls.
 *       No display, pixels, queue, task or framebuffer is retained here.
 */
typedef struct {
    size_t completed_pixels;
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
    uint16_t color;
    bool active;
    bool failed;
} example_ui_fill_t;

/**
 * \brief Prepare one complete in-bounds solid rectangle without device I/O.
 * \param[in,out] fill: Zeroed/completed exclusive drawing state.
 * \param[in] viewport_width: Explicit logical width, 240 or 320 pixels.
 * \param[in] viewport_height: Matching logical height, 320 or 240 pixels.
 * \param[in] x: Rectangle left edge in logical pixels.
 * \param[in] y: Rectangle top edge in logical pixels.
 * \param[in] width: Positive rectangle width.
 * \param[in] height: Positive rectangle height.
 * \param[in] color: Native RGB565 halfword.
 * \return SUCCESS, INVALID for bounds, or STATE for active/failed work.
 * \note Pure fixed work; rejection leaves state unchanged. The actual display
 *       must have the same orientation. After failure, recover/stop its owner
 *       and discard this drawing state explicitly before preparing new work.
 */
nx_result_t example_ui_fill_begin(example_ui_fill_t* fill,
                                  uint16_t viewport_width,
                                  uint16_t viewport_height, uint16_t x,
                                  uint16_t y, uint16_t width, uint16_t height,
                                  uint16_t color);

/**
 * \brief Render at most one caller-sized packed tile with the real driver.
 * \param[in,out] fill: Live same-task rectangle, separate from all bindings.
 * \param[in,out] display: READY exclusive device, owned outside this recipe.
 * \param[out] tile: Naturally aligned RGB565 scratch, borrowed only this call.
 * \param[in] capacity: Available halfwords, at least one full rectangle row.
 * \param[in] deadline: Finite absolute deadline for this entire tile write.
 * \return SUCCESS on completion, BUSY with more rows, EMPTY when complete,
 *         EXHAUSTED for a short tile, or exact device error.
 * \note Work is bounded by supplied capacity and remaining rectangle size.
 *       Scratch is disjoint from fill, device and all transport/configuration
 *       storage. No pixel access survives any return: ILI9341's endpoint is
 *       synchronous/settled. Device errors close drawing admission and retain
 *       exact completed pixel progress; no automatic replay, stop or recovery.
 *       The caller joins UI users, stops touch/display successfully, then stops
 *       shared infrastructure. This does not prove optical refresh timing.
 */
nx_result_t example_ui_fill_step(example_ui_fill_t* fill, nx_ili9341_t* display,
                                 uint16_t* tile, size_t capacity,
                                 nx_time_us_t deadline);

/**
 * \brief Acquire one raw touch sample and apply the existing measured mapping.
 * \param[in,out] touch: Live single-task XPT2046 owner.
 * \param[in] calibration: Stable measured viewport matching the display.
 * \param[out] point: Separate output, unchanged on every failure/pen-up.
 * \param[in] deadline: Finite absolute deadline for the complete sample.
 * \return SUCCESS, EMPTY for pen-up/outside viewport, or exact device error.
 * \note Same task as display; no polling loop, gesture engine or new buffer.
 *       Sampling and calibration preserve original timestamp provenance.
 *       All arguments and opaque transport owner extents are disjoint.
 */
nx_result_t example_ui_touch(nx_xpt2046_t* touch,
                             const example_touch_calibration_t* calibration,
                             example_touch_point_t* point,
                             nx_time_us_t deadline);

#ifdef __cplusplus
}
#endif
#endif /* EXAMPLE_UI_DISPLAY_REFERENCE_H */
