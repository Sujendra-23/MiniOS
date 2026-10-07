#include "paging.h"
#include "frame.h"

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

static uint32_t *table_for(uint32_t virtual_address) {
    uint32_t directory_entry = page_directory[virtual_address >> 22];
    if (!(directory_entry & PAGE_PRESENT)) return 0;
    return (uint32_t *)(directory_entry & 0xFFFFF000u);
}
bool paging_map(uint32_t virtual_address, uint32_t physical, bool writable) {
    if (virtual_address < PAGING_IDENTITY_LIMIT || virtual_address % PAGE_SIZE ||
        !frame_is_allocated(physical)) return false;
    uint32_t *table = table_for(virtual_address);
    if (!table) {
        /* Page-table frames need their permanent low identity mapping. */
        uint32_t table_frame = frame_alloc_below(PAGING_IDENTITY_LIMIT);
        if (!table_frame) return false;
        table = (uint32_t *)table_frame;
        for (unsigned i = 0; i < PAGE_ENTRIES; ++i) table[i] = 0;
        page_directory[virtual_address >> 22] = table_frame | PAGE_PRESENT | PAGE_WRITABLE;
    }
    unsigned index = (virtual_address >> 12) & 0x3FF;
    if (table[index] & PAGE_PRESENT) return false;
    table[index] = physical | PAGE_PRESENT | (writable ? PAGE_WRITABLE : 0);
    __asm__ volatile ("invlpg (%0)" : : "r"(virtual_address) : "memory");
    return true;
}
uint32_t paging_unmap(uint32_t virtual_address) {
    if (virtual_address < PAGING_IDENTITY_LIMIT || virtual_address % PAGE_SIZE)
        return 0;
    uint32_t *table = table_for(virtual_address);
    unsigned index = (virtual_address >> 12) & 0x3FF;
    if (!table || !(table[index] & PAGE_PRESENT)) return 0;
    uint32_t physical = table[index] & 0xFFFFF000u;
    table[index] = 0;
    __asm__ volatile ("invlpg (%0)" : : "r"(virtual_address) : "memory");
    for (unsigned i = 0; i < PAGE_ENTRIES; ++i)
        if (table[i] & PAGE_PRESENT) return physical;
    page_directory[virtual_address >> 22] = 0;
    __asm__ volatile ("mov %0, %%cr3" : : "r"(page_directory) : "memory");
    frame_free((uint32_t)table);
    return physical;
}
bool paging_self_test(void) {
    uint32_t before = frame_free_count();
    uint32_t frame = frame_alloc();
    if (!frame) return false;
    if (!paging_map(0x40000000u, frame, true)) { frame_free(frame); return false; }
    volatile uint32_t *probe = (volatile uint32_t *)0x40000000u;
    *probe = 0x1234ABCDu;
    bool valid = *probe == 0x1234ABCDu &&
        !paging_map(0x40000000u, frame, true);
    uint32_t released = paging_unmap(0x40000000u);
    if (released != frame) return false;
    return frame_free(frame) && valid && frame_free_count() == before &&
        paging_unmap(0x40000000u) == 0;
}
