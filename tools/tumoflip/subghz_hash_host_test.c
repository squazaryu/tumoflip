#include <stdint.h>

#include "lib/subghz/blocks/decoder.h"

static uint8_t hash_two_bytes(uint8_t first, uint8_t second) {
    SubGhzBlockDecoder decoder = {0};
    uint8_t* bytes = (uint8_t*)&decoder.decode_data;
    bytes[0] = first;
    bytes[1] = second;
    return subghz_protocol_blocks_get_hash_data(&decoder, 2U);
}

int main(void) {
    /* Both pairs collide with the previous XOR-only hash. */
    if(hash_two_bytes(0x01U, 0x02U) != 131U) return 1;
    if(hash_two_bytes(0x00U, 0x03U) != 93U) return 2;
    if(hash_two_bytes(0x02U, 0x01U) != 161U) return 3;
    if(hash_two_bytes(0x01U, 0x02U) == hash_two_bytes(0x00U, 0x03U)) return 4;
    if(hash_two_bytes(0x01U, 0x02U) == hash_two_bytes(0x02U, 0x01U)) return 5;
    return 0;
}
