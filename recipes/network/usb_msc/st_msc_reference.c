/**
 * \file            st_msc_reference.c
 * \brief           Single static ST MSC class over one exclusive Nexus Block
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-21
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "st_msc_reference.h"
#include <string.h>

/* The upstream storage callback ABI has no context. This product profile
 * deliberately owns exactly one Device/LUN, not a platform instance registry.
 */
static struct {
    const nx_block_port_t* block;
    nx_clock_t clock;
    uint32_t budget_us;
    nx_result_t fault;
    bool enabled;
    bool busy;
    bool allocated;
} media;
static _Alignas(32) USBD_MSC_BOT_HandleTypeDef class_storage;
static _Alignas(32) uint8_t transfer[512];

/** \brief Retain the first observed media fault for product recovery. */
static int8_t failed(nx_result_t result) {
    if (media.fault == NX_SUCCESS) {
        media.fault = result;
    }
    return -1;
}
/** \brief Readiness requires the real selected media predicate. */
static bool ready(void) {
    return media.block != NULL && media.enabled && !media.busy &&
           media.fault == NX_SUCCESS &&
           nx_block_port_state(media.block) == NX_BLOCK_READY;
}
/** \brief Initialize protocol access only after product-exclusive activation.
 */
static int8_t storage_init(uint8_t lun) {
    return lun == 0 && ready() ? 0 : -1;
}
/** \brief Report the exact admitted 512-byte sector geometry. */
static int8_t capacity(uint8_t lun, uint32_t* blocks, uint16_t* bytes) {
    if (lun != 0 || blocks == NULL || bytes == NULL || !ready()) {
        return -1;
    }
    const nx_block_geometry_t* g = nx_block_port_geometry(media.block);
    if (g == NULL || g->block_count == 0 || g->block_count > UINT32_MAX ||
        g->block_bytes != 512) {
        return failed(NX_ERROR_STATE);
    }
    *blocks = (uint32_t)g->block_count;
    *bytes = 512;
    return 0;
}
/** \brief SCSI readiness does not invent card presence or perform reprobe. */
static int8_t storage_ready(uint8_t lun) {
    return lun == 0 && ready() ? 0 : -1;
}
/** \brief Preserve the provider's explicit read-only media permission. */
static int8_t protected_media(uint8_t lun) {
    if (lun != 0 || media.block == NULL) {
        return 1;
    }
    const nx_block_geometry_t* g = nx_block_port_geometry(media.block);
    return g != NULL && g->writable ? 0 : 1;
}
/** \brief Construct one finite absolute budget shared by write and sync. */
static nx_result_t deadline(nx_time_us_t* out) {
    nx_time_us_t now = media.clock.read(media.clock.context);
    return nx_deadline_finite_after(now, media.budget_us, out);
}
/** \brief Admit exactly one class packet, without truncated multi-block I/O. */
static bool packet_valid(uint8_t lun, const uint8_t* bytes, uint32_t first,
                         uint16_t count) {
    if (lun != 0 || bytes == NULL || count != 1 || !ready()) {
        return false;
    }
    const nx_block_geometry_t* g = nx_block_port_geometry(media.block);
    return g != NULL && first < g->block_count;
}
/** \brief Copy from aligned provider storage only after a complete read. */
static int8_t storage_read(uint8_t lun, uint8_t* bytes, uint32_t first,
                           uint16_t count) {
    if (!packet_valid(lun, bytes, first, count)) {
        return -1;
    }
    nx_time_us_t limit;
    nx_result_t result = deadline(&limit);
    if (result != NX_SUCCESS) {
        return failed(result);
    }
    media.busy = true;
    nx_block_io_result_t io = nx_block_port_read(media.block, first, transfer,
                                                 sizeof(transfer), 1, limit);
    result = io.result != NX_SUCCESS    ? io.result
             : io.completed_blocks != 1 ? NX_ERROR_IO
                                        : NX_SUCCESS;
    if (result == NX_SUCCESS) {
        memcpy(bytes, transfer, sizeof(transfer));
    }
    media.busy = false;
    return result == NX_SUCCESS ? 0 : failed(result);
}
/** \brief Write-through one block and observe media readiness before success.
 */
static int8_t storage_write(uint8_t lun, uint8_t* bytes, uint32_t first,
                            uint16_t count) {
    if (!packet_valid(lun, bytes, first, count) || protected_media(lun) != 0) {
        return -1;
    }
    nx_time_us_t limit;
    nx_result_t result = deadline(&limit);
    if (result != NX_SUCCESS) {
        return failed(result);
    }
    media.busy = true;
    memcpy(transfer, bytes, sizeof(transfer));
    nx_block_io_result_t io = nx_block_port_write(media.block, first, transfer,
                                                  sizeof(transfer), 1, limit);
    result = io.result != NX_SUCCESS    ? io.result
             : io.completed_blocks != 1 ? NX_ERROR_IO
                                        : NX_SUCCESS;
    if (result == NX_SUCCESS) {
        result = nx_block_port_sync(media.block, limit);
    }
    media.busy = false;
    return result == NX_SUCCESS ? 0 : failed(result);
}
/** \brief Select one LUN, whose only valid index is zero. */
static int8_t maximum_lun(void) {
    return 0;
}
static int8_t inquiry[36] = {0,   -128, 2,   2,   31,  0,   0,   0,   'N',
                             'E', 'X',  'U', 'S', ' ', ' ', ' ', 'S', 'O',
                             'F', 'T',  'W', 'A', 'R', 'E', ' ', 'R', 'E',
                             'F', ' ',  ' ', ' ', ' ', '0', '0', '0', '1'};
USBD_StorageTypeDef example_st_msc_storage = {
    storage_init, capacity,      storage_ready, protected_media,
    storage_read, storage_write, maximum_lun,   inquiry};

/** \brief Bind exact geometry; no discovery, activation or USB I/O occurs. */
nx_result_t example_st_msc_bind(const nx_block_port_t* block, nx_clock_t clock,
                                uint32_t operation_budget_us) {
    if (block == NULL || clock.read == NULL || operation_budget_us == 0) {
        return NX_ERROR_INVALID;
    }
    if (media.block != NULL || media.allocated || media.busy) {
        return NX_ERROR_BUSY;
    }
    const nx_block_geometry_t* g = nx_block_port_geometry(block);
    if (g == NULL || g->block_bytes != 512 || g->block_count == 0 ||
        g->block_count > UINT32_MAX || g->buffer_alignment == 0 ||
        g->buffer_alignment > 32 ||
        (g->buffer_alignment & (g->buffer_alignment - 1U)) != 0) {
        return NX_ERROR_UNSUPPORTED;
    }
    media.block = block;
    media.clock = clock;
    media.budget_us = operation_budget_us;
    media.fault = NX_SUCCESS;
    return NX_SUCCESS;
}
/** \brief Enable callbacks only at the product's exclusive USB owner boundary.
 */
nx_result_t example_st_msc_activate(void) {
    if (media.block == NULL || media.allocated || media.busy ||
        nx_block_port_state(media.block) != NX_BLOCK_READY) {
        return NX_ERROR_STATE;
    }
    media.enabled = true;
    return NX_SUCCESS;
}
/** \brief Release local binding only after actual USB/class users have joined.
 */
nx_result_t example_st_msc_unbind(void) {
    if (media.allocated || media.busy) {
        return NX_ERROR_BUSY;
    }
    if (media.block == NULL) {
        return NX_ERROR_STATE;
    }
    memset(&media, 0, sizeof(media));
    memset(transfer, 0, sizeof(transfer));
    return NX_SUCCESS;
}
/** \brief Expose the first actual I/O failure without choosing recovery. */
nx_result_t example_st_msc_fault(void) {
    return media.fault;
}
/** \brief Allocate exactly the selected one-LUN class state, once. */
void* example_st_msc_allocate(uint32_t bytes) {
    if (bytes != sizeof(class_storage) || media.allocated || !media.enabled) {
        return NULL;
    }
    memset(&class_storage, 0, sizeof(class_storage));
    media.allocated = true;
    return &class_storage;
}
/** \brief Erase class memory only after its exact allocation is released. */
void example_st_msc_free(void* storage) {
    if (media.allocated && storage == &class_storage) {
        memset(&class_storage, 0, sizeof(class_storage));
        media.allocated = false;
    }
}

/* Reference identity is for software fixtures; products supply their assigned
 * VID/PID and own unique serial/descriptor policy before device deployment. */
static uint8_t device_descriptor[] = {18,   1, 0,    2, 0, 0, 0, 64, 0xfe,
                                      0xca, 1, 0x40, 0, 1, 1, 2, 3,  1};
static uint8_t language_descriptor[] = {4, 3, 0x09, 0x04};
static uint8_t manufacturer_descriptor[] = {12,  3, 'N', 0, 'e', 0,
                                            'x', 0, 'u', 0, 's', 0};
static uint8_t product_descriptor[] = {28,  3, 'M', 0, 'S', 0, 'C', 0, ' ', 0,
                                       'R', 0, 'e', 0, 'f', 0, 'e', 0, 'r', 0,
                                       'e', 0, 'n', 0, 'c', 0, 'e', 0};
static uint8_t serial_descriptor[] = {18,  3, '0', 0, '0', 0, '0', 0, '0', 0,
                                      '0', 0, '0', 0, '0', 0, '1', 0};
/** \brief Device reference bytes, with explicit FS-only profile. */
static uint8_t* device(USBD_SpeedTypeDef speed, uint16_t* size) {
    (void)speed;
    *size = sizeof(device_descriptor);
    return device_descriptor;
}
/** \brief One fixed software reference language. */
static uint8_t* language(USBD_SpeedTypeDef speed, uint16_t* size) {
    (void)speed;
    *size = sizeof(language_descriptor);
    return language_descriptor;
}
/** \brief Stable UTF-16LE manufacturer descriptor. */
static uint8_t* manufacturer(USBD_SpeedTypeDef speed, uint16_t* size) {
    (void)speed;
    *size = sizeof(manufacturer_descriptor);
    return manufacturer_descriptor;
}
/** \brief Stable UTF-16LE reference product/interface descriptor. */
static uint8_t* product(USBD_SpeedTypeDef speed, uint16_t* size) {
    (void)speed;
    *size = sizeof(product_descriptor);
    return product_descriptor;
}
/** \brief Stable reference serial; real products supply unique identity. */
static uint8_t* serial(USBD_SpeedTypeDef speed, uint16_t* size) {
    (void)speed;
    *size = sizeof(serial_descriptor);
    return serial_descriptor;
}
USBD_DescriptorsTypeDef example_st_msc_descriptors = {
    device, language, manufacturer, product, serial, product, product};
