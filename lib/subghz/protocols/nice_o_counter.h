#pragma once
#include <stdint.h>

/* O-Code's on-air field is an octal odometer with permuted digits. The same
 * mapping converts in either direction. Keep the plain count outside the cipher
 * boundary; this source helper deliberately adds no resident API export. */
static inline uint16_t subghz_protocol_nice_o_cnt_swap(uint16_t count) {
    const uint16_t bits = count & 0x2492U;
    return count ^ (uint16_t)((bits << 1) | ((bits ^ 0x2492U) >> 1));
}
