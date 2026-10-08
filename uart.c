#include "uart.h"
#include "io.h"

#define COM1 0x3F8u
#define RBR (COM1 + 0) /* DLAB=0 read */
#define THR (COM1 + 0) /* DLAB=0 write */
#define DLL (COM1 + 0) /* DLAB=1 */
#define IER (COM1 + 1)
#define DLM (COM1 + 1) /* DLAB=1 */
#define IIR (COM1 + 2)
#define FCR (COM1 + 2)
#define LCR (COM1 + 3)
#define MCR (COM1 + 4)
#define LSR (COM1 + 5)

#define IER_RX 0x01u
#define IER_THRE 0x02u
#define LSR_DATA 0x01u
#define LSR_OVERRUN 0x02u
#define LSR_THRE 0x20u
#define FIFO_DEPTH 16u

struct ring {
    volatile uint32_t head, tail; /* head: producer, tail: consumer */
    char data[UART_RING_SIZE];
};
static struct ring rx, tx;
static volatile struct uart_stats stats;
static bool present;

static bool ring_put(struct ring *r, char ch) {
    uint32_t head = r->head;
    if (head - r->tail == UART_RING_SIZE) return false;
    r->data[head % UART_RING_SIZE] = ch;
    __asm__ volatile ("" : : : "memory");
    r->head = head + 1;
    return true;
}
static bool ring_get(struct ring *r, char *ch) {
    uint32_t tail = r->tail;
    if (tail == r->head) return false;
    *ch = r->data[tail % UART_RING_SIZE];
    __asm__ volatile ("" : : : "memory");
    r->tail = tail + 1;
    return true;
}
static uint32_t irq_save(void) {
    uint32_t flags;
    __asm__ volatile ("pushf; pop %0; cli" : "=r"(flags) : : "memory");
    return flags;
}
static void irq_restore(uint32_t flags) {
    if (flags & 0x200) __asm__ volatile ("sti" : : : "memory");
}

bool uart_init(void) {
    outb(IER, 0x00);
    outb(LCR, 0x80);           /* DLAB on */
    outb(DLL, 0x01);           /* 115200 baud: divisor 1 */
    outb(DLM, 0x00);
    outb(LCR, 0x03);           /* 8 data bits, no parity, 1 stop bit */
    outb(FCR, 0xC7);           /* enable + clear FIFOs, 14-byte RX trigger */
    /* Loopback self-test proves a 16550 is present before using it. */
    outb(MCR, 0x1E);
    outb(THR, 0xAE);
    for (unsigned spin = 0; spin < 100000 && !(inb(LSR) & LSR_DATA); ++spin) {}
    if (inb(RBR) != 0xAE) return present = false;
    if ((inb(IIR) & 0xC0) != 0xC0) return present = false; /* no FIFO: 8250 */
    outb(MCR, 0x0B);           /* DTR, RTS, OUT2 (routes IRQ to the PIC) */
    while (inb(LSR) & LSR_DATA) (void)inb(RBR);
    outb(IER, IER_RX);
    return present = true;
}

void uart_irq(void) {
    ++stats.irqs;
    /* Loop until the UART reports no pending interrupt (IIR bit 0 set). */
    for (uint8_t iir; !((iir = inb(IIR)) & 1);) {
        uint8_t lsr = inb(LSR);
        if (lsr & LSR_OVERRUN) ++stats.overruns;
        while (lsr & LSR_DATA) {
            char ch = (char)inb(RBR);
            if (ring_put(&rx, ch)) ++stats.rx;
            else ++stats.rx_dropped;
            lsr = inb(LSR);
        }
        if (lsr & LSR_THRE) {
            char ch;
            unsigned n = 0;
            while (n < FIFO_DEPTH && ring_get(&tx, &ch)) {
                outb(THR, (uint8_t)ch);
                ++stats.tx;
                ++n;
            }
            if (!n) outb(IER, IER_RX); /* nothing left: stop THRE interrupts */
        }
    }
}

bool uart_getc(char *ch) { return ring_get(&rx, ch); }

void uart_putc(char ch) {
    if (!present) return;
    uint32_t flags = irq_save();
    if (!ring_put(&tx, ch)) {
        /* Ring full with IRQs masked (or called from an IRQ): drain by polling. */
        char out;
        while (ring_get(&tx, &out)) {
            while (!(inb(LSR) & LSR_THRE)) {}
            outb(THR, (uint8_t)out);
            ++stats.tx;
        }
        ring_put(&tx, ch);
    }
    outb(IER, IER_RX | IER_THRE); /* THR-empty interrupt resumes the drain */
    irq_restore(flags);
}

void uart_write(const char *s) {
    while (*s) {
        if (*s == '\n') uart_putc('\r');
        uart_putc(*s++);
    }
}

void uart_write_uint(uint32_t value) {
    char digits[10];
    unsigned n = 0;
    do { digits[n++] = '0' + value % 10; value /= 10; } while (value);
    while (n) uart_putc(digits[--n]);
}

struct uart_stats uart_stats(void) {
    uint32_t flags = irq_save();
    struct uart_stats copy = {stats.irqs, stats.rx, stats.tx, stats.rx_dropped, stats.overruns};
    irq_restore(flags);
    return copy;
}
