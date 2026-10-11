/**
 * \file            fault_observer.c
 * \brief           Explicit application override of the SDK observation port
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-10-11
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "fault_policy.h"

/** \brief This example retains one first fault and leaves parking to the SDK.
 */
void nx_fault_observe(nx_fault_domain_t domain, nx_result_t reason,
                      const char* file, unsigned line) {
    (void)file;
    (void)example_fault_policy_observe(example_owner_fault_policy(), domain,
                                       reason, line);
}
