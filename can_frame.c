#include "can_frame.h"

unsigned can_packed_length(uint8_t info) {
    unsigned dlc = info & 0x0F;
    if (dlc > 8) dlc = 8;
    unsigned header = (info & 0x80) ? 5 : 3;
    return (info & 0x40) ? header : header + dlc; /* RTR frames carry no data */
}
unsigned can_frame_pack(const struct can_frame *f, uint8_t out[CAN_PACKED_MAX]) {
    if (f->dlc > 8) return 0;
    if (f->id > (f->extended ? CAN_EFF_MASK : CAN_SFF_MASK)) return 0;
    out[0] = (uint8_t)((f->extended ? 0x80 : 0) | (f->rtr ? 0x40 : 0) | f->dlc);
    unsigned n = 1;
    if (f->extended) {
        out[n++] = (uint8_t)(f->id >> 21);
        out[n++] = (uint8_t)(f->id >> 13);
        out[n++] = (uint8_t)(f->id >> 5);
        out[n++] = (uint8_t)(f->id << 3);
    } else {
        out[n++] = (uint8_t)(f->id >> 3);
        out[n++] = (uint8_t)(f->id << 5);
    }
    if (!f->rtr)
        for (unsigned i = 0; i < f->dlc; ++i) out[n++] = f->data[i];
    return n;
}
bool can_frame_unpack(const uint8_t *in, unsigned len, struct can_frame *f) {
    if (len < 1 || len < can_packed_length(in[0])) return false;
    f->extended = (in[0] & 0x80) != 0;
    f->rtr = (in[0] & 0x40) != 0;
    f->dlc = in[0] & 0x0F;
    if (f->dlc > 8) f->dlc = 8;
    unsigned n = 1;
    if (f->extended) {
        f->id = ((uint32_t)in[1] << 21) | ((uint32_t)in[2] << 13) |
                ((uint32_t)in[3] << 5) | (in[4] >> 3);
        n = 5;
    } else {
        f->id = ((uint32_t)in[1] << 3) | (in[2] >> 5);
        n = 3;
    }
    for (unsigned i = 0; i < 8; ++i) f->data[i] = 0;
    if (!f->rtr)
        for (unsigned i = 0; i < f->dlc; ++i) f->data[i] = in[n++];
    return true;
}
bool can_frame_equal(const struct can_frame *a, const struct can_frame *b) {
    if (a->id != b->id || a->dlc != b->dlc || a->extended != b->extended || a->rtr != b->rtr)
        return false;
    if (!a->rtr)
        for (unsigned i = 0; i < a->dlc; ++i)
            if (a->data[i] != b->data[i]) return false;
    return true;
}
