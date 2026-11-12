#include "packet_config.h"
#include "../drivers/cc1101_channel.h"

bool subghz_packet_preset_is_valid(const uint8_t* data, size_t size) {
    if(!data || size < 14U || size > SUBGHZ_PACKET_PRESET_MAX || (size & 1U)) return false;
    bool packet = false, gpio = false;
    uint64_t seen = 0;
    for(size_t i = 0; i + 1U < size; i += 2U) {
        uint8_t reg = data[i], value = data[i + 1U];
        if(reg == 0U && value == 0U) return i + 10U == size && packet && gpio;
        if(reg > 0x2EU || (seen & (1ULL << reg))) return false;
        seen |= 1ULL << reg;
        if(reg == 0x08U) packet = (value & 0x37U) == 0x05U;
        if(reg == 0x02U) gpio = value == 0x06U;
    }
    return false;
}

uint32_t subghz_packet_channel_frequency(
    uint32_t word, uint8_t mantissa, uint8_t exponent, uint8_t channel) {
    return cc1101_channel_frequency(word, mantissa, exponent, channel);
}
