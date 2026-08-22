/**
 * @file mf_classic_parser_util.h
 * @brief Shared helpers for the MIFARE Classic supported-card parsers.
 */
#pragma once

#include <nfc/protocols/mf_classic/mf_classic.h>

/**
 * @brief Tell whether a block holds data worth rendering.
 *
 * Known sector keys and blocks read are tracked independently. An unread
 * block is zero-filled, except in legacy dumps saved before the read mask was
 * introduced: their non-zero bytes are still valid data and must remain
 * parseable. A zero value is therefore trusted only when its read bit is set.
 *
 * @param[in] data card data to inspect.
 * @param[in] block_num data block to test; never pass a sector trailer.
 * A dictionary attack can place recovered keys in a trailer without reading
 * it, so its non-zero bytes are not evidence of card data.
 * @returns true when the block may be rendered.
 */
static inline bool mf_classic_parser_block_has_data(const MfClassicData* data, uint8_t block_num) {
    if(block_num >= mf_classic_get_total_block_num(data->type)) return false;
    if(mf_classic_is_block_read(data, block_num)) return true;

    for(size_t i = 0; i < MF_CLASSIC_BLOCK_SIZE; i++) {
        if(data->block[block_num].data[i] != 0) return true;
    }

    return false;
}
