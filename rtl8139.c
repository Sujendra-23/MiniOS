#include "rtl8139.h"
#include "pci.h"
#include "io.h"

#define IDR0 0x00
#define TSD0 0x10
#define TSAD0 0x20
#define RBSTART 0x30
#define CMD 0x37
#define CAPR 0x38
#define IMR 0x3C
#define TCR 0x40
#define RCR 0x44
#define CONFIG1 0x52

#define CMD_BUFE 0x01
#define CMD_TE 0x04
#define CMD_RE 0x08
#define CMD_RST 0x10
#define TSD_TOK 0x8000u
#define RX_ROK 0x0001u

#define RX_RING 8192u
#define RX_BUFFER (RX_RING + 16u + 2048u) /* WRAP lets the last packet overrun */
#define TX_SLOTS 4u
#define TX_SIZE 1792u
#define SPIN_LIMIT 200000u

static uint8_t rx_buffer[RX_BUFFER] __attribute__((aligned(16)));
static uint8_t tx_buffer[TX_SLOTS][TX_SIZE] __attribute__((aligned(16)));
static uint16_t base;
static uint32_t rx_offset;
static unsigned tx_slot;

bool rtl8139_init(uint8_t mac[6]) {
    const struct pci_device *dev = pci_find(0x10EC, 0x8139, 0);
    if (!dev || !(dev->bar[0] & 1)) return false;
    pci_enable(dev);
    base = (uint16_t)(dev->bar[0] & ~3u);
    outb(base + CONFIG1, 0x00);              /* power on */
    outb(base + CMD, CMD_RST);
    unsigned spin = 0;
    while (inb(base + CMD) & CMD_RST)
        if (++spin > SPIN_LIMIT) return false;
    for (unsigned i = 0; i < 6; ++i) mac[i] = inb(base + IDR0 + i);
    for (unsigned i = 0; i < RX_BUFFER; ++i) rx_buffer[i] = 0;
    rx_offset = 0;
    tx_slot = 0;
    outl(base + RBSTART, (uint32_t)rx_buffer);
    outw(base + IMR, 0x0000);                /* polled: no interrupts */
    outb(base + CMD, CMD_RE | CMD_TE);
    /* Accept broadcast, multicast, and unicast-to-us; WRAP; unlimited DMA burst. */
    outl(base + RCR, 0x0000000Eu | (1u << 7) | (7u << 8));
    outl(base + TCR, (3u << 24) | (7u << 8));
    outw(base + CAPR, 0xFFF0);
    return true;
}
bool rtl8139_send(const uint8_t *frame, size_t length) {
    if (!base || length > 1514) return false;
    unsigned slot = tx_slot;
    tx_slot = (tx_slot + 1) % TX_SLOTS;
    for (size_t i = 0; i < length; ++i) tx_buffer[slot][i] = frame[i];
    size_t padded = length < 60 ? 60 : length; /* the NIC does not pad runts */
    for (size_t i = length; i < padded; ++i) tx_buffer[slot][i] = 0;
    outl(base + TSAD0 + 4 * slot, (uint32_t)tx_buffer[slot]);
    outl(base + TSD0 + 4 * slot, (uint32_t)padded); /* OWN=0 starts the DMA */
    unsigned spin = 0;
    while (!(inl(base + TSD0 + 4 * slot) & TSD_TOK))
        if (++spin > SPIN_LIMIT) return false;
    return true;
}
bool rtl8139_has_frame(void) { return base && !(inb(base + CMD) & CMD_BUFE); }
size_t rtl8139_recv(uint8_t *buf, size_t max) {
    if (!base || (inb(base + CMD) & CMD_BUFE)) return 0;
    const uint8_t *header = rx_buffer + rx_offset;
    uint16_t status = (uint16_t)(header[0] | (header[1] << 8));
    uint16_t length = (uint16_t)(header[2] | (header[3] << 8)); /* includes 4-byte CRC */
    size_t result = 0;
    if ((status & RX_ROK) && length >= 4 + 14 && length <= 1518 + 4) {
        result = length - 4u;
        if (result > max) result = max;
        for (size_t i = 0; i < result; ++i) buf[i] = header[4 + i];
    }
    rx_offset = (rx_offset + length + 4u + 3u) & ~3u;
    if (rx_offset >= RX_RING) rx_offset -= RX_RING;
    outw(base + CAPR, (uint16_t)(rx_offset - 16u));
    return result;
}
