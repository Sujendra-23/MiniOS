#ifndef PAGING_H
#define PAGING_H
#include <stdint.h>

#define PAGING_IDENTITY_LIMIT 0x00400000u

/* 4 KiB supervisor pages; page zero and addresses >= 4 MiB stay unmapped. */
void paging_init(void);
/* Deliberate faults for the QEMU smoke test and interactive debugging. */
void paging_test_unmapped(void);
void paging_test_readonly(void);
void paging_test_null(void);
#endif
