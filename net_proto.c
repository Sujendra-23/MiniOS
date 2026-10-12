#include "net_proto.h"

#define ETH_HEADER 14u
#define ETHERTYPE_IPV4 0x0800u
#define ETHERTYPE_ARP 0x0806u
#define ARP_LENGTH 28u

static void copy(uint8_t *dst, const uint8_t *src, size_t n) {
    for (size_t i = 0; i < n; ++i) dst[i] = src[i];
}
static bool same(const uint8_t *a, const uint8_t *b, size_t n) {
    for (size_t i = 0; i < n; ++i) if (a[i] != b[i]) return false;
    return true;
}
static uint16_t get16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }

uint16_t net_checksum(const void *data, size_t length) {
    const uint8_t *p = data;
    uint32_t sum = 0;
    for (; length > 1; length -= 2, p += 2) sum += get16(p);
    if (length) sum += (uint32_t)p[0] << 8;
    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    return (uint16_t)~sum;
}
void net_ctx_init(struct net_ctx *ctx, const uint8_t mac[6], const uint8_t ip[4]) {
    uint8_t *raw = (uint8_t *)ctx;
    for (size_t i = 0; i < sizeof(*ctx); ++i) raw[i] = 0;
    copy(ctx->mac, mac, 6);
    copy(ctx->ip, ip, 4);
}
bool net_arp_lookup(const struct net_ctx *ctx, const uint8_t ip[4], uint8_t mac[6]) {
    for (unsigned i = 0; i < NET_ARP_ENTRIES; ++i)
        if (ctx->arp[i].valid && same(ctx->arp[i].ip, ip, 4)) {
            copy(mac, ctx->arp[i].mac, 6);
            return true;
        }
    return false;
}
static void arp_learn(struct net_ctx *ctx, const uint8_t ip[4], const uint8_t mac[6]) {
    unsigned slot = NET_ARP_ENTRIES;
    for (unsigned i = 0; i < NET_ARP_ENTRIES; ++i) {
        if (ctx->arp[i].valid && same(ctx->arp[i].ip, ip, 4)) { slot = i; break; }
        if (!ctx->arp[i].valid && slot == NET_ARP_ENTRIES) slot = i;
    }
    if (slot == NET_ARP_ENTRIES) slot = 0; /* full: overwrite the oldest-created slot */
    ctx->arp[slot].valid = true;
    copy(ctx->arp[slot].ip, ip, 4);
    copy(ctx->arp[slot].mac, mac, 6);
}
static size_t build_arp(struct net_ctx *ctx, uint8_t *out, uint16_t op,
                        const uint8_t dst_mac[6], const uint8_t target_mac[6],
                        const uint8_t target_ip[4]) {
    copy(out, dst_mac, 6);
    copy(out + 6, ctx->mac, 6);
    put16(out + 12, ETHERTYPE_ARP);
    uint8_t *a = out + ETH_HEADER;
    put16(a, 1);                 /* Ethernet */
    put16(a + 2, ETHERTYPE_IPV4);
    a[4] = 6; a[5] = 4;
    put16(a + 6, op);
    copy(a + 8, ctx->mac, 6);
    copy(a + 14, ctx->ip, 4);
    copy(a + 18, target_mac, 6);
    copy(a + 24, target_ip, 4);
    return ETH_HEADER + ARP_LENGTH;
}
size_t net_build_arp_request(struct net_ctx *ctx, const uint8_t target_ip[4], uint8_t *out) {
    static const uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    static const uint8_t zero[6] = {0, 0, 0, 0, 0, 0};
    ++ctx->stats.arp_req_tx;
    return build_arp(ctx, out, 1, broadcast, zero, target_ip);
}
static size_t handle_arp(struct net_ctx *ctx, const uint8_t *in, size_t length, uint8_t *out) {
    if (length < ETH_HEADER + ARP_LENGTH) { ++ctx->stats.rx_dropped; return 0; }
    const uint8_t *a = in + ETH_HEADER;
    if (get16(a) != 1 || get16(a + 2) != ETHERTYPE_IPV4 || a[4] != 6 || a[5] != 4) {
        ++ctx->stats.rx_dropped;
        return 0;
    }
    uint16_t op = get16(a + 6);
    const uint8_t *sender_mac = a + 8, *sender_ip = a + 14, *target_ip = a + 24;
    if (op == 1) {
        ++ctx->stats.arp_req_rx;
        if (!same(target_ip, ctx->ip, 4)) return 0;
        arp_learn(ctx, sender_ip, sender_mac);
        ++ctx->stats.arp_reply_tx;
        return build_arp(ctx, out, 2, sender_mac, sender_mac, sender_ip);
    }
    if (op == 2 && same(target_ip, ctx->ip, 4)) {
        ++ctx->stats.arp_reply_rx;
        arp_learn(ctx, sender_ip, sender_mac);
    }
    return 0;
}
static size_t handle_ipv4(struct net_ctx *ctx, const uint8_t *in, size_t length, uint8_t *out) {
    if (length < ETH_HEADER + 20) { ++ctx->stats.rx_dropped; return 0; }
    const uint8_t *ip = in + ETH_HEADER;
    unsigned header = (ip[0] & 0x0Fu) * 4;
    size_t total = get16(ip + 2);
    if ((ip[0] >> 4) != 4 || header < 20 || total < header + 8 ||
        ETH_HEADER + total > length) { ++ctx->stats.rx_dropped; return 0; }
    if (!same(ip + 16, ctx->ip, 4)) return 0; /* not addressed to us */
    if (net_checksum(ip, header) != 0) { ++ctx->stats.bad_checksum; return 0; }
    if (get16(ip + 6) & 0x3FFFu) { ++ctx->stats.rx_dropped; return 0; } /* fragments */
    if (ip[9] != 1) return 0; /* only ICMP */
    const uint8_t *icmp = ip + header;
    size_t icmp_length = total - header;
    if (net_checksum(icmp, icmp_length) != 0) { ++ctx->stats.bad_checksum; return 0; }
    if (icmp[0] != 8 || icmp[1] != 0) return 0; /* echo request only */
    ++ctx->stats.icmp_echo_rx;
    arp_learn(ctx, ip + 12, in + 6);
    copy(out, in + 6, 6);
    copy(out + 6, ctx->mac, 6);
    put16(out + 12, ETHERTYPE_IPV4);
    uint8_t *r = out + ETH_HEADER;
    copy(r, ip, 20);              /* standard 20-byte header; options are dropped */
    r[0] = 0x45;
    put16(r + 2, (uint16_t)(20 + icmp_length));
    r[8] = 64;                    /* TTL */
    copy(r + 12, ip + 16, 4);     /* src = our address */
    copy(r + 16, ip + 12, 4);     /* dst = requester */
    put16(r + 10, 0);
    put16(r + 10, net_checksum(r, 20));
    uint8_t *e = r + 20;
    copy(e, icmp, icmp_length);
    e[0] = 0;                     /* echo reply */
    put16(e + 2, 0);
    put16(e + 2, net_checksum(e, icmp_length));
    ++ctx->stats.icmp_reply_tx;
    return ETH_HEADER + 20 + icmp_length;
}
size_t net_process(struct net_ctx *ctx, const uint8_t *in, size_t length, uint8_t *out) {
    ++ctx->stats.rx_frames;
    if (length < ETH_HEADER) { ++ctx->stats.rx_dropped; return 0; }
    static const uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    if (!same(in, ctx->mac, 6) && !same(in, broadcast, 6)) return 0;
    switch (get16(in + 12)) {
    case ETHERTYPE_ARP: return handle_arp(ctx, in, length, out);
    case ETHERTYPE_IPV4: return handle_ipv4(ctx, in, length, out);
    default: return 0;
    }
}
bool net_parse_ip(const char *text, uint8_t ip[4]) {
    for (unsigned part = 0; part < 4; ++part) {
        if (*text < '0' || *text > '9') return false;
        unsigned value = 0;
        while (*text >= '0' && *text <= '9') {
            value = value * 10 + (unsigned)(*text++ - '0');
            if (value > 255) return false;
        }
        ip[part] = (uint8_t)value;
        if (part < 3 && *text++ != '.') return false;
    }
    return *text == 0;
}
