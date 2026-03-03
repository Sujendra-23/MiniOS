cat > kernel.c <<'EOF'
#include <stdint.h>
#include "kprint.h"

extern void install_idt_and_pic(void);
extern void irq1_handler_asm(void);

void kernel_main(void) {
    kcls();
    kprint("MiniOS: booting...\n");
    kprint("Installing IDT and remapping PIC...\n");
    install_idt_and_pic();
    kprint("Keyboard handler installed. Press keys in QEMU window.\n");
    kprint("Type to see characters echoed. Press Ctrl+Alt+G to exit QEMU.\n");

    asm volatile ("sti");

    while (1) {
        asm volatile ("hlt");
    }
}
EOF