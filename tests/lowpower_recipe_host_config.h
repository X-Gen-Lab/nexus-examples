/**
 * \file            lowpower_recipe_host_config.h
 * \brief           Native hook model selects public variable placement only
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-10-30
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#ifndef EXAMPLE_LOWPOWER_RECIPE_HOST_CONFIG_H
#define EXAMPLE_LOWPOWER_RECIPE_HOST_CONFIG_H

#include "nexus_config.h"

/* Keep the generated Native ABI; the host linker can retain the SDK's real
 * SRAM4 section attributes without pretending to model physical SRAM4. */
#undef NEXUS_MEMORY_SRAM4
#define NEXUS_MEMORY_SRAM4 1

/* The object target renames this firmware entry for host link collection. */
int main(void);

#endif /* EXAMPLE_LOWPOWER_RECIPE_HOST_CONFIG_H */
