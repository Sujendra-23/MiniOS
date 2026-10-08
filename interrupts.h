#ifndef INTERRUPTS_H
#define INTERRUPTS_H
#include <stdint.h>
/* Layout matches pusha, vector/error, and the ring-0 hardware IRET frame. */
struct interrupt_frame {
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t vector, error, eip, cs, eflags;
};
void interrupts_init(void);
uint32_t interrupts_ticks(void);
#endif
