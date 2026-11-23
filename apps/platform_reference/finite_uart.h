/**
 * \file            finite_uart.h
 *
 * \brief           Single-executor finite UART application lifecycle
 * \author          Nexus Team
 */
#ifndef EXAMPLE_FINITE_UART_H
#define EXAMPLE_FINITE_UART_H

#include "nexus/io/uart.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 *
 * \brief Application storage retained through complete withdrawal.
 *
 * \note Zero-initialize once before publication. One task owns all operations.
 *       Every payload span is nonwrapping and disjoint from this full object.
 *       Public faces and their provider contexts outlive every call and borrow.
 *       Pending, quarantine and BUSY stop retain all storage. This example
 *       installs no observers, wake targets or extra tasks.
 */
typedef struct {
    nx_uart_tx_request_t request;
    const nx_uart_port_t* port;
    nx_result_t result;
    size_t transferred;
    bool pending;
    bool stopping;
    bool stopped;
} example_finite_uart_t;

/**
 *
 * \brief Submit bytes using a finite deadline in the live provider domain.
 *
 * \param[in,out] app: Fresh or completely settled single-executor state.
 *
 * \param[in] port: Live port retained through complete stop.
 *
 * \param[in] bytes: Immutable data retained through observed settlement.
 *
 * \param[in] length: Positive byte count.
 *
 * \param[in] now: Snapshot from the live port's Core clock domain.
 *
 * \param[in] budget: Finite relative microsecond budget.
 *
 * \return SUCCESS means borrowed; rejection retains no payload.
 *
 * \note Task-only, bounded; no allocation, wait, worker or recovery policy.
 */
nx_result_t example_finite_uart_start(example_finite_uart_t* app,
                                      const nx_uart_port_t* port,
                                      const uint8_t* bytes, size_t length,
                                      nx_time_us_t now, uint64_t budget);

/** \brief Advance at most one service and consume a settled result.
 *
 * \param[in,out] app: Original live single-executor state.
 *
 * \return BUSY while borrowed; otherwise the final result.
 *
 * \note Task-only, bounded. A quarantined request remains pending; timeout or
 *       cancellation cannot authorize reclamation or another start.
 */
nx_result_t example_finite_uart_step(example_finite_uart_t* app);

/** \brief Close application admission and request cancellation once per call.
 *
 * \param[in,out] app: Original state retained through subsequent progress.
 *
 * \return SUCCESS requests drain, not settlement; errors retain pending state.
 *
 * \note Task-only. The same executor continues step after every return.
 */
nx_result_t example_finite_uart_request_stop(example_finite_uart_t* app);

/** \brief Make one bounded attempt to withdraw the UART after settlement.
 *
 * \param[in,out] app: State whose producers and external callers have joined.
 *
 * \return SUCCESS permits this example's port storage release; BUSY retains it.
 *
 * \note Task-only, no loop. Platform stops separately after all its users join.
 *       Running OS infrastructure is subject to its own shutdown contract.
 */
nx_result_t example_finite_uart_finish(example_finite_uart_t* app);

#ifdef __cplusplus
}
#endif
#endif
