/**
 * \file            workload.h
 * \brief           Finite public-UART acquisition with persistent caller loans.
 * \author          Nexus Team
 */
#ifndef FIFO_ACQUISITION_WORKLOAD_H
#define FIFO_ACQUISITION_WORKLOAD_H

#include "nexus/io/uart.h"
#ifdef __cplusplus
extern "C" {
#endif

#define FIFO_ACQUISITION_TX_BYTES         32U
#define FIFO_ACQUISITION_RX_BYTES         4096U
#define FIFO_ACQUISITION_SERVICE_SAMPLES  256U
#define FIFO_ACQUISITION_CLEANUP_ATTEMPTS 64U

typedef enum {
    FIFO_ACQUISITION_COLD,
    FIFO_ACQUISITION_RUNNING,
    FIFO_ACQUISITION_DRAINING,
    FIFO_ACQUISITION_RETAINED,
    FIFO_ACQUISITION_CLOSED
} fifo_acquisition_phase_t;

/** \brief One finite session in the already-started provider clock domain. */
typedef struct {
    uint32_t session;
    uint32_t duration_us;
    uint32_t service_period_us;
    uint32_t transfer_timeout_us;
    uint32_t drain_timeout_us;
    uint32_t cpu_work_units;
} fifo_acquisition_config_t;

/**
 * \brief Application-owned static state, never a replacement UART provider.
 * \note Zero before first initialize. One task owns all fields. Snapshot only
 *       after CLOSED and successful Platform stop; the two producers are then
 *       joined. RETAINED still owns request/payload and is never reclaimable.
 */
typedef struct {
    const nx_uart_port_t* port;
    fifo_acquisition_config_t config;
    nx_uart_tx_request_t request;
    uint8_t transmit[FIFO_ACQUISITION_TX_BYTES];
    uint8_t received[FIFO_ACQUISITION_RX_BYTES];
    nx_time_us_t service_times[FIFO_ACQUISITION_SERVICE_SAMPLES];
    nx_time_us_t started;
    nx_time_us_t window_end;
    nx_time_us_t acquisition_end;
    nx_time_us_t last_now;
    nx_time_us_t next_service;
    nx_time_us_t drain_end;
    uint32_t received_count;
    uint32_t service_count;
    uint32_t overflow_events;
    uint32_t mismatch_count;
    uint32_t accepted_requests;
    uint32_t rejected_submissions;
    uint32_t completed_requests;
    uint32_t completed_tx_bytes;
    volatile uint32_t cpu_checksum;
    nx_result_t first_error;
    fifo_acquisition_phase_t phase;
    bool borrowed;
    bool cancel_sent;
    bool trace_exhausted;
    bool clock_fault;
} fifo_acquisition_t;

/**
 * \brief Initialize cold application storage without calling any UART method.
 * \param[in,out] state: Exclusive zero-initialized static application storage.
 * \param[in] port: Started stable public byte-UART face; outlives state/IRQs.
 * \param[in] config: Copied finite reviewed session; no implicit defaults.
 * \param[in] now: Finite current time in the provider's live clock epoch.
 * \return SUCCESS, INVALID or STATE; rejection has no UART effects.
 * \note The owner must never reinitialize a live or retained session.
 */
nx_result_t fifo_acquisition_initialize(fifo_acquisition_t* state,
                                        const nx_uart_port_t* port,
                                        const fifo_acquisition_config_t* config,
                                        nx_time_us_t now);
/**
 * \brief Perform one bounded task step with TX service and independent RX.
 * \param[in,out] state: Same sole unmasked owner and persistent storage.
 * \param[in] now: Nondecreasing time from the original live provider epoch.
 * \return First diagnostic; SUCCESS does not mean stopped or settled.
 * \note CLOSED proves provider stop only. Caller must stop/join Platform too.
 *       On drain expiry retain storage and keep stepping for possible late TC.
 *       A broken clock epoch retains forever for external recovery; no further
 *       provider access is attempted with invalid timing assumptions.
 */
nx_result_t fifo_acquisition_step(fifo_acquisition_t* state, nx_time_us_t now);
/**
 * \brief Close traffic admission; cancellation/drain occur in later steps.
 * \param[in,out] state: Same sole owner, not a cross-task cancellation slot.
 * \param[in] now: Current time from the same live provider epoch.
 * \return SUCCESS, INVALID or STATE. This never releases a borrow.
 */
nx_result_t fifo_acquisition_close(fifo_acquisition_t* state, nx_time_us_t now);

/**
 * \brief Retry explicit Platform cleanup with a finite application budget.
 * \param[in] state: COLD before any app loan, or CLOSED after UART stop.
 * \param[in] stop_platform: The existing nx_platform_stop leaf, same owner.
 * \param[in] attempt_budget: 1..FIFO_ACQUISITION_CLEANUP_ATTEMPTS calls.
 * \param[out] attempts: Actual calls, unchanged on invalid/state rejection.
 * \return SUCCESS only after the leaf proves release; otherwise its last
 *         error. Budget exhaustion never implies time expiry or join.
 * \note No live application writer, observer or retained UART loan may enter.
 *       The callback must obey the existing finite Platform stop contract;
 *       an attempt count is not a WCET bound on arbitrary external callbacks.
 *       No clock is read after stop may have withdrawn its source. All static
 *       state stays alive on every return, including BUSY/IO and exhaustion.
 */
nx_result_t fifo_acquisition_join_platform(const fifo_acquisition_t* state,
                                           nx_result_t (*stop_platform)(void),
                                           uint32_t attempt_budget,
                                           uint32_t* attempts);

#ifdef __cplusplus
}
#endif
#endif
