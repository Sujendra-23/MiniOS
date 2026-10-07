#include <stdint.h>
#include "io.h"
#include "kprint.h"
#include "interrupts.h"
#include "scheduler.h"
struct idt_gate {
    uint16_t low, selector;
    uint8_t zero, flags;
    uint16_t high;
} __attribute__((packed));
struct idt_pointer {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));
static struct idt_gate idt[256];
extern void (*isr_table[48])(void);
extern void keyboard_handler_c(void);
static uint32_t ticks;
static void idt_set_gate(unsigned vector, void (*handler)(void)) {
    uint32_t address = (uint32_t)handler;
    idt[vector] = (struct idt_gate){
        (uint16_t)address, 0x08, 0, 0x8E, (uint16_t)(address >> 16)
    };
}
static void pic_write(uint16_t port, uint8_t value) {
    outb(port, value);
    io_wait();
}
void interrupts_init(void) {
    for (unsigned i = 0; i < 48; ++i) idt_set_gate(i, isr_table[i]);
    struct idt_pointer ptr = {sizeof(idt) - 1, (uint32_t)idt};
    __asm__ volatile ("lidt %0" : : "m"(ptr) : "memory");
    pic_write(0x21, 0xFF);
    pic_write(0xA1, 0xFF);
    pic_write(0x20, 0x11);
    pic_write(0xA0, 0x11);
    pic_write(0x21, 0x20);
    pic_write(0xA1, 0x28);
    pic_write(0x21, 0x04);
    pic_write(0xA1, 0x02);
    pic_write(0x21, 0x01);
    pic_write(0xA1, 0x01);
    /* PIT channel 0, mode 2, approximately 100 Hz. */
    uint16_t divisor = 1193182 / 100;
    outb(0x43, 0x34);
    outb(0x40, (uint8_t)divisor);
    outb(0x40, (uint8_t)(divisor >> 8));
    pic_write(0x21, 0xFC); /* IRQ0 and IRQ1 only */
    pic_write(0xA1, 0xFF);
}
struct interrupt_frame *interrupt_dispatch(struct interrupt_frame *frame) {
    uint32_t vector = frame->vector;
    if (frame->vector < 32) {
        /* Capture CR2 before printing, so diagnostics cannot overwrite it. */
        uint32_t fault_address = 0;
        if (frame->vector == 14)
            __asm__ volatile ("mov %%cr2, %0" : "=r"(fault_address));
        kprint("\nEXCEPTION vector=");
        kprint_uint(frame->vector);
        kprint(" error=");
        kprint_uint(frame->error);
        kprint(" eip=");
        kprint_uint(frame->eip);
        kprint("\n");
        if (frame->vector == 14) {
            kprint("PAGE FAULT cr2=");
            kprint_hex(fault_address);
            kprint(frame->error & 1 ? " protection" : " not-present");
            kprint(frame->error & 2 ? " write" : " read");
            kprint(frame->error & 4 ? " user" : " supervisor");
            if (frame->error & 8) kprint(" reserved-bit");
            if (frame->error & 16) kprint(" instruction-fetch");
            kprint("\n");
        }
        for (;;) __asm__ volatile ("cli; hlt");
    }
    if (frame->vector == 32) {
        vga_ticks(++ticks);
        if (ticks % 100 == 0) {
            const char *label = "TICK ";
            while (*label) outb(0xE9, *label++);
            char digits[10];
            uint32_t value = ticks;
            unsigned n = 0;
            do { digits[n++] = '0' + value % 10; value /= 10; } while (value);
            while (n) outb(0xE9, digits[--n]);
            outb(0xE9, '\n');
        }
        frame = scheduler_tick(frame, ticks);
    } else if (frame->vector == 33) {
        keyboard_handler_c();
    }
    if (vector >= 40) outb(0xA0, 0x20);
    outb(0x20, 0x20);
    return frame;
}
