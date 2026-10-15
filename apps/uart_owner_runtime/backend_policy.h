/**
 * \file            backend_policy.h
 * \brief           Product teardown choice from the selected generated backend
 * \author          Nexus Team
 */
#ifndef NEXUS_EXAMPLE_BACKEND_POLICY_H
#define NEXUS_EXAMPLE_BACKEND_POLICY_H
#include <stdbool.h>

#if (defined(NEXUS_BACKEND_NATIVE) + defined(NEXUS_BACKEND_BAREMETAL) +        \
     defined(NEXUS_BACKEND_FREERTOS)) != 1
#error "Select exactly one generated OS backend before application policy"
#endif

/** \brief A live FreeRTOS scheduler retains clocks/Tick until explicit reset.
 */
#if defined(NEXUS_BACKEND_FREERTOS)
#define EXAMPLE_KEEP_PLATFORM_RUNNING true
#else
#define EXAMPLE_KEEP_PLATFORM_RUNNING false
#endif
#endif /* NEXUS_EXAMPLE_BACKEND_POLICY_H */
