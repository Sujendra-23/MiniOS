#include "can.h"
#include "pci.h"
#include "io.h"
#include "kprint.h"

#define KVASER_VENDOR 0x10E8u
#define KVASER_DEVICE 0x8406u
#define SJA_BAR 1u

/* SJA1000 registers (PeliCAN mode). */
#define MOD 0
#define CMR 1
#define SR 2
#define IER 4
#define BTR0 6
#define BTR1 7
#define OCR 8
#define BUF 16 /* TX buffer on write, RX FIFO window on read */
#define ACR0 16
#define AMR0 20
#define CDR 31
#define SPIN_LIMIT 20000u

static uint16_t io[CAN_MAX_CONTROLLERS];
static unsigned controllers;

static uint8_t reg_read(unsigned c, unsigned reg) { return inb(io[c] + reg); }
static void reg_write(unsigned c, unsigned reg, uint8_t v) { outb(io[c] + reg, v); }

static bool controller_init(unsigned c) {
    reg_write(c, MOD, 0x01);          /* reset mode */
    reg_write(c, CDR, 0x80);          /* PeliCAN, only writable in reset mode */
    if (!(reg_read(c, CDR) & 0x80)) return false;
    reg_write(c, IER, 0x00);          /* polled */
    for (unsigned i = 0; i < 4; ++i) {
        reg_write(c, ACR0 + i, 0x00);
        reg_write(c, AMR0 + i, 0xFF); /* accept every ID */
    }
    reg_write(c, BTR0, 0x00);         /* nominal 500 kbit/s at 16 MHz; QEMU ignores timing */
    reg_write(c, BTR1, 0x1C);
    reg_write(c, OCR, 0x1A);
    reg_write(c, MOD, 0x00);          /* leave reset mode */
    for (unsigned i = 0; i < SPIN_LIMIT; ++i)
        if (!(reg_read(c, MOD) & 0x01)) return true;
    return false;
}
unsigned can_init(void) {
    controllers = 0;
    for (unsigned i = 0; i < CAN_MAX_CONTROLLERS; ++i) {
        const struct pci_device *dev = pci_find(KVASER_VENDOR, KVASER_DEVICE, i);
        if (!dev || !(dev->bar[SJA_BAR] & 1)) break; /* need an I/O BAR */
        pci_enable(dev);
        io[controllers] = (uint16_t)(dev->bar[SJA_BAR] & ~3u);
        if (!controller_init(controllers)) break;
        ++controllers;
    }
    return controllers;
}
unsigned can_controller_count(void) { return controllers; }
uint16_t can_controller_io(unsigned index) { return index < controllers ? io[index] : 0; }

bool can_send(unsigned c, const struct can_frame *frame) {
    uint8_t packed[CAN_PACKED_MAX];
    unsigned n = can_frame_pack(frame, packed);
    if (c >= controllers || !n) return false;
    unsigned spin = 0;
    while (!(reg_read(c, SR) & 0x04)) /* TBS: transmit buffer free */
        if (++spin > SPIN_LIMIT) return false;
    for (unsigned i = 0; i < n; ++i) reg_write(c, BUF + i, packed[i]);
    reg_write(c, CMR, 0x01);          /* transmission request */
    return true;
}
bool can_receive(unsigned c, struct can_frame *frame) {
    if (c >= controllers || !(reg_read(c, SR) & 0x01)) return false; /* RBS */
    uint8_t packed[CAN_PACKED_MAX];
    packed[0] = reg_read(c, BUF);
    unsigned n = can_packed_length(packed[0]);
    for (unsigned i = 1; i < n; ++i) packed[i] = reg_read(c, BUF + i);
    reg_write(c, CMR, 0x04);          /* release receive buffer */
    return can_frame_unpack(packed, n, frame);
}

static void make_frame(uint32_t i, struct can_frame *f) {
    f->extended = i % 5 == 0;
    f->rtr = i % 7 == 3;
    f->id = (i * 0x9E3779B1u >> 3) & (f->extended ? CAN_EFF_MASK : CAN_SFF_MASK);
    f->dlc = f->rtr ? 0 : (uint8_t)(i % 9); /* RTR with DLC>0 desynced QEMU 8.2.2's RX FIFO (see README) */
    for (unsigned j = 0; j < 8; ++j) f->data[j] = f->rtr ? 0 : (uint8_t)(i * 31 + j * 17 + 0xA5);
}
struct can_selftest_result can_selftest(unsigned frames) {
    struct can_selftest_result r = {0, 0, 0, 0, 0};
    if (controllers < 2) return r;
    for (uint32_t i = 0; i < frames; ++i) {
        unsigned from = i & 1, to = from ^ 1;
        struct can_frame tx, rx;
        make_frame(i, &tx);
        if (!can_send(from, &tx)) { ++r.tx_timeouts; continue; }
        ++r.sent;
        unsigned spin = 0;
        while (!can_receive(to, &rx))
            if (++spin > SPIN_LIMIT) break;
        if (spin > SPIN_LIMIT) {
            if (r.rx_timeouts++ < 4) { kprint("CAN RX TIMEOUT i="); kprint_uint(i); kprint("\n"); }
            continue;
        }
        ++r.received;
        if (!can_frame_equal(&tx, &rx)) {
            if (r.mismatches++ < 4) { /* first few diffs go to the debug console */
                kprint("CAN MISMATCH i="); kprint_uint(i);
                kprint(" tx id="); kprint_hex(tx.id); kprint(" dlc="); kprint_uint(tx.dlc);
                kprint(tx.extended ? " ext" : " std"); kprint(tx.rtr ? " rtr" : "");
                kprint(" rx id="); kprint_hex(rx.id); kprint(" dlc="); kprint_uint(rx.dlc);
                kprint(rx.extended ? " ext" : " std"); kprint(rx.rtr ? " rtr\n" : "\n");
            }
        }
    }
    return r;
}
