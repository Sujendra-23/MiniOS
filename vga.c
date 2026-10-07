#include "kprint.h"
#include "io.h"
static volatile uint16_t *const vga = (volatile uint16_t *)0xB8000;
static unsigned row, col;
static void cell(unsigned index, char ch) { vga[index] = 0x0F00 | (uint8_t)ch; }
void kcls(void) {
    for (unsigned i = 0; i < 80 * 25; ++i) cell(i, ' ');
    row = col = 0;
}
void kputc(char ch) {
    outb(0xE9, (uint8_t)ch);
    if (ch == '\n') { ++row; col = 0; }
    else if (ch == '\b') {
        if (col) { --col; cell(row * 80 + col, ' '); }
    } else if (ch == '\t') {
        do { cell(row * 80 + col++, ' '); } while (col % 8 && col < 80);
    } else { cell(row * 80 + col++, ch); }
    if (col >= 80) { col = 0; ++row; }
    /* Reserve the bottom row for ticks. */
    if (row >= 24) {
        for (unsigned i = 0; i < 23 * 80; ++i) vga[i] = vga[i + 80];
        for (unsigned i = 23 * 80; i < 24 * 80; ++i) cell(i, ' ');
        row = 23;
    }
}
void kprint(const char *s) { while (*s) kputc(*s++); }
void kprint_uint(uint32_t value) {
    char digits[10];
    unsigned n = 0;
    do { digits[n++] = '0' + value % 10; value /= 10; } while (value);
    while (n) kputc(digits[--n]);
}
void vga_ticks(uint32_t ticks) {
    const char *label = "PIT 100 Hz | ticks: ";
    unsigned col = 0;
    while (*label) cell(24 * 80 + col++, *label++);
    char digits[10];
    unsigned n = 0;
    do { digits[n++] = '0' + ticks % 10; ticks /= 10; } while (ticks);
    while (n) cell(24 * 80 + col++, digits[--n]);
    while (col < 80) cell(24 * 80 + col++, ' ');
}

void kprint_hex(uint32_t value) {
    static const char digits[] = "0123456789abcdef";
    kprint("0x");
    for (int shift = 28; shift >= 0; shift -= 4)
        kputc(digits[(value >> shift) & 0xF]);
}

static void status_uint(unsigned *col, uint32_t value) {
    char digits[10];
    unsigned n = 0;
    do { digits[n++] = '0' + value % 10; value /= 10; } while (value);
    while (n) cell(24 * 80 + (*col)++, digits[--n]);
}
void vga_task(uint32_t task, uint32_t switches) {
    unsigned col = 40;
    const char *label = "task: ";
    while (*label) cell(24 * 80 + col++, *label++);
    status_uint(&col, task);
    label = " | switches: ";
    while (*label) cell(24 * 80 + col++, *label++);
    status_uint(&col, switches);
    while (col < 80) cell(24 * 80 + col++, ' ');
}
