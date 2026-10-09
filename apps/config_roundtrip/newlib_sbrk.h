/** Application-owned newlib heap; this example has exactly one allocator owner. */
#ifndef EXAMPLE_NEWLIB_SBRK_H
#define EXAMPLE_NEWLIB_SBRK_H

#include <stdbool.h>
#include <stdint.h>

/** Bind once from baremetal main or the scheduled Configuration worker.
 * Every malloc/calloc/realloc/free in this firmware belongs to that same owner.
 * This does not install newlib locks or permit allocation from other tasks.
 */
bool example_newlib_heap_bind(void);

/** Debugger-visible break diagnostics, not malloc live bytes or stack peaks. */
extern volatile uintptr_t config_example_heap_begin;
extern volatile uintptr_t config_example_heap_end;
extern volatile uintptr_t config_example_heap_break;
extern volatile uintptr_t config_example_heap_high_water;

#endif
