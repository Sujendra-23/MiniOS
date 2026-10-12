#include "pci.h"
#include "io.h"
#include "fmt.h"

static struct pci_device devices[PCI_MAX_DEVICES];
static unsigned count;

static uint32_t address(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    return 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
           ((uint32_t)function << 8) | (offset & 0xFCu);
}
uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    outl(0xCF8, address(bus, slot, function, offset));
    return inl(0xCFC);
}
void pci_write32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint32_t value) {
    outl(0xCF8, address(bus, slot, function, offset));
    outl(0xCFC, value);
}
static void probe_function(uint8_t bus, uint8_t slot, uint8_t function, unsigned depth);

static void scan_bus(uint8_t bus, unsigned depth) {
    for (uint8_t slot = 0; slot < 32; ++slot) {
        uint32_t id = pci_read32(bus, slot, 0, 0);
        if ((id & 0xFFFFu) == 0xFFFFu) continue;
        uint8_t header = (pci_read32(bus, slot, 0, 0x0C) >> 16) & 0xFF;
        probe_function(bus, slot, 0, depth);
        if (header & 0x80)
            for (uint8_t function = 1; function < 8; ++function)
                if ((pci_read32(bus, slot, function, 0) & 0xFFFFu) != 0xFFFFu)
                    probe_function(bus, slot, function, depth);
    }
}
static void probe_function(uint8_t bus, uint8_t slot, uint8_t function, unsigned depth) {
    uint32_t id = pci_read32(bus, slot, function, 0);
    uint32_t class_reg = pci_read32(bus, slot, function, 0x08);
    uint32_t header_reg = pci_read32(bus, slot, function, 0x0C);
    uint8_t type = (header_reg >> 16) & 0x7F;
    if (count < PCI_MAX_DEVICES) {
        struct pci_device *d = &devices[count++];
        d->bus = bus; d->slot = slot; d->function = function;
        d->vendor = id & 0xFFFF; d->device = id >> 16;
        d->class_code = class_reg >> 24;
        d->subclass = class_reg >> 16;
        d->prog_if = class_reg >> 8;
        d->header_type = type;
        d->irq_line = pci_read32(bus, slot, function, 0x3C) & 0xFF;
        for (unsigned i = 0; i < 6; ++i)
            d->bar[i] = type == 0 ? pci_read32(bus, slot, function, 0x10 + 4 * i) : 0;
    }
    /* PCI-PCI bridge: follow its secondary bus (bounded depth, no loops). */
    if (type == 1 && depth < 4) {
        uint8_t secondary = (pci_read32(bus, slot, function, 0x18) >> 8) & 0xFF;
        if (secondary > bus) scan_bus(secondary, depth + 1);
    }
}
unsigned pci_scan(void) {
    count = 0;
    scan_bus(0, 0);
    return count;
}
unsigned pci_device_count(void) { return count; }
const struct pci_device *pci_device_at(unsigned index) {
    return index < count ? &devices[index] : 0;
}
const struct pci_device *pci_find(uint16_t vendor, uint16_t device, unsigned index) {
    for (unsigned i = 0; i < count; ++i)
        if (devices[i].vendor == vendor && devices[i].device == device && index-- == 0)
            return &devices[i];
    return 0;
}
void pci_enable(const struct pci_device *dev) {
    uint32_t command = pci_read32(dev->bus, dev->slot, dev->function, 0x04);
    pci_write32(dev->bus, dev->slot, dev->function, 0x04, (command & 0xFFFFu) | 0x7u);
}
void pci_format(const struct pci_device *dev, char *out) {
    char *p = out;
    p = f_hex(p, dev->bus, 2); *p++ = ':';
    p = f_hex(p, dev->slot, 2); *p++ = '.';
    p = f_hex(p, dev->function, 1); *p++ = ' ';
    p = f_hex(p, dev->vendor, 4); *p++ = ':';
    p = f_hex(p, dev->device, 4); *p++ = ' ';
    p = f_hex(p, dev->class_code, 2); *p++ = ':';
    p = f_hex(p, dev->subclass, 2);
    *p = 0;
}
const char *pci_class_name(const struct pci_device *dev) {
    switch (dev->class_code) {
    case 0x01: return "storage";
    case 0x02: return "network";
    case 0x03: return "display";
    case 0x06: return dev->subclass == 0 ? "host-bridge" : dev->subclass == 1 ? "isa-bridge" : "bridge";
    case 0x0C: return dev->subclass == 0x09 ? "canbus" : "serial-bus";
    case 0x0D: return "wireless";
    case 0x07: return "comm";
    case 0x0B: return "processor";
    default: return "other";
    }
}
