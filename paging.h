#ifndef PAGING_H
#define PAGING_H
#include <stdint.h>
#include <stdbool.h>

#define PAGING_IDENTITY_LIMIT 0x00400000u

/* 4 KiB supervisor pages; page zero and addresses >= 4 MiB stay unmapped. */
void paging_init(void);
/* Dynamic supervisor mappings above the identity window. Frames belong to the
 * caller; empty page tables are released automatically on unmap. */
bool paging_map(uint32_t virtual_address, uint32_t physical, bool writable);
uint32_t paging_unmap(uint32_t virtual_address);
bool paging_self_test(void);
/* Deliberate faults for the QEMU smoke test and interactive debugging. */
void paging_test_unmapped(void);
void paging_test_readonly(void);
void paging_test_null(void);
#endif
