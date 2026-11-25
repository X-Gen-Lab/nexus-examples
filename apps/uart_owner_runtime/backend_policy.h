/**
 * \file            backend_policy.h
 * \brief           Product teardown choice from the selected generated backend
 * \author          Nexus Team
 */
#ifndef NEXUS_EXAMPLE_BACKEND_POLICY_H
#define NEXUS_EXAMPLE_BACKEND_POLICY_H
#include <stdbool.h>

#if (defined(NEXUS_BACKEND_NATIVE) + defined(NEXUS_BACKEND_BAREMETAL) +        \
     defined(NEXUS_BACKEND_FREERTOS) + defined(NEXUS_BACKEND_THREADX)) != 1
#error "Select exactly one generated OS backend before application policy"
#endif

/** \brief Kernel startup and terminal policy stay in application assembly.
 */
#if defined(NEXUS_BACKEND_FREERTOS) || defined(NEXUS_BACKEND_THREADX)
#define EXAMPLE_KERNEL_BACKEND        1
#define EXAMPLE_KEEP_PLATFORM_RUNNING true
#else
#define EXAMPLE_KERNEL_BACKEND        0
#define EXAMPLE_KEEP_PLATFORM_RUNNING false
#endif
#endif /* NEXUS_EXAMPLE_BACKEND_POLICY_H */
