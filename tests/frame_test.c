#include <assert.h>
#include <stdio.h>
#include "frame.h"

static void reserve_and_alignment(void) {
    frame_init();
    /* Only pages wholly inside this range may be used. */
    frame_mark_available(0x100001, 4 * FRAME_SIZE - 1);
    assert(frame_free_count() == 3);
    frame_reserve(0x102FFF, 2); /* Both partially overlapping pages are reserved. */
    assert(frame_free_count() == 1);
    assert(frame_alloc() == 0x101000);
    assert(frame_alloc() == 0);
    assert(!frame_free(0x102000));
    assert(!frame_free(0x103000));
    assert(frame_free(0x101000));
    assert(!frame_free(0x101000)); /* Double free. */
    assert(!frame_free(0x101001));
}
static void exhaustion_and_reuse(void) {
    frame_init();
    frame_mark_available(0x100000, 3 * FRAME_SIZE);
    frame_mark_available(0x100000, 3 * FRAME_SIZE); /* No double accounting. */
    assert(frame_free_count() == 3);
    uint32_t a = frame_alloc(), b = frame_alloc(), c = frame_alloc();
    assert(a && b && c && a != b && a != c && b != c);
    assert(frame_free_count() == 0 && frame_alloc() == 0);
    assert(frame_is_allocated(b));
    assert(frame_free(b));
    assert(frame_alloc() == b);
    assert(frame_free(a) && frame_free(b) && frame_free(c));
    assert(frame_free_count() == 3);
}
static void address_limits(void) {
    frame_init();
    frame_mark_available(0, 2 * FRAME_SIZE);
    assert(frame_free_count() == 1); /* Frame zero remains unavailable. */
    assert(frame_alloc_below(FRAME_SIZE) == 0);
    assert(frame_alloc_below(2 * FRAME_SIZE - 1) == 0);
    assert(frame_alloc_below(2 * FRAME_SIZE) == FRAME_SIZE);
    assert(!frame_free(0) && !frame_free(FRAME_MEMORY_LIMIT));
    frame_init();
    frame_mark_available(FRAME_MEMORY_LIMIT - FRAME_SIZE, UINT64_MAX);
    frame_mark_available(UINT64_MAX - 100, 200); /* Ignore ranges above the cap. */
    assert(frame_free_count() == 1);
    assert(frame_alloc_below(FRAME_MEMORY_LIMIT - FRAME_SIZE) == 0);
    assert(frame_alloc() == FRAME_MEMORY_LIMIT - FRAME_SIZE);
    assert(frame_alloc() == 0);
}
static void full_bitmap(void) {
    frame_init();
    frame_mark_available(0, FRAME_MEMORY_LIMIT);
    frame_reserve(0, 0x123001); /* Includes the final partial page. */
    assert(frame_free_count() == (FRAME_MEMORY_LIMIT - 0x124000) / FRAME_SIZE);
    for (uint32_t expected = 0x124000; expected < FRAME_MEMORY_LIMIT; expected += FRAME_SIZE)
        assert(frame_alloc() == expected); /* Cross every bitmap word boundary. */
    assert(frame_alloc() == 0 && frame_free_count() == 0);
    for (uint32_t physical = 0x124000; physical < FRAME_MEMORY_LIMIT; physical += FRAME_SIZE)
        assert(frame_free(physical));
    assert(frame_free_count() == (FRAME_MEMORY_LIMIT - 0x124000) / FRAME_SIZE);
}
int main(void) {
    reserve_and_alignment();
    exhaustion_and_reuse();
    address_limits();
    full_bitmap();
    puts("PASS: frame allocator reservations, alignment, exhaustion, reuse, limits, bitmap");
    return 0;
}
