/**
 * \file            inference_reference.c
 * \brief           Fixed numerical graph over actual CMSIS-NN dense kernels
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-22
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "inference_reference.h"
#include "nexus/algorithms/cmsis_nn.h"
#include <limits.h>

/** \brief Explicit row-major model tensors, with accumulator-unit biases. */
static const int8_t hidden_weights[12] = {1, 0, 0, -1, 0,  0,
                                          0, 1, 1, 0,  -1, -1};
static const int32_t hidden_bias[4] = {0, 0, -4, 4};
static const int8_t output_weights[8] = {1, 1, -1, -1, -1, -1, 1, 1};
static const int32_t output_bias[2] = {0, 0};
static const nx_cmsis_nn_dense_s8_t hidden_layer = {
    3U, 4U, hidden_weights, hidden_bias, 0, 0, INT32_MAX, 0, 0, 127};
static const nx_cmsis_nn_dense_s8_t output_layer = {
    4U, 2U, output_weights, output_bias, 0, 0, INT32_MAX, 0, -128, 127};

nx_result_t example_inference_classify(const int8_t input[3],
                                       example_inference_result_t* result) {
    if (input == NULL || result == NULL) {
        return NX_ERROR_INVALID;
    }
    int8_t hidden[4];
    example_inference_result_t next = {0};
    nx_result_t status =
        nx_cmsis_nn_dense_s8(&hidden_layer, input, 3U, hidden, sizeof(hidden));
    if (status == NX_SUCCESS) {
        status = nx_cmsis_nn_dense_s8(&output_layer, hidden, sizeof(hidden),
                                      next.logits, sizeof(next.logits));
    }
    if (status == NX_SUCCESS) {
        next.class_index = next.logits[1] > next.logits[0] ? 1U : 0U;
        *result = next;
    }
    return status;
}
