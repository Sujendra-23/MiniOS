#ifndef NET_H
#define NET_H
#include <stdbool.h>
#include "net_proto.h"

/* Static IPv4 10.0.2.15/24 (QEMU user-mode default); gateway 10.0.2.2. */
bool net_init(void);
bool net_present(void);
void net_poll(void);                       /* drain RX ring, answer ARP/ICMP */
bool net_send_arp_request(const uint8_t ip[4]);
const struct net_ctx *net_context(void);
#endif
