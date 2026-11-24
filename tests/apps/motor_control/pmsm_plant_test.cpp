/**
 * \file            pmsm_plant_test.cpp
 * \brief           Independent electrical, mechanical and sensing contracts
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-24
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "plant.h"
#include <cmath>
#include <cstring>
#include <gtest/gtest.h>

namespace {
example_pmsm_config_t configuration() {
    return {0.8, 0.0004, 0.0004, 0.015, 0.0002, 0.0001, 0.005, 7, 16384, 2048};
}

TEST(PmsmPlant, NeutralOutputPreservesRestAndQuantizesOnlySensors) {
    example_pmsm_t model{};
    const auto config = configuration();
    ASSERT_EQ(example_pmsm_initialize(&model, &config), NX_SUCCESS);
    const float neutral[3] = {0.5F, 0.5F, 0.5F};
    for (unsigned i = 0; i < 1000; ++i) {
        ASSERT_EQ(example_pmsm_advance(&model, neutral, 24, 0.00005, 0),
                  NX_SUCCESS);
    }
    EXPECT_DOUBLE_EQ(model.id_a, 0);
    EXPECT_DOUBLE_EQ(model.iq_a, 0);
    EXPECT_DOUBLE_EQ(model.velocity_rad_s, 0);
    example_pmsm_sensor_t sensed{};
    ASSERT_EQ(example_pmsm_sense(&model, &sensed), NX_SUCCESS);
    for (auto code : sensed.current_code) {
        EXPECT_EQ(code, 2048);
    }
    EXPECT_EQ(sensed.encoder_count, 0U);
    EXPECT_FALSE(sensed.saturated);
}

TEST(PmsmPlant, QAxisVoltageProducesTorqueWithoutReadingControlReference) {
    example_pmsm_t model{};
    const auto config = configuration();
    ASSERT_EQ(example_pmsm_initialize(&model, &config), NX_SUCCESS);
    const float duty[3] = {0.5F, 0.55F, 0.45F};
    ASSERT_EQ(example_pmsm_advance(&model, duty, 24, 0.00005, 0), NX_SUCCESS);
    EXPECT_GT(model.iq_a, 0);
    EXPECT_GT(model.velocity_rad_s, 0);
    EXPECT_GT(model.dc_current_a, 0);
}

TEST(PmsmPlant, SensorWrapAndSaturationAreObservable) {
    example_pmsm_t model{};
    const auto config = configuration();
    ASSERT_EQ(example_pmsm_initialize(&model, &config), NX_SUCCESS);
    model.position_rad = -6.283185307179586 / 16384;
    model.id_a = 30;
    example_pmsm_sensor_t sensed{};
    ASSERT_EQ(example_pmsm_sense(&model, &sensed), NX_SUCCESS);
    EXPECT_EQ(sensed.encoder_count, 65535U);
    EXPECT_TRUE(sensed.saturated);
}

TEST(PmsmPlant, InvalidStepPreservesCompleteState) {
    example_pmsm_t model{};
    const auto config = configuration();
    ASSERT_EQ(example_pmsm_initialize(&model, &config), NX_SUCCESS);
    const auto before = model;
    const float duty[3] = {0.5F, 1.1F, 0.5F};
    EXPECT_EQ(example_pmsm_advance(&model, duty, 24, 0.00005, 0),
              NX_ERROR_INVALID);
    EXPECT_EQ(std::memcmp(&model, &before, sizeof(model)), 0);
}
} /* namespace */
