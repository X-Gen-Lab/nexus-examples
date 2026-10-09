#include "heap_bounds.h"

#include <errno.h>
#include <stdint.h>

static bool valid_bounds(const example_heap_bounds_t* bounds) {
    const uintptr_t alignment = (uintptr_t)_Alignof(max_align_t);
    return bounds && bounds->begin != 0 && bounds->begin <= bounds->end &&
           bounds->end != UINTPTR_MAX && bounds->begin % alignment == 0 &&
           bounds->current >= bounds->begin &&
           bounds->current <= bounds->high_water &&
           bounds->high_water <= bounds->end;
}

bool example_heap_bounds_init(example_heap_bounds_t* bounds,
                              uintptr_t begin, uintptr_t end) {
    const uintptr_t alignment = (uintptr_t)_Alignof(max_align_t);
    if (!bounds || begin == 0 || begin > end || end == UINTPTR_MAX ||
        begin % alignment != 0) {
        errno = EINVAL;
        return false;
    }
    bounds->begin = begin;
    bounds->end = end;
    bounds->current = begin;
    bounds->high_water = begin;
    return true;
}

void* example_heap_bounds_adjust(example_heap_bounds_t* bounds,
                                 ptrdiff_t increment) {
    if (!valid_bounds(bounds)) {
        errno = EINVAL;
        return (void*)-1;
    }

    const uintptr_t previous = bounds->current;
    uintptr_t next;
    if (increment >= 0) {
        /* Compare in uintmax_t before narrowing: uintptr_t and ptrdiff_t need
         * not have the same width. Subtraction is safe after bounds checks. */
        const uintmax_t amount = (uintmax_t)increment;
        if (amount > (uintmax_t)(bounds->end - previous)) {
            errno = ENOMEM;
            return (void*)-1;
        }
        next = previous + (uintptr_t)amount;
    } else {
        /* PTRDIFF_MIN cannot be negated. increment+1 and its negation fit the
         * signed domain; only the final +1 occurs in the unsigned domain. */
        const uintmax_t amount = (uintmax_t)(-(increment + 1)) + UINTMAX_C(1);
        if (amount > (uintmax_t)(previous - bounds->begin)) {
            errno = EINVAL;
            return (void*)-1;
        }
        next = previous - (uintptr_t)amount;
    }

    bounds->current = next;
    if (next > bounds->high_water) {
        bounds->high_water = next;
    }
    return (void*)previous;
}
