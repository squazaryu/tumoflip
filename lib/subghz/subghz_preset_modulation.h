#pragma once

#include "types.h"
#include <string.h>

/** Determine RX modulation from bounded CC1101 register pairs, not a user label.
 * Unknown/malformed custom presets disable filtering instead of losing frames.
 * A preset ends with a 00/00 register terminator followed by an 8-byte PA table.
 */
static inline SubGhzProtocolFlag
    subghz_preset_modulation(const char* name, const uint8_t* data, size_t size) {
    if(data) {
        int modulation = -1;
        for(size_t i = 0; i + 1 < size; i += 2) {
            if(data[i] == 0 && data[i + 1] == 0) {
                if(size - i < 10) return 0;
                if(modulation == 3) return SubGhzProtocolFlag_AM;
                if(modulation == 0 || modulation == 1 || modulation == 4 || modulation == 7)
                    return SubGhzProtocolFlag_FM;
                return 0;
            }
            if(data[i] > 0x2E) return 0;
            if(data[i] == 0x12) modulation = (data[i + 1] >> 4) & 7;
        }
        return 0;
    }
    if(!name || size) return 0;
    if(strcmp(name, "AM270") == 0 || strcmp(name, "AM650") == 0) return SubGhzProtocolFlag_AM;
    if(strcmp(name, "FM238") == 0 || strcmp(name, "FM476") == 0 || strcmp(name, "FM12K") == 0)
        return SubGhzProtocolFlag_FM;
    return 0;
}
