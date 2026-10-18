#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Passive structural check only: a CRC is not proof of ownership/authenticity.
 * No certificate creation, transmission or automatic enrollment is provided. */
static inline uint8_t nice_o_certificate_crc(const uint8_t* bytes, unsigned start, unsigned end) {
    uint8_t state = 0xFF;
    for(unsigned bit = start; bit < end; bit++) {
        const bool feedback = (state ^ (bytes[bit / 8] >> (7 - bit % 8))) & 1;
        state >>= 1;
        if(feedback) state ^= 0x97;
    }
    uint8_t result = 0;
    for(unsigned bit = 0; bit < 8; bit++) result = (result << 1) | ((state >> bit) & 1);
    return result;
}

static inline bool nice_o_certificate_decode(uint64_t body, uint64_t tail, uint32_t* certificate) {
    if(!certificate || body >> 52 || tail >> 60) return false;
    uint8_t p[15] = {0};
    for(unsigned i = 0; i < 7; i++) p[i] = body >> (48 - i * 8);
    for(unsigned i = 0; i < 7; i++) p[7 + i] = tail >> (52 - i * 8);
    p[14] = tail << 4;
    if((p[7] & 0xC0) != 0xC0 || ((((p[8] >> 4) ^ p[7]) & 2) == 0) ||
       (p[9] & 15) != 14 ||
       ((p[8] & 15) << 4 | p[9] >> 4) != nice_o_certificate_crc(p, 4, 68) ||
       ((p[13] & 15) << 4 | p[14] >> 4) != nice_o_certificate_crc(p, 68, 108)) {
        return false;
    }
    *certificate = ((uint32_t)(tail >> 8) ^ (uint32_t)body) & 0xFFFFFF;
    return true;
}
