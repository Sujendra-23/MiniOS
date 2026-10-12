#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "net_proto.h"

static const uint8_t our_mac[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
static const uint8_t our_ip[4] = {10, 0, 2, 15};
static const uint8_t peer_mac[6] = {0x52, 0x55, 0x0A, 0x00, 0x02, 0x02};
static const uint8_t peer_ip[4] = {10, 0, 2, 2};
static struct net_ctx ctx;

static void reset(void) { net_ctx_init(&ctx, our_mac, our_ip); }

static void checksum_vectors(void) {
    /* Textbook IPv4 header; the checksum field is 0xB861. */
    uint8_t h[20] = {0x45, 0, 0, 0x73, 0, 0, 0x40, 0, 0x40, 0x11, 0, 0,
                     0xC0, 0xA8, 0, 1, 0xC0, 0xA8, 0, 0xC7};
    assert(net_checksum(h, 20) == 0xB861);
    h[10] = 0xB8; h[11] = 0x61;
    assert(net_checksum(h, 20) == 0);
    uint8_t odd[3] = {0x01, 0x02, 0x03};
    assert(net_checksum(odd, 3) == (uint16_t)~(0x0102 + 0x0300));
}
static size_t arp_frame(uint8_t *f, uint16_t op, const uint8_t *tip) {
    memset(f, 0, 64);
    memset(f, 0xFF, 6);
    memcpy(f + 6, peer_mac, 6);
    f[12] = 0x08; f[13] = 0x06;
    uint8_t *a = f + 14;
    a[1] = 1; a[2] = 8; a[4] = 6; a[5] = 4; a[7] = (uint8_t)op;
    memcpy(a + 8, peer_mac, 6);
    memcpy(a + 14, peer_ip, 4);
    memcpy(a + 24, tip, 4);
    return 42;
}
static void arp_request_gets_reply(void) {
    reset();
    uint8_t in[64], out[1600];
    size_t n = net_process(&ctx, in, arp_frame(in, 1, our_ip), out);
    assert(n == 42);
    assert(!memcmp(out, peer_mac, 6) && !memcmp(out + 6, our_mac, 6));
    assert(out[12] == 0x08 && out[13] == 0x06 && out[14 + 7] == 2);
    assert(!memcmp(out + 14 + 8, our_mac, 6) && !memcmp(out + 14 + 14, our_ip, 4));
    assert(!memcmp(out + 14 + 18, peer_mac, 6) && !memcmp(out + 14 + 24, peer_ip, 4));
    uint8_t mac[6];
    assert(net_arp_lookup(&ctx, peer_ip, mac) && !memcmp(mac, peer_mac, 6));
    assert(ctx.stats.arp_req_rx == 1 && ctx.stats.arp_reply_tx == 1);
    /* A who-has for somebody else is ignored. */
    const uint8_t other[4] = {10, 0, 2, 99};
    assert(net_process(&ctx, in, arp_frame(in, 1, other), out) == 0);
    assert(ctx.stats.arp_reply_tx == 1);
}
static void arp_reply_is_learned(void) {
    reset();
    uint8_t in[64], out[1600];
    uint8_t request[64];
    assert(net_build_arp_request(&ctx, peer_ip, request) == 42);
    assert(request[0] == 0xFF && request[14 + 7] == 1 && !memcmp(request + 14 + 24, peer_ip, 4));
    memcpy(in, our_mac, 6);
    assert(net_process(&ctx, in, arp_frame(in, 2, our_ip), out) == 0);
    memcpy(in, our_mac, 6); /* frame_ARP sets broadcast; unicast reply is also accepted */
    assert(ctx.stats.arp_reply_rx == 1 && ctx.stats.arp_req_tx == 1);
    uint8_t mac[6];
    assert(net_arp_lookup(&ctx, peer_ip, mac));
}
static size_t echo_request(uint8_t *f, uint16_t id, uint16_t seq, size_t payload) {
    memset(f, 0, 1600);
    memcpy(f, our_mac, 6);
    memcpy(f + 6, peer_mac, 6);
    f[12] = 0x08;
    uint8_t *ip = f + 14;
    size_t total = 20 + 8 + payload;
    ip[0] = 0x45; ip[2] = (uint8_t)(total >> 8); ip[3] = (uint8_t)total;
    ip[6] = 0x40; ip[8] = 64; ip[9] = 1;
    memcpy(ip + 12, peer_ip, 4);
    memcpy(ip + 16, our_ip, 4);
    uint16_t c = net_checksum(ip, 20);
    ip[10] = (uint8_t)(c >> 8); ip[11] = (uint8_t)c;
    uint8_t *icmp = ip + 20;
    icmp[0] = 8;
    icmp[4] = (uint8_t)(id >> 8); icmp[5] = (uint8_t)id;
    icmp[6] = (uint8_t)(seq >> 8); icmp[7] = (uint8_t)seq;
    for (size_t i = 0; i < payload; ++i) icmp[8 + i] = (uint8_t)(i * 7 + 1);
    c = net_checksum(icmp, 8 + payload);
    icmp[2] = (uint8_t)(c >> 8); icmp[3] = (uint8_t)c;
    return 14 + total;
}
static void echo_gets_reply(void) {
    reset();
    uint8_t in[1600], out[1600];
    for (size_t payload = 0; payload <= 1472; payload += (payload < 64 ? 7 : 400)) {
        size_t len = echo_request(in, 0x1234, (uint16_t)payload, payload);
        size_t n = net_process(&ctx, in, len, out);
        assert(n == len);
        assert(!memcmp(out, peer_mac, 6) && !memcmp(out + 6, our_mac, 6));
        const uint8_t *ip = out + 14, *icmp = ip + 20;
        assert(net_checksum(ip, 20) == 0);              /* valid IP header checksum */
        assert(!memcmp(ip + 12, our_ip, 4) && !memcmp(ip + 16, peer_ip, 4));
        assert(net_checksum(icmp, 8 + payload) == 0);   /* valid ICMP checksum */
        assert(icmp[0] == 0 && icmp[1] == 0);           /* echo reply */
        assert(icmp[4] == 0x12 && icmp[5] == 0x34);     /* identifier echoed */
        assert(icmp[6] == (uint8_t)(payload >> 8) && icmp[7] == (uint8_t)payload);
        assert(!memcmp(icmp + 8, in + 14 + 20 + 8, payload));
    }
    assert(ctx.stats.icmp_reply_tx == ctx.stats.icmp_echo_rx && ctx.stats.icmp_echo_rx > 5);
}
static void bad_packets_are_dropped(void) {
    reset();
    uint8_t in[1600], out[1600];
    size_t len = echo_request(in, 1, 1, 16);
    in[14 + 20 + 9] ^= 0xFF;                            /* corrupt ICMP payload */
    assert(net_process(&ctx, in, len, out) == 0 && ctx.stats.bad_checksum == 1);
    len = echo_request(in, 1, 1, 16);
    in[14 + 8] ^= 0x01;                                 /* corrupt IP TTL */
    assert(net_process(&ctx, in, len, out) == 0 && ctx.stats.bad_checksum == 2);
    len = echo_request(in, 1, 1, 16);
    assert(net_process(&ctx, in, len - 10, out) == 0);  /* truncated */
    len = echo_request(in, 1, 1, 16);
    in[0] ^= 0x02;                                      /* not our MAC */
    assert(net_process(&ctx, in, len, out) == 0);
    assert(ctx.stats.icmp_reply_tx == 0);
    assert(net_process(&ctx, in, 5, out) == 0);         /* runt */
}
static void parse_ip(void) {
    uint8_t ip[4];
    assert(net_parse_ip("10.0.2.2", ip) && ip[0] == 10 && ip[3] == 2);
    assert(!net_parse_ip("10.0.2", ip) && !net_parse_ip("10.0.2.256", ip));
    assert(!net_parse_ip("10.0.2.2x", ip) && !net_parse_ip("", ip));
}
int main(void) {
    checksum_vectors();
    arp_request_gets_reply();
    arp_reply_is_learned();
    echo_gets_reply();
    bad_packets_are_dropped();
    parse_ip();
    puts("net protocol tests passed");
    return 0;
}
