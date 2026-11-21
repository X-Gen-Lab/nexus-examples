/**
 * \file            control_loop.h
 * \brief           Explicit ADC stream, filter, PID and PWM reference loop
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-21
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#ifndef EXAMPLE_INDUSTRIAL_CONTROL_LOOP_H
#define EXAMPLE_INDUSTRIAL_CONTROL_LOOP_H
#include "nexus/algorithms/filter.h"
#include "nexus/algorithms/pid.h"
#include "nexus/components/fsm.h"
#include "nexus/io/adc.h"
#include "nexus/io/timer.h"
#ifdef __cplusplus
extern "C" {
#endif
/** \brief This fragment owns only its consumer and control fault state. */
typedef enum {
    EXAMPLE_CONTROL_RUNNING,
    EXAMPLE_CONTROL_FAULT,
    EXAMPLE_CONTROL_STOPPED
} example_control_state_t;
/**
 * \brief Explicit one-channel normalized control profile, no Board defaults.
 * \note Caller exclusively starts/stops ADC stream and PWM through its existing
 *       owner. Every block contains one native uint16_t scan. full_scale_counts
 *       maps raw counts to normalized measurement; setpoint is in [0,1].
 *       PWM period is the already selected shared base. Sample period is the
 *       declared acquisition interval, not delivery/IRQ time or measured
 *       aperture. PID output limits must lie within [0,1].
 */
typedef struct {
    const nx_adc_port_t* adc;
    nx_stream_t* stream;
    const nx_pwm_port_t* pwm;
    nx_pid_config_t pid;
    uint32_t pwm_period_ticks;
    uint32_t sample_period_us;
    uint16_t full_scale_counts;
    float alpha;
    float setpoint;
} example_control_config_t;
/** \brief Copied successful sample facts, never a live block/payload pointer.
 */
typedef struct {
    float measurement;
    float command;
    uint32_t duty_ticks;
    uint32_t samples;
} example_control_observation_t;
/**
 * \brief Caller-owned storage; fields are implementation state, not controls.
 * \note One existing task/loop serializes initialize, step and finish. ADC,
 *       PWM, stream, coefficients and storage remain live until source stop,
 *       all users join and finish succeeds. No allocation, global tick, task,
 *       hardware discovery, duplicate queue or automatic recovery is created.
 */
typedef struct {
    example_control_config_t config;
    nx_ema_t filter;
    nx_pid_t pid;
    nx_fsm_t machine;
    nx_stream_block_t borrowed_block;
    uint32_t samples;
    bool borrowed;
} example_control_loop_t;
/**
 * \brief           Initialize unused independent consumer/control storage.
 * \param[out]      loop: Fresh distinct caller storage, unchanged on rejection.
 * \param[in]       config: Valid explicit profile, copied without starting I/O.
 * \return          SUCCESS or INVALID; no callbacks/hardware/OS calls.
 * \note            Caller must establish exclusive live ports and the stream
 *                  session before step. Reinitialize only after complete join.
 */
nx_result_t
example_control_loop_initialize(example_control_loop_t* loop,
                                const example_control_config_t* config);
/**
 * \brief           Process at most one real ADC stream loan on the caller task.
 * \param[in,out]   loop: Initialized RUNNING consumer; no concurrent
 * step/finish. \param[out]      out: Distinct copied facts, unchanged on EMPTY
 * or error. \return          SUCCESS, EMPTY, STATE or actual
 * acquisition/control/PWM error. \note            Calls actual ADC service,
 * stream acquire/release, EMA, PID and PWM set. BUSY service can coexist with a
 * ready block. Loss/error blocks and invalid counts fault the control. A fault
 * never authorizes reclaiming the live ADC session; the existing owner must
 * stop PWM/ADC and join. Every block is released before return when possible;
 * failed release retains its exact loan in loop until finish succeeds. A staged
 * PWM update may precede a subsequent release failure.
 */
nx_result_t example_control_loop_step(example_control_loop_t* loop,
                                      example_control_observation_t* out);
/**
 * \brief           Finish control after the existing owner stops and joins I/O.
 * \param[in,out]   loop: Initialized caller state, with no future step calls.
 * \return          SUCCESS or actual retained-consumer release failure.
 * \note            Retries only an outstanding consumer release, then marks
 *                  STOPPED. It does not stop ADC/PWM, revoke producer borrows,
 *                  destroy the stream, join tasks or implement app shutdown.
 */
nx_result_t example_control_loop_finish(example_control_loop_t* loop);
#ifdef __cplusplus
}
#endif
#endif
