#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static inline bool weather_editor_bits_valid(
    const char* protocol, uint32_t bits, uint32_t variable_bits, uint32_t frame_bits) {
    // TX8300 reports the full frame length; its encoder handles that format separately.
    const bool data_fits = bits <= 64U ||
                           (protocol && !strcmp(protocol, "TX8300") && bits == 72U);
    return data_fits && variable_bits <= 64U && frame_bits <= 128U;
}

static inline bool weather_editor_preset_valid(const uint8_t* data, size_t size) {
    if(!data || size < 10U || size > 512U) return false;
    // HAL reads register/value pairs until a zero register, then eight PA bytes.
    for(size_t offset = 0; offset + 1U < size; offset += 2U) {
        if(data[offset] == 0U) return size - offset == 10U;
        if(data[offset] > 0x2EU) return false;
    }
    return false;
}
