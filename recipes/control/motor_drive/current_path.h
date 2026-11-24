/**
 * \file            current_path.h
 * \brief           Statically selected current capture and voltage mapping leaf
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-24
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#ifndef EXAMPLE_MOTOR_CURRENT_PATH_H
#define EXAMPLE_MOTOR_CURRENT_PATH_H

#include "control.h"

#ifdef __cplusplus
extern "C" {
#endif

/** \brief Validate only the statically selected hardware/current topology.
 * \param[in] config: Immutable explicit application configuration.
 * \param[in] stage: Actual cold provider facts in carrier tick units.
 * \return SUCCESS or exact unsupported/invalid profile error.
 */
nx_result_t
example_motor_path_validate(const example_motor_drive_config_t* config,
                            const nx_power_stage_state_t* stage);

/** \brief Reconstruct physical currents without inventing missing samples.
 * \param[in] context: Borrowed IRQ context with qualified calibration.
 * \param[in] raw: Callback-local actual selected observation.
 * \param[out] current: Complete reconstructed physical phase currents.
 * \return SUCCESS or exact current/timing/identity failure.
 */
nx_result_t
example_motor_path_sample(const example_motor_drive_context_t* context,
                          const nx_power_stage_sample_t* raw,
                          nx_phase_current_t* current);

/** \brief Produce complete PWM/capture group and final quantized voltage.
 * \param[in,out] context: Owns any descriptor retained until provider copy.
 * \param[in] constraints: Qualified selected profile and timing budgets.
 * \param[in] requested: Finite stationary alpha/beta voltage.
 * \param[in] bus_v: Qualified positive DC-link voltage.
 * \param[out] modulation: Full selected quantized average-voltage result.
 * \param[out] plan: Complete pulse/capture plan; caller assigns next sequence.
 * \return SUCCESS or exact planner/profile failure, never a partial plan.
 */
nx_result_t example_motor_path_plan(example_motor_drive_context_t* context,
                                    const nx_modulation_config_t* constraints,
                                    nx_motor_ab_t requested, float bus_v,
                                    nx_modulation_output_t* modulation,
                                    nx_power_stage_plan_t* plan);

/** \brief Initialize a selected finite session's first complete output plan.
 * \param[in,out] context: Stable owner containing the descriptor storage.
 * \param[in] constraints: Same qualified profile used for later planning.
 * \param[out] plan: Initial sequence-zero selected pulse/capture group.
 * \return SUCCESS or exact unsupported/unqualified initial profile.
 */
nx_result_t
example_motor_path_initial(example_motor_drive_context_t* context,
                           const nx_modulation_config_t* constraints,
                           nx_power_stage_plan_t* plan);

#ifdef __cplusplus
}
#endif
#endif /* EXAMPLE_MOTOR_CURRENT_PATH_H */
