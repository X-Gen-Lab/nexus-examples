#include "heap_bounds.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>

enum {
    HEAP_TEST_CAPACITY = 128,
    HEAP_TEST_GUARD = _Alignof(max_align_t),
    HEAP_TEST_BYTES = HEAP_TEST_CAPACITY + 2 * HEAP_TEST_GUARD
};

/* The union aligns the byte arena. A whole alignment unit precedes the heap,
 * so the heap starts aligned while both canaries remain directly adjacent. */
typedef union {
    max_align_t alignment;
    unsigned char bytes[HEAP_TEST_BYTES];
} heap_test_arena_t;

static bool same_state(const example_heap_bounds_t* a,
                       const example_heap_bounds_t* b) {
    return a->begin == b->begin && a->end == b->end &&
           a->current == b->current && a->high_water == b->high_water;
}

static bool canaries_intact(const heap_test_arena_t* arena) {
    for (size_t i = 0; i < HEAP_TEST_GUARD; ++i) {
        if (arena->bytes[i] != 0xA5 ||
            arena->bytes[HEAP_TEST_GUARD + HEAP_TEST_CAPACITY + i] != 0xA5) {
            return false;
        }
    }
    return true;
}

/* Checks remain active in Release/NDEBUG builds. The line identifies failure
 * without introducing stdout, abort(), allocator or assertion dependencies. */
#define HEAP_CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

int example_heap_bounds_self_test(void) {
    heap_test_arena_t arena;
    memset(arena.bytes, 0xA5, sizeof(arena.bytes));
    unsigned char* const memory = arena.bytes + HEAP_TEST_GUARD;
    const uintptr_t begin = (uintptr_t)memory;
    const uintptr_t end = (uintptr_t)(memory + HEAP_TEST_CAPACITY);
    example_heap_bounds_t bounds = {0};
    errno = ERANGE;
    HEAP_CHECK(example_heap_bounds_init(&bounds, begin, end));
    HEAP_CHECK(errno == ERANGE && bounds.begin == begin && bounds.end == end);
    HEAP_CHECK(bounds.current == begin && bounds.high_water == begin);
    HEAP_CHECK(example_heap_bounds_adjust(&bounds, 0) == memory);
    HEAP_CHECK(errno == ERANGE && canaries_intact(&arena));

    /* Failed initialization leaves an existing valid context unchanged. */
    example_heap_bounds_t saved = bounds;
    HEAP_CHECK(!example_heap_bounds_init(NULL, begin, end) && errno == EINVAL);
    HEAP_CHECK(!example_heap_bounds_init(&bounds, end, begin) && errno == EINVAL);
    HEAP_CHECK(same_state(&bounds, &saved));
    HEAP_CHECK(!example_heap_bounds_init(&bounds, 0, end) && errno == EINVAL);
    HEAP_CHECK(same_state(&bounds, &saved));
    HEAP_CHECK(!example_heap_bounds_init(&bounds, begin, UINTPTR_MAX) &&
               errno == EINVAL);
    HEAP_CHECK(same_state(&bounds, &saved));
    if (_Alignof(max_align_t) > 1) {
        HEAP_CHECK(!example_heap_bounds_init(&bounds, begin + 1, end) &&
                   errno == EINVAL);
        HEAP_CHECK(same_state(&bounds, &saved));
    }

    errno = ERANGE;
    unsigned char* block = example_heap_bounds_adjust(&bounds, 17);
    HEAP_CHECK(block == memory && errno == ERANGE);
    HEAP_CHECK(bounds.current == begin + 17 && bounds.high_water == begin + 17);
    memset(block, 0x31, 17);
    HEAP_CHECK(canaries_intact(&arena));
    block = example_heap_bounds_adjust(&bounds, HEAP_TEST_CAPACITY - 17);
    HEAP_CHECK(block == memory + 17 && errno == ERANGE);
    memset(block, 0x62, HEAP_TEST_CAPACITY - 17);
    HEAP_CHECK(bounds.current == end && bounds.high_water == end);
    HEAP_CHECK(canaries_intact(&arena));
    for (size_t i = 0; i < HEAP_TEST_CAPACITY; ++i) {
        HEAP_CHECK(memory[i] == (i < 17 ? 0x31 : 0x62));
    }

    /* Exactly full is legal; every larger request fails without wraparound. */
    saved = bounds;
    HEAP_CHECK(example_heap_bounds_adjust(&bounds, 1) == (void*)-1);
    HEAP_CHECK(errno == ENOMEM && same_state(&bounds, &saved));
    HEAP_CHECK(example_heap_bounds_adjust(&bounds, PTRDIFF_MAX) == (void*)-1);
    HEAP_CHECK(errno == ENOMEM && same_state(&bounds, &saved));
    errno = ERANGE;
    HEAP_CHECK(example_heap_bounds_adjust(&bounds, 0) == memory + HEAP_TEST_CAPACITY);
    HEAP_CHECK(errno == ERANGE && same_state(&bounds, &saved));

    HEAP_CHECK(example_heap_bounds_adjust(&bounds, -1) == memory + HEAP_TEST_CAPACITY);
    HEAP_CHECK(errno == ERANGE && bounds.current == end - 1);
    HEAP_CHECK(bounds.high_water == end);
    block = example_heap_bounds_adjust(&bounds, 1);
    HEAP_CHECK(block == memory + HEAP_TEST_CAPACITY - 1 && errno == ERANGE);
    *block = 0x73;
    HEAP_CHECK(memory[HEAP_TEST_CAPACITY - 1] == 0x73 && canaries_intact(&arena));
    saved = bounds;
    HEAP_CHECK(example_heap_bounds_adjust(&bounds, -(HEAP_TEST_CAPACITY + 1)) ==
               (void*)-1);
    HEAP_CHECK(errno == EINVAL && same_state(&bounds, &saved));
    HEAP_CHECK(example_heap_bounds_adjust(&bounds, PTRDIFF_MIN) == (void*)-1);
    HEAP_CHECK(errno == EINVAL && same_state(&bounds, &saved));
    errno = ERANGE;
    HEAP_CHECK(example_heap_bounds_adjust(&bounds, -HEAP_TEST_CAPACITY) ==
               memory + HEAP_TEST_CAPACITY);
    HEAP_CHECK(errno == ERANGE && bounds.current == begin && bounds.high_water == end);
    saved = bounds;
    HEAP_CHECK(example_heap_bounds_adjust(&bounds, -1) == (void*)-1);
    HEAP_CHECK(errno == EINVAL && same_state(&bounds, &saved));
    errno = ERANGE;
    block = example_heap_bounds_adjust(&bounds, HEAP_TEST_CAPACITY);
    HEAP_CHECK(block == memory && errno == ERANGE);
    memset(block, 0x84, HEAP_TEST_CAPACITY);
    HEAP_CHECK(bounds.current == end && bounds.high_water == end);
    HEAP_CHECK(canaries_intact(&arena));

    /* Empty heap and invalid context cannot manufacture allocatable storage. */
    HEAP_CHECK(example_heap_bounds_init(&bounds, begin, begin));
    HEAP_CHECK(errno == ERANGE);
    HEAP_CHECK(example_heap_bounds_adjust(&bounds, 0) == memory && errno == ERANGE);
    saved = bounds;
    HEAP_CHECK(example_heap_bounds_adjust(&bounds, 1) == (void*)-1);
    HEAP_CHECK(errno == ENOMEM && same_state(&bounds, &saved));
    HEAP_CHECK(example_heap_bounds_adjust(NULL, 0) == (void*)-1 && errno == EINVAL);
    bounds.current = end;
    saved = bounds;
    HEAP_CHECK(example_heap_bounds_adjust(&bounds, 0) == (void*)-1);
    HEAP_CHECK(errno == EINVAL && same_state(&bounds, &saved));

    /* Address-only boundary case near UINTPTR_MAX: there is deliberately no
     * memory dereference. A signed maximum request must be checked before add. */
    const uintptr_t limit = UINTPTR_MAX - 1;
    const uintptr_t alignment = (uintptr_t)_Alignof(max_align_t);
    const uintptr_t near_limit = limit - limit % alignment;
    HEAP_CHECK(example_heap_bounds_init(&bounds, near_limit, limit));
    saved = bounds;
    HEAP_CHECK(example_heap_bounds_adjust(&bounds, PTRDIFF_MAX) == (void*)-1);
    HEAP_CHECK(errno == ENOMEM && same_state(&bounds, &saved));
    HEAP_CHECK(example_heap_bounds_adjust(&bounds, PTRDIFF_MIN) == (void*)-1);
    HEAP_CHECK(errno == EINVAL && same_state(&bounds, &saved));
    HEAP_CHECK(canaries_intact(&arena));
    return 0;
}
