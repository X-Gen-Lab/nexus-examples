/** Strong application-owned newlib syscall for the bounded Config example. */
#include "nexus_config.h"
#include "arch/nx_arch.h"
#include "osal/osal.h"
#include "heap_bounds.h"
#include "newlib_sbrk.h"
#include <errno.h>
#include <stddef.h>

#if defined(NX_CONFIG_PLATFORM_NATIVE)
#error "The hosted example must use its host C runtime, not this newlib adapter"
#endif

/* Both maintained GNU linker section files place `_end` at the aligned start
 * of ._user_heap_stack. `_Min_Heap_Size` is an absolute size symbol; its address
 * is the reserved byte count, not storage to read. There is no exported heap
 * end symbol. Derive the exclusive bound from these actual linker definitions.
 */
extern unsigned char _end[];
extern unsigned char _Min_Heap_Size[];

static example_heap_bounds_t heap;
static bool bound;
static osal_task_handle_t owner;
volatile uintptr_t config_example_heap_begin;
volatile uintptr_t config_example_heap_end;
volatile uintptr_t config_example_heap_break;
volatile uintptr_t config_example_heap_high_water;

static bool allowed_context(void) {
    if (nx_arch_in_isr() || nx_arch_irq_is_masked()) {
        errno = EPERM;
        return false;
    }
    return true;
}

bool example_newlib_heap_bind(void) {
    if (!allowed_context()) return false;
    if (bound) { errno = EBUSY; return false; }
    osal_task_handle_t current = osal_task_get_current();
#if defined(NX_CONFIG_OSAL_FREERTOS)
    /* Bind inside the already scheduled Configuration worker, never main. */
    if (!current) { errno = EPERM; return false; }
#endif
    uintptr_t begin = (uintptr_t)_end;
    uintptr_t size = (uintptr_t)_Min_Heap_Size;
    if (size < 8192U || size != (uintptr_t)NX_CONFIG_FIRMWARE_LIBC_HEAP_SIZE ||
        begin > UINTPTR_MAX - size) {
        errno = EINVAL;
        return false;
    }
    if (!example_heap_bounds_init(&heap, begin, begin + size)) return false;
    owner = current;
    bound = true;
    config_example_heap_begin = begin;
    config_example_heap_end = begin + size;
    config_example_heap_break = begin;
    config_example_heap_high_water = begin;
    return true;
}

/* Newlib calls this through _sbrk_r, which propagates errno into its reentrant
 * state. There is no _write retarget, libc multi-task locking or Flash use.
 * Context rejection is a guard; the app must also serialize free and all
 * malloc-family calls, since a freed existing block need not call _sbrk.
 */
void* _sbrk(ptrdiff_t increment) {
    if (!allowed_context()) return (void*)-1;
    if (!bound || osal_task_get_current() != owner) {
        errno = EPERM;
        return (void*)-1;
    }
    void* previous = example_heap_bounds_adjust(&heap, increment);
    config_example_heap_break = heap.current;
    config_example_heap_high_water = heap.high_water;
    return previous;
}
