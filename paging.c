#include "paging.h"

enum {
    PAGE_SIZE = 4096,
    PAGE_ENTRIES = 1024,
    PAGE_PRESENT = 1,
    PAGE_WRITABLE = 2,
    CR0_WP = 1u << 16,
    CR0_PG = 1u << 31
};
static uint32_t page_directory[PAGE_ENTRIES] __attribute__((aligned(PAGE_SIZE)));
static uint32_t page_table[PAGE_ENTRIES] __attribute__((aligned(PAGE_SIZE)));
extern char __text_start[], __text_end[], __rodata_start[], __rodata_end[];

static void protect_range(uint32_t start, uint32_t end) {
    for (uint32_t address = start; address < end; address += PAGE_SIZE)
        page_table[address / PAGE_SIZE] &= ~PAGE_WRITABLE;
}

void paging_init(void) {
    /* All other PDEs remain zero in BSS. No PAE or large pages are required. */
    for (uint32_t i = 1; i < PAGE_ENTRIES; ++i)
        page_table[i] = i * PAGE_SIZE | PAGE_PRESENT | PAGE_WRITABLE;
    protect_range((uint32_t)__text_start, (uint32_t)__text_end);
    protect_range((uint32_t)__rodata_start, (uint32_t)__rodata_end);
    page_directory[0] = (uint32_t)page_table | PAGE_PRESENT | PAGE_WRITABLE;

    /* Keep the current code, stack, GDT, IDT, page tables and VGA mapped. */
    __asm__ volatile ("mov %0, %%cr3" : : "r"(page_directory) : "memory");
    uint32_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= CR0_PG | CR0_WP; /* Enforce read-only PTEs even in ring 0. */
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0) : "memory");
}

void paging_test_unmapped(void) {
    /* Inline assembly avoids undefined C pointer dereferences in fault probes. */
    __asm__ volatile ("movl (%0), %%eax" : : "r"(PAGING_IDENTITY_LIMIT) : "eax", "memory");
}
void paging_test_readonly(void) {
    __asm__ volatile ("movb $0, (%0)" : : "r"(__text_start) : "memory");
}
void paging_test_null(void) {
    __asm__ volatile ("movl (%0), %%eax" : : "r"(0u) : "eax", "memory");
}
