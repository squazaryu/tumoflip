#include "varta_inspect.h"
#include <string.h>

bool varta_inspect_frame(uint32_t high, uint32_t low, uint8_t id[5]) {
    if(!id) return false;
    const uint64_t frame = ((uint64_t)high << 32U) | low;
    if((frame >> 55U) != 0x1FFU || (frame & 1U)) return false;
    uint8_t decoded[5] = {0};
    uint8_t columns = 0;
    for(unsigned row = 0; row < 10U; row++) {
        const unsigned shift = 50U - row * 5U;
        const uint8_t value = (frame >> shift) & 31U;
        uint8_t parity = 0;
        for(unsigned bit = 0; bit < 5U; bit++) parity ^= (value >> bit) & 1U;
        if(parity) return false;
        const uint8_t nibble = value >> 1U;
        columns ^= nibble;
        decoded[row / 2U] |= nibble << (row % 2U ? 0U : 4U);
    }
    if(columns != ((frame >> 1U) & 15U)) return false;
    memcpy(id, decoded, sizeof(decoded));
    return true;
}

bool varta_inspect_read_list(const char* text, uint8_t* mask) {
    if(!text || !mask) return false;
    uint8_t result = 0;
    const char* p = text;
    while(*p) {
        while(*p == ' ' || *p == '\t') ++p;
        if(!*p) break;
        if(*p < '0' || *p > '7') return false;
        const uint8_t bit = 1U << (*p++ - '0');
        if((result & bit) || (*p && *p != ' ' && *p != '\t')) return false;
        result |= bit;
    }
    *mask = result;
    return true;
}

void varta_inspect_id(VartaInspection* result, const uint8_t id[5], uint8_t clock) {
    if(!result || !id) return;
    memset(result, 0, sizeof(*result));
    memcpy(result->id, id, sizeof(result->id));
    result->clock = clock;
    result->known_prefix = (id[0] == 3U || id[0] == 4U) && id[1] == 0U;
    result->status = VartaIdOnly;
}

void varta_inspect_dump(VartaInspection* result, const uint32_t blocks[8], uint8_t mask) {
    if(!result || !blocks) return;
    memset(result, 0, sizeof(*result));
    result->status = VartaMissingBlocks;
    if((mask & 6U) != 6U) return;
    uint8_t id[5];
    if(!varta_inspect_frame(blocks[1], blocks[2], id)) {
        result->status = VartaBadFrame;
        return;
    }
    varta_inspect_id(result, id, 0);
    result->status = VartaDumpDecoded;
    result->has_config = (mask & 1U) != 0;
    result->config_matches = result->has_config && blocks[0] == 0x00148050U;
    if(result->config_matches) result->clock = 64U;
    result->has_marker = (mask & 64U) != 0;
    result->marker_matches = result->has_marker && blocks[6] == 0xFFAD2D00U;
}
