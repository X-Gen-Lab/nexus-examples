/**
 * \file            maintenance.h
 * \brief           Product-owned read-only maintenance composition
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-21
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#ifndef EXAMPLE_DIAGNOSTICS_MAINTENANCE_H
#define EXAMPLE_DIAGNOSTICS_MAINTENANCE_H
#include "nexus/components/log.h"
#include "nexus/components/shell_storage.h"

#ifdef __cplusplus
extern "C" {
#endif

/** \brief Explicit composition storage, private to one calling task. */
typedef struct {
    nx_shell_t shell;
    char line_history[128];
    nx_log_t log;
    nx_health_snapshot_t health;
    nx_reset_record_t reset;
    nx_shell_diagnostics_t diagnostics;
    nx_result_t exit_result;
    bool shell_live;
    bool log_live;
} example_diagnostics_t;

/**
 * \brief Read existing boot facts, then expose one read-only Shell and Log.
 * \param[in,out] state: Initially zeroed exclusive storage through joined exit.
 * \param[in] io: Bounded synchronous transport owned by the calling task.
 * \param[in] sink: Selected synchronous Log sink, including bounded flush.
 * \param[in] version: Immutable valid text, at most 80 bytes, through exit.
 * \param[in] retained: Quiescent live boot-retained bytes; never acknowledged.
 * \param[in] reset_causes: Already-latched product reset causes.
 * \param[in] facts: Stable health observations in one monotonic domain.
 * \param[in] deadline: Finite absolute output deadline understood by transport.
 * \return SUCCESS, STATE if already live, or public component/transport error.
 * \note Single task before publication; no platform start, task, heap, fault
 * entry, recovery, persistence or reset. Failure detaches accepted components;
 * exit_result reports incomplete cleanup, whose storage remains live.
 */
nx_result_t
example_diagnostics_start(example_diagnostics_t* state, nx_shell_io_t io,
                          nx_log_sink_port_t sink, const char* version,
                          const volatile nx_fault_record_t* retained,
                          uint32_t reset_causes, const nx_health_facts_t* facts,
                          nx_time_us_t deadline);

/**
 * \brief Refresh copied health and process a bounded number of Shell bytes.
 * \param[in,out] state: Unique live task owner; no concurrent callbacks.
 * \param[in] facts: New stable same-domain observations, copied before input.
 * \param[in] maximum_characters: Per-call budget, from 1 through 16.
 * \param[in] deadline: Finite absolute deadline passed unchanged to output.
 * \return SUCCESS, EMPTY, INVALID, STATE or component/transport error.
 * \note No busy input loop or implicit clock sample. Transport/health failure
 * closes Shell admission and attempts ordered exit once, preserving its result
 * in exit_result. Observing maintenance input is not useful-work progress.
 */
nx_result_t example_diagnostics_step(example_diagnostics_t* state,
                                     const nx_health_facts_t* facts,
                                     unsigned maximum_characters,
                                     nx_time_us_t deadline);

/**
 * \brief Detach Shell, flush selected sink once, then stop Log admission.
 * \param[in,out] state: Sole task after all application publishers joined.
 * \param[in] deadline: Finite absolute deadline for one bounded flush.
 * \return SUCCESS, BUSY, or component/transport error; caller chooses retry.
 * \note Never stops the shared transport. An incomplete exit retains storage;
 * RTT control-block buffers/name remain valid for the whole image even after
 * successful stop. Reclaim other state only after successful exit and joins.
 */
nx_result_t example_diagnostics_stop(example_diagnostics_t* state,
                                     nx_time_us_t deadline);

#ifdef __cplusplus
}
#endif
#endif /* EXAMPLE_DIAGNOSTICS_MAINTENANCE_H */
