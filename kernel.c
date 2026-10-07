#include "kprint.h"
#include "paging.h"
#include "memory.h"
#include "frame.h"
#include "scheduler.h"

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
    if (!memory_init(magic, info)) panic("no usable Multiboot memory map");
    kprint("FRAMES: free=");
    kprint_uint(frame_free_count());
    kprint("\n");
    paging_init();
    kprint("PAGING: 4 KiB identity map below 4 MiB; null unmapped; WP enabled\n");
    if (!paging_self_test()) panic("dynamic page map/unmap failed");
    kprint("PAGING MAP TEST: PASS\n");
    scheduler_init();
    for (unsigned i = 0; i < 3; ++i)
        if (!scheduler_create_task(worker)) panic("task stack allocation failed");
    kprint("READY: PIT 100 Hz, PS/2 keyboard, round-robin 100 ms quantum\n");
    kprint("Type in QEMU. F8/F9/F10 test paging; F11/F12 test exceptions.\n");
    __asm__ volatile ("sti" : : : "memory");
    for (;;) __asm__ volatile ("hlt");
}
