/**
 * \file            touch_reference_test.cpp
 * \brief           Product calibration, viewport and copied touch observations
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-21
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "touch_reference.h"
#include <gtest/gtest.h>

namespace {
class TouchReference : public testing::Test {
  protected:
    example_touch_calibration_t calibration{100U, 3900U, 200U,  3800U, 240U,
                                            320U, false, false, false};
    nx_xpt2046_sample_t sample{};
    example_touch_point_t output{};

    void SetUp() override {
        sample.x = 100U;
        sample.y = 200U;
        sample.z1 = 123U;
        sample.z2 = 234U;
        sample.pressure_valid = true;
        sample.observed.ticks = 4567U;
        sample.observed.ticks_per_second = 1000000U;
        sample.observed.domain = 3U;
        sample.observed.epoch = 9U;
        sample.observed.source = NX_IO_TIMESTAMP_SERVICE_OBSERVATION;
        output.x = 99U;
        output.y = 88U;
        output.raw.observed.ticks = 77U;
    }

    void unchanged() {
        EXPECT_EQ(output.x, 99U);
        EXPECT_EQ(output.y, 88U);
        EXPECT_EQ(output.raw.observed.ticks, 77U);
    }
};

TEST_F(TouchReference, MeasuredViewportMapsBothDisplayEndpointsExactly) {
    ASSERT_EQ(example_touch_map(&calibration, &sample, &output), NX_SUCCESS);
    EXPECT_EQ(output.x, 0U);
    EXPECT_EQ(output.y, 0U);
    sample.x = 3900U;
    sample.y = 3800U;
    ASSERT_EQ(example_touch_map(&calibration, &sample, &output), NX_SUCCESS);
    EXPECT_EQ(output.x, 239U);
    EXPECT_EQ(output.y, 319U);
}

TEST_F(TouchReference, MidpointRoundingOccursBeforeLogicalInversion) {
    sample.x = 2000U;
    sample.y = 2000U;
    ASSERT_EQ(example_touch_map(&calibration, &sample, &output), NX_SUCCESS);
    EXPECT_EQ(output.x, 120U);
    EXPECT_EQ(output.y, 160U);
    calibration.invert_x = true;
    calibration.invert_y = true;
    ASSERT_EQ(example_touch_map(&calibration, &sample, &output), NX_SUCCESS);
    EXPECT_EQ(output.x, 119U);
    EXPECT_EQ(output.y, 159U);
}

TEST_F(TouchReference, SwappedAxesFollowLandscapeLogicalDimensions) {
    calibration.width = 320U;
    calibration.height = 240U;
    calibration.swap_axes = true;
    calibration.invert_x = true;
    sample.y = 3800U;
    ASSERT_EQ(example_touch_map(&calibration, &sample, &output), NX_SUCCESS);
    EXPECT_EQ(output.x, 0U);
    EXPECT_EQ(output.y, 0U);
    calibration.invert_y = true;
    ASSERT_EQ(example_touch_map(&calibration, &sample, &output), NX_SUCCESS);
    EXPECT_EQ(output.y, 239U);
}

TEST_F(TouchReference, OutsideMeasuredViewportIsRejectedWithoutClipping) {
    sample.x = 99U;
    EXPECT_EQ(example_touch_map(&calibration, &sample, &output),
              NX_ERROR_EMPTY);
    unchanged();
    sample.x = 100U;
    sample.y = 3801U;
    EXPECT_EQ(example_touch_map(&calibration, &sample, &output),
              NX_ERROR_EMPTY);
    unchanged();
}

TEST_F(TouchReference, InvalidCalibrationAndAdcLeaveCompleteOutputUnchanged) {
    auto invalid = calibration;
    invalid.raw_x_max = invalid.raw_x_min;
    EXPECT_EQ(example_touch_map(&invalid, &sample, &output), NX_ERROR_INVALID);
    unchanged();
    invalid = calibration;
    invalid.raw_y_max = 4096U;
    EXPECT_EQ(example_touch_map(&invalid, &sample, &output), NX_ERROR_INVALID);
    unchanged();
    invalid = calibration;
    invalid.width = 320U;
    EXPECT_EQ(example_touch_map(&invalid, &sample, &output), NX_ERROR_INVALID);
    unchanged();
    sample.x = 4096U;
    EXPECT_EQ(example_touch_map(&calibration, &sample, &output),
              NX_ERROR_INVALID);
    unchanged();
}

TEST_F(TouchReference, CopiesPreserveRawPressureAndOriginalTimeProvenance) {
    ASSERT_EQ(example_touch_map(&calibration, &sample, &output), NX_SUCCESS);
    sample.x = 3900U;
    sample.observed.ticks = 9999U;
    EXPECT_EQ(output.raw.x, 100U);
    EXPECT_EQ(output.raw.y, 200U);
    EXPECT_EQ(output.raw.z1, 123U);
    EXPECT_EQ(output.raw.z2, 234U);
    EXPECT_TRUE(output.raw.pressure_valid);
    EXPECT_EQ(output.raw.observed.ticks, 4567U);
    EXPECT_EQ(output.raw.observed.domain, 3U);
    EXPECT_EQ(output.raw.observed.epoch, 9U);
    EXPECT_EQ(output.raw.observed.ticks_per_second, 1000000U);
    EXPECT_EQ(output.raw.observed.source, NX_IO_TIMESTAMP_SERVICE_OBSERVATION);
    EXPECT_FALSE(output.raw.observed.uncertainty_known);
}

TEST_F(TouchReference, NullArgumentsDoNotInventAPoint) {
    EXPECT_EQ(example_touch_map(nullptr, &sample, &output), NX_ERROR_INVALID);
    EXPECT_EQ(example_touch_map(&calibration, nullptr, &output),
              NX_ERROR_INVALID);
    EXPECT_EQ(example_touch_map(&calibration, &sample, nullptr),
              NX_ERROR_INVALID);
    unchanged();
}
} /* namespace */
