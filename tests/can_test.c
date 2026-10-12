#include <assert.h>
#include <stdio.h>
#include "can_frame.h"

static void standard_layout(void) {
    struct can_frame f = {0x123, 2, false, false, {0xDE, 0xAD, 0, 0, 0, 0, 0, 0}};
    uint8_t b[CAN_PACKED_MAX];
    assert(can_frame_pack(&f, b) == 5);
    /* SJA1000 SFF: info, ID[10:3], ID[2:0]<<5, data */
    assert(b[0] == 0x02 && b[1] == 0x24 && b[2] == 0x60 && b[3] == 0xDE && b[4] == 0xAD);
    struct can_frame g;
    assert(can_frame_unpack(b, 5, &g) && can_frame_equal(&f, &g));
}
static void extended_layout(void) {
    struct can_frame f = {0x1ABCDEF1, 8, true, false, {1, 2, 3, 4, 5, 6, 7, 8}};
    uint8_t b[CAN_PACKED_MAX];
    assert(can_frame_pack(&f, b) == 13);
    assert(b[0] == 0x88);
    assert(b[1] == (0x1ABCDEF1 >> 21) && b[2] == ((0x1ABCDEF1 >> 13) & 0xFF));
    assert(b[3] == ((0x1ABCDEF1 >> 5) & 0xFF) && b[4] == ((0x1ABCDEF1 << 3) & 0xFF));
    struct can_frame g;
    assert(can_frame_unpack(b, 13, &g) && can_frame_equal(&f, &g));
}
static void remote_frame(void) {
    struct can_frame f = {0x7FF, 4, false, true, {0}};
    uint8_t b[CAN_PACKED_MAX];
    assert(can_frame_pack(&f, b) == 3 && b[0] == 0x44); /* RTR: no data bytes */
    struct can_frame g;
    assert(can_frame_unpack(b, 3, &g) && g.rtr && g.dlc == 4 && g.id == 0x7FF);
}
static void rejects_invalid(void) {
    uint8_t b[CAN_PACKED_MAX] = {0};
    struct can_frame f = {0x800, 0, false, false, {0}};
    assert(can_frame_pack(&f, b) == 0);          /* 12-bit ID in a standard frame */
    f.id = 1; f.dlc = 9;
    assert(can_frame_pack(&f, b) == 0);          /* DLC > 8 */
    f.dlc = 8; f.extended = true; f.id = 0x20000000;
    assert(can_frame_pack(&f, b) == 0);          /* 30-bit ID */
    struct can_frame g;
    uint8_t truncated[2] = {0x08, 0};
    assert(!can_frame_unpack(truncated, 2, &g)); /* too short */
    assert(!can_frame_unpack(truncated, 0, &g));
}
static void sweep_round_trip(void) {
    for (uint32_t i = 0; i < 2000; ++i) {
        struct can_frame f, g;
        uint8_t b[CAN_PACKED_MAX];
        f.extended = i % 5 == 0;
        f.rtr = i % 7 == 3;
        f.id = (i * 0x9E3779B1u >> 3) & (f.extended ? CAN_EFF_MASK : CAN_SFF_MASK);
        f.dlc = i % 9;
        for (unsigned j = 0; j < 8; ++j) f.data[j] = f.rtr ? 0 : (uint8_t)(i * 31 + j * 17);
        unsigned n = can_frame_pack(&f, b);
        assert(n == can_packed_length(b[0]));
        assert(can_frame_unpack(b, n, &g) && can_frame_equal(&f, &g));
    }
}
int main(void) {
    standard_layout();
    extended_layout();
    remote_frame();
    rejects_invalid();
    sweep_round_trip();
    puts("can frame tests passed");
    return 0;
}
