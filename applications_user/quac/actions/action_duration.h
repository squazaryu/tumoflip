#pragma once
#include <stdbool.h>
#include <stdint.h>

#define QUAC_ACTION_MIN_MS 100U
#define QUAC_ACTION_MAX_MS 60000U

static inline bool quac_duration_valid(uint32_t duration) {
    return duration >= QUAC_ACTION_MIN_MS && duration <= QUAC_ACTION_MAX_MS;
}

static inline bool quac_duration_parse(const char* text, bool allow_zero, uint32_t* result) {
    if(!text || !*text || !result) return false;
    uint32_t value = 0;
    for(; *text; text++) {
        if(*text < '0' || *text > '9') return false;
        const uint32_t digit = (uint32_t)(*text - '0');
        if(value > (QUAC_ACTION_MAX_MS - digit) / 10U) return false;
        value = value * 10U + digit;
    }
    // Pauses allow 0..60000 ms; active emulation requires at least 100 ms.
    if(!allow_zero && !quac_duration_valid(value)) return false;
    *result = value;
    return true;
}
