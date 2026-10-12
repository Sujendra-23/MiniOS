#ifndef NET_PROTO_H
#define NET_PROTO_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Hardware-independent Ethernet II / ARP / IPv4 / ICMP echo handling.
 * net_process() consumes one received frame and, if the protocol requires an
 * answer, builds it in `out`. No allocation, no libc, host-testable. */
#define NET_ARP_ENTRIES 4u
#define NET_MAX_FRAME 1514u

struct net_stats {
    uint32_t rx_frames, tx_frames, rx_dropped;
    uint32_t arp_req_rx, arp_reply_tx, arp_req_tx, arp_reply_rx;
    uint32_t icmp_echo_rx, icmp_reply_tx, bad_checksum;
};
struct arp_entry {
    bool valid;
    uint8_t ip[4], mac[6];
};
struct net_ctx {
    uint8_t mac[6], ip[4];
    struct net_stats stats;
    struct arp_entry arp[NET_ARP_ENTRIES];
};

uint16_t net_checksum(const void *data, size_t length);
void net_ctx_init(struct net_ctx *ctx, const uint8_t mac[6], const uint8_t ip[4]);
/* Returns the length of a reply written to out (>= NET_MAX_FRAME bytes), or 0. */
size_t net_process(struct net_ctx *ctx, const uint8_t *in, size_t length, uint8_t *out);
/* Builds a broadcast who-has frame for target_ip; returns its length. */
size_t net_build_arp_request(struct net_ctx *ctx, const uint8_t target_ip[4], uint8_t *out);
bool net_arp_lookup(const struct net_ctx *ctx, const uint8_t ip[4], uint8_t mac[6]);
bool net_parse_ip(const char *text, uint8_t ip[4]);
#endif
