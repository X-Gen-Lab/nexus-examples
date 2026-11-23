/**
 * \file            adc_acquisition.h
 * \brief           Caller-owned finite-window ADC block application
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-23
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#ifndef EXAMPLE_ADC_ACQUISITION_H
#define EXAMPLE_ADC_ACQUISITION_H
#include "nexus/io/adc.h"
#ifdef __cplusplus
extern "C" {
#endif
/** \brief Caller-owned ADC session and copied raw-count observations.
 *
 * \note Zero-initialize before first publication. The task exclusively owns
 *       this object, its port and the stream consumer role. blocks/scans count
 *       complete consumed storage, not physical conversion timestamps. The
 *       last raw count has no implicit voltage calibration. Do not reset this
 *       object or reclaim its graph while active or has_pending is true.
 */
typedef struct {
    const nx_adc_port_t* port;
    nx_stream_t* stream;
    nx_stream_block_t pending;
    nx_time_us_t deadline;
    nx_time_us_t last_now;
    size_t scan_bytes;
    size_t block_limit;
    size_t blocks;
    size_t scans;
    uint16_t last_count;
    nx_result_t result;
    bool active;
    bool stopping;
    bool source_stopped;
    bool has_pending;
} example_adc_acquisition_t;

/**
 * \brief Start a finite-window continuous ADC consumer without a worker.
 *
 * \param[in,out] app: Independent, initially zeroed or fully joined storage.
 *
 * \param[in] port: Prepared ADC owner, kept live throughout source shutdown.
 *
 * \param[in,out] stream: Initialized FREE pool; slots and full reservations
 *       remain independent of app, face/ops and all other pool/control storage.
 *
 * \param[in] trigger_hz: Positive requested complete-scan rate.
 *
 * \param[in] block_limit: Positive number of valid blocks to consume.
 *
 * \param[in] now: Finite ready clock snapshot in the provider's live domain.
 *
 * \param[in] duration_us: Representable relative budget, including startup.
 *
 * \return SUCCESS admits the session; other results retain no new stream
 *       borrow. Invalid known spans/aliases reject before any port callback or
 *       application state write. Finite deadline overflow returns EXHAUSTED.
 *
 * \note Task-only cold call, one serialized executor. Caller owns genuine
 *       initialized objects; validation cannot prove arbitrary pointers mapped
 *       or determine the complete private provider-context extent. The real
 *       provider separately validates DMA domains, geometry and private state.
 *       No observer/wake binding is installed. Acceptance does not prove
 *       warmup or physical triggering complete.
 */
nx_result_t example_adc_acquisition_start(example_adc_acquisition_t* app,
                                          const nx_adc_port_t* port,
                                          nx_stream_t* stream,
                                          uint32_t trigger_hz,
                                          size_t block_limit, nx_time_us_t now,
                                          uint64_t duration_us);

/**
 * \brief Service once, consume at most one block and attempt one source stop.
 *
 * \param[in,out] app: Same stable, exclusively executed application graph.
 *
 * \param[in] now: Nondecreasing snapshot in the original live clock domain.
 *
 * \return BUSY while progress/loans remain; a completed window returns SUCCESS
 *       or its first operation failure. Other errors may also leave active
 *       true: always inspect active before reclaiming or closing Platform.
 *
 * \note No loop or blocking wait. Timeout/cancel never revoke storage. Failed
 *       release preserves the exact token and never processes it twice. active
 *       becomes false only after provider source join and a fully free stream.
 *       Independently installed observers/waiters still require caller join.
 */
nx_result_t example_adc_acquisition_step(example_adc_acquisition_t* app,
                                         nx_time_us_t now);

/**
 * \brief Request cancellation while preserving all accepted storage.
 *
 * \param[in,out] app: Active session under the same serialized task executor.
 *
 * \return SUCCESS records the request; STATE for absent/inactive storage.
 *
 * \note Call step until active is false. This is neither cross-task control
 *       nor permission to clear a quarantined provider or close its clock.
 */
nx_result_t example_adc_acquisition_cancel(example_adc_acquisition_t* app);
#ifdef __cplusplus
}
#endif
#endif
