/**
 * \file            inference_reference.h
 * \brief           Explicit fixed-graph signed-byte inference composition
 * \author          Nexus Team
 */
#ifndef EXAMPLE_INFERENCE_REFERENCE_H
#define EXAMPLE_INFERENCE_REFERENCE_H
#include "nexus/core/status.h"
#ifdef __cplusplus
extern "C" {
#endif
/** \brief Numeric graph output; logits are quantized values, not probability.
 */
typedef struct {
    int8_t logits[2];
    uint8_t class_index;
} example_inference_result_t;
/**
 * \brief           Evaluate the fixed 3-to-4-to-2 demonstration graph.
 * \param[in]       input: Three stable signed-byte numerical input features.
 * \param[out]      result: Disjoint aligned output, unchanged on rejection.
 * \return          SUCCESS or the selected kernel failure.
 * \note            Caller owns feature extraction, quantization and execution
 *                  budget. Pure C11 with automatic scratch and immutable model
 *                  arrays; no clock, task, retained borrow or heap. Labels
 *                  describe this numeric reference only; these hand-authored
 *                  weights establish no trained product classifier accuracy.
 *                  A tie chooses index zero. Reset/join follows the caller's
 *                  ordinary execution scope; this function retains no state.
 */
nx_result_t example_inference_classify(const int8_t input[3],
                                       example_inference_result_t* result);
#ifdef __cplusplus
}
#endif
#endif
