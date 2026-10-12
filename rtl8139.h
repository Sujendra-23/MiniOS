#ifndef RTL8139_H
#define RTL8139_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Polled driver for the Realtek RTL8139 NIC as emulated by QEMU
 * (PCI 10ec:8139). I/O BAR0, 8 KiB receive ring in kernel BSS (the kernel is
 * identity-mapped, so virtual == physical for DMA), four transmit descriptors
 * used synchronously. Emulated hardware only. */
bool rtl8139_init(uint8_t mac[6]);
bool rtl8139_send(const uint8_t *frame, size_t length);
bool rtl8139_has_frame(void);
/* Copies one received frame (without CRC) into buf; returns its length or 0. */
size_t rtl8139_recv(uint8_t *buf, size_t max);
#endif
