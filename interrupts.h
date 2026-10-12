#ifndef INTERRUPTS_H
#define INTERRUPTS_H
#include <stdint.h>
/* Layout matches pusha, vector/error, and the ring-0 hardware IRET frame. */
struct interrupt_frame {
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t vector, error, eip, cs, eflags;
};
#define PIT_HZ 100u
#define PIT_INPUT_HZ 1193182u
#define PIT_DIVISOR (PIT_INPUT_HZ / PIT_HZ) /* 11931 counts per tick */
void interrupts_init(void);
uint32_t interrupts_ticks(void);
#endif
