#pragma once
#include <stdint.h>

/** CC1101 base-frequency word and channel spacing, using a 26 MHz crystal. */
static inline uint32_t cc1101_channel_frequency(
    uint32_t word, uint8_t mantissa, uint8_t exponent, uint8_t channel) {
    if(word > 0xFFFFFFU) return 0;
    uint64_t numerator = (uint64_t)word * 4U * 26000000U;
    numerator += (uint64_t)channel * (256U + mantissa) * (1U << (exponent & 3U)) * 26000000U;
    uint64_t frequency = numerator / 262144U;
    return frequency > UINT32_MAX ? 0U : (uint32_t)frequency;
}
