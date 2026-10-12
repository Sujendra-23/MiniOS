#ifndef CAN_FRAME_H
#define CAN_FRAME_H
#include <stdbool.h>
#include <stdint.h>

/* Classic CAN frame (up to 8 data bytes) and its NXP SJA1000 PeliCAN
 * register-window layout (info byte, ID bytes, data). Pure functions so they
 * are unit-tested natively. */
struct can_frame {
    uint32_t id;      /* 11-bit (standard) or 29-bit (extended) */
    uint8_t dlc;      /* 0..8 */
    bool extended;
    bool rtr;
    uint8_t data[8];
};
#define CAN_SFF_MASK 0x7FFu
#define CAN_EFF_MASK 0x1FFFFFFFu
#define CAN_PACKED_MAX 13u

/* Encode into SJA1000 TX/RX buffer bytes; returns length, 0 if invalid. */
unsigned can_frame_pack(const struct can_frame *f, uint8_t out[CAN_PACKED_MAX]);
/* Number of bytes the buffer occupies, from its first (info) byte. */
unsigned can_packed_length(uint8_t info);
/* Decode; false if in is too short or malformed. */
bool can_frame_unpack(const uint8_t *in, unsigned len, struct can_frame *f);
bool can_frame_equal(const struct can_frame *a, const struct can_frame *b);
#endif
