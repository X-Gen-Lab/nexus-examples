/** Caller-owned byte break for one explicitly supplied heap interval. */
#ifndef EXAMPLE_HEAP_BOUNDS_H
#define EXAMPLE_HEAP_BOUNDS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uintptr_t begin;
    uintptr_t end;
    uintptr_t current;
    uintptr_t high_water;
} example_heap_bounds_t;

/** Initialize [begin, end), with begin aligned for max_align_t.
 * Empty intervals are valid. Zero begin, reversed bounds and UINTPTR_MAX end
 * are rejected; the latter reserves the conventional failure pointer.
 * The caller supplies real address bounds and serializes all access. No memory
 * is allocated or inspected. Success preserves errno; failure sets EINVAL and
 * leaves the context unchanged. Reinitialization is caller policy and must not
 * invalidate live allocator objects. */
bool example_heap_bounds_init(example_heap_bounds_t* bounds,
                              uintptr_t begin, uintptr_t end);

/** Move by exactly increment bytes and return the previous break.
 * Zero queries the break. Growth past end returns (void*)-1 with ENOMEM;
 * shrink below begin or invalid context returns (void*)-1 with EINVAL.
 * Every failure preserves the entire context, and every success preserves
 * errno. Shrink does not lower high_water. Increment alignment and allocations
 * are owned by the caller; this helper never rounds requests or accesses RAM.
 * It provides no locking, thread/ISR admission or platform lifecycle policy. */
void* example_heap_bounds_adjust(example_heap_bounds_t* bounds,
                                 ptrdiff_t increment);

/** Finite Native contract checks using a local arena; zero means success. */
int example_heap_bounds_self_test(void);

#ifdef __cplusplus
}
#endif
#endif
