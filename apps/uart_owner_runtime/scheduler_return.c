/**
 * \file            scheduler_return.c
 * \brief           Preserve kernel-start partial effects on terminal failure
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-10-11
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "scheduler_return.h"
#include "nexus/arch/atomic.h"

/** \brief The start attempt crosses an irreversible application boundary.
 */
_Noreturn void
example_owner_scheduler_returned(example_owner_bootstrap_t* effects,
                                 uint32_t* status, uint32_t* stage,
                                 example_owner_fail_stop_port_t port) {
    effects->scheduler_started = true;
    nx_atomic_u32_store_relaxed(stage, EXAMPLE_OWNER_SCHEDULER_RETURNED);
    nx_atomic_u32_store_release(status, NX_ERROR_IO);
    if (port.fail_stop != NULL) {
        port.fail_stop(port.context);
    }
    /* Returning from a terminal product sink cannot revoke kernel effects.
     */
    for (;;) {
    }
}
