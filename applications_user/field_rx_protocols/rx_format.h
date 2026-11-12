#pragma once
#include <lib/subghz/blocks/generic.h>

// The shared generic format stores only the first64-bit Key. These new long
// receive-only protocols require the exact second word; never invent a zero tail.
static inline bool field_rx_write_tail(SubGhzBlockGeneric* generic, FlipperFormat* format) {
    if(generic->data_count_bit <= 64 || generic->data_count_bit > 128) return false;
    uint8_t bytes[8];
    for(unsigned i = 0; i < 8; i++)
        bytes[7 - i] = generic->data_2 >> (i * 8);
    return flipper_format_rewind(format) &&
           flipper_format_insert_or_update_hex(format, "Key2", bytes, sizeof(bytes));
}
static inline bool field_rx_read_tail(SubGhzBlockGeneric* generic, FlipperFormat* format) {
    generic->data_2 = 0;
    uint32_t count;
    uint8_t bytes[8];
    if(!flipper_format_rewind(format) || !flipper_format_get_value_count(format, "Key2", &count) ||
       count != sizeof(bytes) || !flipper_format_rewind(format) ||
       !flipper_format_read_hex(format, "Key2", bytes, sizeof(bytes)))
        return false;
    for(unsigned i = 0; i < 8; i++)
        generic->data_2 = (generic->data_2 << 8) | bytes[i];
    return true;
}
