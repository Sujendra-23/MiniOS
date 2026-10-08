#ifndef UART_H
#define UART_H
#include <stdbool.h>
#include <stdint.h>

/* Interrupt-driven 16550 driver for COM1 (I/O 0x3F8, IRQ4, 115200 8N1).
 * IRQ4 moves received bytes into a ring buffer and drains a transmit ring
 * through the THR-empty interrupt. Ring buffers are single-producer,
 * single-consumer; uart_write may be called from any context. */
#define UART_RING_SIZE 256u

struct uart_stats {
    uint32_t irqs, rx, tx, rx_dropped, overruns;
};

bool uart_init(void);
void uart_irq(void);
bool uart_getc(char *ch);
void uart_putc(char ch);
void uart_write(const char *s);
void uart_write_uint(uint32_t value);
struct uart_stats uart_stats(void);
#endif
