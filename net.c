#include "net.h"
#include "rtl8139.h"

static struct net_ctx ctx;
static bool present;
static uint8_t rx[NET_MAX_FRAME + 8], tx[NET_MAX_FRAME + 8];

bool net_init(void) {
    uint8_t mac[6];
    static const uint8_t ip[4] = {10, 0, 2, 15};
    present = rtl8139_init(mac);
    net_ctx_init(&ctx, present ? mac : (const uint8_t[6]){0, 0, 0, 0, 0, 0}, ip);
    return present;
}
bool net_present(void) { return present; }
const struct net_ctx *net_context(void) { return &ctx; }

static void transmit(const uint8_t *frame, size_t length) {
    if (length && rtl8139_send(frame, length)) ++ctx.stats.tx_frames;
}
void net_poll(void) {
    if (!present) return;
    for (unsigned budget = 0; budget < 16 && rtl8139_has_frame(); ++budget) {
        size_t length = rtl8139_recv(rx, sizeof(rx)); /* 0 for a damaged frame */
        if (length) transmit(tx, net_process(&ctx, rx, length, tx));
    }
}
bool net_send_arp_request(const uint8_t ip[4]) {
    if (!present) return false;
    size_t length = net_build_arp_request(&ctx, ip, tx);
    transmit(tx, length);
    return true;
}
