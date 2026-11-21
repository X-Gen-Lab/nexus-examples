/**
 * \file            maintenance.c
 * \brief           Explicit read-only diagnostics assembly and ordered exit
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-21
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "maintenance.h"

/** \brief Preserve the primary error while exposing any incomplete exit. */
static nx_result_t fail(example_diagnostics_t* state, nx_result_t primary,
                        nx_time_us_t deadline) {
    state->exit_result = example_diagnostics_stop(state, deadline);
    return primary;
}

/** \brief Remove command entry before flushing the externally owned sink. */
nx_result_t example_diagnostics_stop(example_diagnostics_t* state,
                                     nx_time_us_t deadline) {
    if (state == NULL || deadline == NX_DEADLINE_NEVER)
        return NX_ERROR_INVALID;
    nx_result_t result = NX_SUCCESS;
    if (state->shell_live) {
        result = nx_shell_stop(&state->shell);
        if (result != NX_SUCCESS)
            goto done;
        state->shell_live = false;
    }
    if (state->log_live) {
        result = nx_log_flush(&state->log, deadline);
        if (result != NX_SUCCESS)
            goto done;
        result = nx_log_stop(&state->log);
        if (result == NX_SUCCESS)
            state->log_live = false;
    }
done:
    state->exit_result = result;
    return result;
}

/** \brief Snapshot boot facts before admitting any maintenance commands. */
nx_result_t
example_diagnostics_start(example_diagnostics_t* state, nx_shell_io_t io,
                          nx_log_sink_port_t sink, const char* version,
                          const volatile nx_fault_record_t* retained,
                          uint32_t reset_causes, const nx_health_facts_t* facts,
                          nx_time_us_t deadline) {
    if (state == NULL || sink.flush == NULL || deadline == NX_DEADLINE_NEVER)
        return NX_ERROR_INVALID;
    if (state->shell_live || state->log_live)
        return NX_ERROR_STATE;
    nx_health_snapshot_t health;
    nx_result_t result = nx_health_snapshot(facts, &health);
    if (result != NX_SUCCESS)
        return result;
    nx_reset_record_t reset;
    result = nx_reset_record_read(retained, reset_causes, &reset);
    if (result != NX_SUCCESS)
        return result;
    state->health = health;
    state->reset = reset;
    state->diagnostics = (nx_shell_diagnostics_t){
        .version = version, .health = &state->health, .reset = &state->reset};
    state->exit_result = NX_SUCCESS;
    result = nx_log_init(&state->log, sink, NX_LOG_INFO);
    if (result != NX_SUCCESS)
        return result;
    state->log_live = true;
    result = nx_shell_init(&state->shell, io, state->line_history,
                           sizeof(state->line_history), &state->diagnostics,
                           deadline);
    if (result != NX_SUCCESS)
        return fail(state, result, deadline);
    state->shell_live = true;
    static const char ready[] = "maintenance ready\r\n";
    result = nx_log_write(&state->log, NX_LOG_INFO, ready, sizeof(ready) - 1U,
                          deadline);
    return result == NX_SUCCESS ? NX_SUCCESS : fail(state, result, deadline);
}

/** \brief One calling task supplies observations and the exact byte budget. */
nx_result_t example_diagnostics_step(example_diagnostics_t* state,
                                     const nx_health_facts_t* facts,
                                     unsigned maximum_characters,
                                     nx_time_us_t deadline) {
    if (state == NULL || maximum_characters == 0U || maximum_characters > 16U ||
        deadline == NX_DEADLINE_NEVER)
        return NX_ERROR_INVALID;
    if (!state->shell_live || !state->log_live)
        return NX_ERROR_STATE;
    nx_result_t result = nx_health_snapshot(facts, &state->health);
    if (result != NX_SUCCESS)
        return fail(state, result, deadline);
    for (unsigned i = 0U; i < maximum_characters; ++i) {
        result = nx_shell_step(&state->shell, deadline);
        if (result == NX_ERROR_EMPTY)
            return result;
        if (result != NX_SUCCESS)
            return fail(state, result, deadline);
    }
    return NX_SUCCESS;
}
