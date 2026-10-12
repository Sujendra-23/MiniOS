#ifndef PCI_H
#define PCI_H
#include <stdbool.h>
#include <stdint.h>

/* PCI configuration mechanism #1: address port 0xCF8, data port 0xCFC.
 * pci_scan() walks bus 0 and any bus behind a PCI-PCI bridge. */
#define PCI_MAX_DEVICES 32u

struct pci_device {
    uint8_t bus, slot, function;
    uint16_t vendor, device;
    uint8_t class_code, subclass, prog_if, header_type, irq_line;
    uint32_t bar[6];
};

uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset);
void pci_write32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint32_t value);
unsigned pci_scan(void);
unsigned pci_device_count(void);
const struct pci_device *pci_device_at(unsigned index);
/* index selects among several devices with the same ID; NULL if absent. */
const struct pci_device *pci_find(uint16_t vendor, uint16_t device, unsigned index);
/* Enable I/O decoding, memory decoding, and bus mastering. */
void pci_enable(const struct pci_device *dev);
/* Format "bb:ss.f vvvv:dddd cc:ss" into out (>= 32 bytes). */
void pci_format(const struct pci_device *dev, char *out);
const char *pci_class_name(const struct pci_device *dev);
#endif
