/**
 * \file            main.c
 * \brief           Application-owned persistent journal region and workspace
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-10-09
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "nexus/components/file_flash.h"
#include <stdio.h>
#include <string.h>

/** \brief Commit and reopen one explicit file model without an MCU reservation.
 */
int main(int argc, char** argv) {
    if (argc != 2) {
        fputs("Usage: journal <dedicated-model-file>\n", stderr);
        return 1;
    }
    nx_file_flash_t file;
    if (nx_file_flash_open(&file, argv[1], 1024U, 256U, 4U) != NX_STORAGE_OK) {
        return 2;
    }
    uint8_t workspace[NX_STORAGE_HEADER_SIZE];
    nx_storage_t storage;
    static const uint8_t payload[] = {'s', 'e', 't', 't', 'i', 'n', 'g', 's'};
    if (nx_storage_open(&storage, &file.port, 0U, 512U, workspace,
                        sizeof(workspace)) != NX_STORAGE_OK ||
        nx_storage_save(&storage, payload, sizeof(payload)) != NX_STORAGE_OK) {
        nx_file_flash_close(&file);
        return 3;
    }
    nx_file_flash_close(&file);
    if (nx_file_flash_open(&file, argv[1], 1024U, 256U, 4U) != NX_STORAGE_OK ||
        nx_storage_open(&storage, &file.port, 0U, 512U, workspace,
                        sizeof(workspace)) != NX_STORAGE_OK) {
        return 4;
    }
    uint8_t output[sizeof(payload)];
    size_t length = sizeof(output);
    nx_storage_status_t result = nx_storage_load(&storage, output, &length);
    nx_file_flash_close(&file);
    if (result != NX_STORAGE_OK || length != sizeof(payload) ||
        memcmp(output, payload, length) != 0) {
        return 5;
    }
    puts("Journal reopened: durable file model; MCU power-loss test not "
         "executed");
    return 0;
}
