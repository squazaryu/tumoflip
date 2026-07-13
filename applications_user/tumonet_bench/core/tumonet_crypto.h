#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TUMONET_MASTER_KEY_SIZE 32U
#define TUMONET_ENC_KEY_SIZE    16U
#define TUMONET_MAC_KEY_SIZE    32U
#define TUMONET_NONCE_SIZE      16U
#define TUMONET_TAG_SIZE        16U

bool tumonet_crypto_derive_direction(
    const uint8_t master[TUMONET_MASTER_KEY_SIZE],
    uint8_t source,
    uint8_t destination,
    uint8_t encryption_key[TUMONET_ENC_KEY_SIZE],
    uint8_t authentication_key[TUMONET_MAC_KEY_SIZE]);

bool tumonet_crypto_crypt(
    const uint8_t key[TUMONET_ENC_KEY_SIZE],
    const uint8_t nonce[TUMONET_NONCE_SIZE],
    const uint8_t* input,
    uint8_t* output,
    size_t size);

bool tumonet_crypto_tag(
    const uint8_t key[TUMONET_MAC_KEY_SIZE],
    const uint8_t* data,
    size_t size,
    uint8_t tag[TUMONET_TAG_SIZE]);

bool tumonet_crypto_tag_equal(
    const uint8_t left[TUMONET_TAG_SIZE],
    const uint8_t right[TUMONET_TAG_SIZE]);

void tumonet_crypto_zero(void* data, size_t size);
