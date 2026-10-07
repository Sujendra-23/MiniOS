#include "frame.h"

#define FRAME_COUNT (FRAME_MEMORY_LIMIT / FRAME_SIZE)
#define BITMAP_WORDS (FRAME_COUNT / 32)
static uint32_t usable[BITMAP_WORDS];
static uint32_t allocated[BITMAP_WORDS];
static uint32_t free_frames;

static bool bit(const uint32_t *map, uint32_t index) {
    return (map[index / 32] & (1u << (index % 32))) != 0;
}
static void set(uint32_t *map, uint32_t index) {
    map[index / 32] |= 1u << (index % 32);
}
static void clear(uint32_t *map, uint32_t index) {
    map[index / 32] &= ~(1u << (index % 32));
}
static uint64_t range_end(uint64_t base, uint64_t length) {
    uint64_t end = length > UINT64_MAX - base ? UINT64_MAX : base + length;
    return end > FRAME_MEMORY_LIMIT ? FRAME_MEMORY_LIMIT : end;
}
void frame_init(void) {
    for (unsigned i = 0; i < BITMAP_WORDS; ++i) {
        usable[i] = 0;
        allocated[i] = 0;
    }
    free_frames = 0;
}
void frame_mark_available(uint64_t base, uint64_t length) {
    if (base >= FRAME_MEMORY_LIMIT || length == 0) return;
    uint32_t first = (uint32_t)((base + FRAME_SIZE - 1) / FRAME_SIZE);
    uint32_t last = (uint32_t)(range_end(base, length) / FRAME_SIZE);
    if (first == 0) first = 1;
    for (uint32_t i = first; i < last; ++i) {
        if (!bit(usable, i)) {
            set(usable, i);
            if (!bit(allocated, i)) ++free_frames;
        }
    }
}
void frame_reserve(uint64_t base, uint64_t length) {
    if (base >= FRAME_MEMORY_LIMIT || length == 0) return;
    uint32_t first = (uint32_t)(base / FRAME_SIZE);
    uint32_t last = (uint32_t)((range_end(base, length) + FRAME_SIZE - 1) / FRAME_SIZE);
    for (uint32_t i = first; i < last; ++i) {
        if (bit(usable, i)) {
            if (!bit(allocated, i)) --free_frames;
            clear(usable, i);
        }
    }
}
uint32_t frame_alloc_below(uint32_t limit) {
    uint32_t last = limit / FRAME_SIZE;
    if (last > FRAME_COUNT) last = FRAME_COUNT;
    for (uint32_t word = 0; word * 32 < last; ++word) {
        uint32_t candidates = usable[word] & ~allocated[word];
        uint32_t remaining = last - word * 32;
        if (remaining < 32) candidates &= (1u << remaining) - 1;
        if (!candidates) continue;
        uint32_t index = word * 32 + (uint32_t)__builtin_ctz(candidates);
        set(allocated, index);
        --free_frames;
        return index * FRAME_SIZE;
    }
    return 0;
}
uint32_t frame_alloc(void) { return frame_alloc_below(FRAME_MEMORY_LIMIT); }
bool frame_is_allocated(uint32_t physical) {
    if (physical == 0 || physical % FRAME_SIZE || physical >= FRAME_MEMORY_LIMIT)
        return false;
    uint32_t index = physical / FRAME_SIZE;
    return bit(usable, index) && bit(allocated, index);
}
bool frame_free(uint32_t physical) {
    if (!frame_is_allocated(physical)) return false;
    clear(allocated, physical / FRAME_SIZE);
    ++free_frames;
    return true;
}
uint32_t frame_free_count(void) { return free_frames; }
