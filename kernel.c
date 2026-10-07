#include "kprint.h"
#include "paging.h"
extern void interrupts_init(void);
void kernel_main(void) {
    kcls();
    kprint("MiniOS: booting...\n");
    interrupts_init();
    paging_init();
    kprint("PAGING: 4 KiB identity map below 4 MiB; null unmapped; WP enabled\n");
    kprint("READY: PIT 100 Hz, PS/2 keyboard enabled\n");
    kprint("Type in the QEMU window. F8/F9/F10 test paging; F11/F12 test exceptions.\n");
    __asm__ volatile ("sti" : : : "memory");
    for (;;) __asm__ volatile ("hlt");
}
