#ifndef CAN_H
#define CAN_H
#include <stdbool.h>
#include <stdint.h>
#include "can_frame.h"

/* Polled driver for the SJA1000 CAN controller on QEMU's emulated Kvaser
 * PCIcanx card (PCI 10e8:8406). It programs the SJA1000 in PeliCAN mode
 * through I/O BAR1. Emulated hardware only: QEMU's can-bus object connects
 * the controllers, there is no CAN transceiver or physical bus. */
#define CAN_MAX_CONTROLLERS 2u

struct can_selftest_result {
    uint32_t sent, received, mismatches, tx_timeouts, rx_timeouts;
};

unsigned can_init(void);              /* returns controllers found */
unsigned can_controller_count(void);
uint16_t can_controller_io(unsigned index);
bool can_send(unsigned controller, const struct can_frame *frame);
bool can_receive(unsigned controller, struct can_frame *frame);
/* Send `frames` frames alternately 0->1 and 1->0, receive each on the peer,
 * and compare id/flags/dlc/data. Needs two controllers on one bus. */
struct can_selftest_result can_selftest(unsigned frames);
#endif
