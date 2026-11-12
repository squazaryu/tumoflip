#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SUBGHZ_PACKET_PRESET_MAX 256U

/** Variable-length, normal packet format with CRC and sync/end-of-packet GDO0.
 * Exact register terminator and eight-byte PA table are required. */
bool subghz_packet_preset_is_valid(const uint8_t* data, size_t size);

/** CC1101 26 MHz crystal; frequency word plus channel-spacing registers.
 * Returns zero for an invalid frequency word or arithmetic overflow. */
uint32_t subghz_packet_channel_frequency(
    uint32_t word, uint8_t spacing_mantissa, uint8_t spacing_exponent, uint8_t channel);
