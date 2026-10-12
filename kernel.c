#include "kprint.h"
#include "paging.h"
#include "memory.h"
#include "frame.h"
#include "scheduler.h"
#include "uart.h"
#include "shell.h"
#include "pci.h"
#include "can.h"
#include "net.h"
#include "rt.h"
#include "fmt.h"

static void panic(const char *message) {
    kprint("PANIC: ");
    kprint(message);
    kprint("\n");
    for (;;) __asm__ volatile ("cli; hlt");
}
static void worker(void) {
    /* Never yields or sleeps: progress in all workers requires preemption.
     * A private stack value also checks that each task keeps its own stack. */
    volatile uint32_t marker = 0xC0DE0000u | scheduler_current_task();
    for (;;) {
        if (marker != (0xC0DE0000u | scheduler_current_task()))
            panic("task stack/context corrupted");
        scheduler_record_work();
        __asm__ volatile ("pause");
    }
}
void kernel_main(uint32_t magic, const struct multiboot_info *info) {
    kcls();
    kprint("MiniOS: booting...\n");
    interrupts_init();
    if (uart_init()) kprint("SERIAL: COM1 16550 115200 8N1, IRQ4 RX/TX rings\n");
    else kprint("SERIAL: COM1 not detected\n");
    if (!memory_init(magic, info)) panic("no usable Multiboot memory map");
    kprint("FRAMES: free=");
    kprint_uint(frame_free_count());
    kprint("\n");
    {
        char line[80];
        unsigned found = pci_scan();
        kprint("PCI: ");
        kprint_uint(found);
        kprint(" devices (config ports 0xCF8/0xCFC)\n");
        for (unsigned i = 0; i < found; ++i) {
            const struct pci_device *d = pci_device_at(i);
            char *p = f_str(line, "PCI: ");
            pci_format(d, p);
            while (*p) ++p;
            p = f_str(p, " ");
            p = f_str(p, pci_class_name(d));
            *p++ = '\n'; *p = 0;
            kprint(line);
        }
    }
    paging_init();
    kprint("PAGING: 4 KiB identity map below 4 MiB; null unmapped; WP enabled\n");
    if (!paging_self_test()) panic("dynamic page map/unmap failed");
    kprint("PAGING MAP TEST: PASS\n");
    scheduler_init();
    for (unsigned i = 0; i < 3; ++i)
        if (!scheduler_create_task(worker)) panic("task stack allocation failed");
    if (!rt_init()) panic("real-time task creation failed");
    kprint("RT: 3 periodic tasks (prio 1..3), policy rr; 'sched fp' selects fixed priority\n");
    if (can_init() >= 2) {
        kprint("CAN: 2 x SJA1000 (QEMU kvaser_pci) PeliCAN, io=");
        kprint_hex(can_controller_io(0));
        kprint(",");
        kprint_hex(can_controller_io(1));
        kprint("\n");
    } else {
        kprint("CAN: controllers not present\n");
    }
    if (net_init()) {
        char line[80], *p = f_str(line, "NET: RTL8139 mac=");
        p = f_mac(p, net_context()->mac);
        p = f_str(p, " ip=");
        p = f_ip(p, net_context()->ip);
        *p++ = '\n'; *p = 0;
        kprint(line);
    } else {
        kprint("NET: RTL8139 not present\n");
    }
    kprint("READY: PIT 100 Hz, PS/2 keyboard, round-robin 100 ms quantum\n");
    kprint("Type in QEMU. F8/F9/F10 test paging; F11/F12 test exceptions.\n");
    shell_init();
    __asm__ volatile ("sti" : : : "memory");
    /* Task 0 sleeps until an interrupt, then consumes bytes IRQ4 queued. */
    for (;;) {
        shell_poll();
        net_poll();
        __asm__ volatile ("hlt");
    }
}
