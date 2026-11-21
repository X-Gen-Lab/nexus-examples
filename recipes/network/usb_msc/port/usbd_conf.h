/**
 * \file            usbd_conf.h
 * \brief           Exact single-LUN MSC application profile, no libc allocator
 * \author          Nexus Team
 */
#ifndef NEXUS_EXAMPLE_MSC_USBD_CONF_H
#define NEXUS_EXAMPLE_MSC_USBD_CONF_H
#include <stdint.h>
#include <string.h>
#ifdef __cplusplus
extern "C" {
#endif
void* example_st_msc_allocate(uint32_t bytes);
void example_st_msc_free(void* storage);
#ifdef __cplusplus
}
#endif
/* Match the selected Nexus ST Device Core ABI; MSC-specific state is private.
 */
#define USBD_MAX_NUM_INTERFACES       4U
#define USBD_MAX_NUM_CONFIGURATION    1U
#define USBD_MAX_SUPPORTED_CLASS      1U
#define USBD_LPM_ENABLED              0U
#define USBD_SELF_POWERED             0U
#define USBD_DEBUG_LEVEL              0U
#define USBD_SUPPORT_USER_STRING_DESC 0U
#define USBD_CLASS_USER_STRING_DESC   0U
#define USBD_CLASS_BOS_ENABLED        0U
#define USBD_USER_REGISTER_CALLBACK   0U
#define MSC_BOT_MAX_LUN               1U
#define MSC_MEDIA_PACKET              512U
#define USBD_malloc                   example_st_msc_allocate
#define USBD_free                     example_st_msc_free
#define USBD_memset                   memset
#define USBD_memcpy                   memcpy
#define UNUSED(value)                 ((void)(value))
#define __IO                          volatile
#define __STATIC_INLINE               static inline
#define __PACKED                      __attribute__((packed))
#define __ALIGN_BEGIN
#define __ALIGN_END
#endif
