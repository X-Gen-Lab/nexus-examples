/**
 * \file            main.c
 * \brief           Explicit Native gateway and external ciphertext peer
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-02
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#define _POSIX_C_SOURCE 200809L
#include "native/host.h"

/* Failed cleanup may retain stack and descriptor borrows through process exit.
 */
static example_gateway_host_t host;

/** \brief Own exact static storage and delegate the Native composition. */
int main(int argc, char** argv) {
    return example_gateway_host_run(&host, argc, argv);
}
