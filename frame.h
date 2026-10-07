#ifndef FRAME_H
#define FRAME_H
#include <stdbool.h>
#include <stdint.h>

#define FRAME_SIZE 4096u
#define FRAME_MEMORY_LIMIT (128u * 1024u * 1024u)

/* Single-core allocator: callers must exclude interrupts while modifying it.
 * Init starts with all frames reserved. Only complete available pages become
 * usable. Reservations cover any overlapping page; frame zero stays reserved.
 * Allocation returns a physical address, or zero on exhaustion. */
void frame_init(void);
void frame_mark_available(uint64_t base, uint64_t length);
void frame_reserve(uint64_t base, uint64_t length);
uint32_t frame_alloc(void);
uint32_t frame_alloc_below(uint32_t limit);
bool frame_free(uint32_t physical);
bool frame_is_allocated(uint32_t physical);
uint32_t frame_free_count(void);
#endif
