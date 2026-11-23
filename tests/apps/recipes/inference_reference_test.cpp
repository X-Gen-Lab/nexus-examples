/**
 * \file            inference_reference_test.cpp
 * \brief           Fixed quantized graph consumes the selected real kernels
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-22
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "inference_reference.h"
#include <array>
#include <gtest/gtest.h>

TEST(InferenceReference, EvaluatesBothLayersWithExplicitInputScale) {
    const std::array<int8_t, 3> first{10, 0, 0};
    example_inference_result_t result{};
    ASSERT_EQ(example_inference_classify(first.data(), &result), NX_SUCCESS);
    EXPECT_EQ(result.class_index, 0U);
    EXPECT_EQ(result.logits[0], 6);
    EXPECT_EQ(result.logits[1], -6);
    const std::array<int8_t, 3> second{0, 10, 10};
    ASSERT_EQ(example_inference_classify(second.data(), &result), NX_SUCCESS);
    EXPECT_EQ(result.class_index, 1U);
    EXPECT_EQ(result.logits[0], -16);
    EXPECT_EQ(result.logits[1], 16);
}

TEST(InferenceReference, RejectionAndRepeatedCallsHaveNoHiddenState) {
    const std::array<int8_t, 3> zero{};
    example_inference_result_t result{{99, 98}, 7U};
    EXPECT_EQ(example_inference_classify(nullptr, &result), NX_ERROR_INVALID);
    EXPECT_EQ(result.class_index, 7U);
    ASSERT_EQ(example_inference_classify(zero.data(), &result), NX_SUCCESS);
    EXPECT_EQ(result.class_index, 1U);
    const auto first = result;
    ASSERT_EQ(example_inference_classify(zero.data(), &result), NX_SUCCESS);
    EXPECT_EQ(result.logits[0], first.logits[0]);
    EXPECT_EQ(result.logits[1], first.logits[1]);
}
