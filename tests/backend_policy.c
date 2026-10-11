/**
 * \file            backend_policy.c
 * \brief           Compile the production teardown choice for each backend
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-10-11
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "backend_policy.h"

bool EXAMPLE_POLICY_SYMBOL(void);
/** \brief Expose the configured constant to the host contract fixture. */
bool EXAMPLE_POLICY_SYMBOL(void) {
    return EXAMPLE_KEEP_PLATFORM_RUNNING;
}
