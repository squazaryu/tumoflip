#include "mf_ultralight_aes_crypto.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <mbedtls/aes.h>
#include <mbedtls/platform_util.h>

#define MF_ULTRALIGHT_AES_CRYPTO_KEY_SIZE   (16U)
#define MF_ULTRALIGHT_AES_CRYPTO_BLOCK_SIZE (16U)
#define MF_ULTRALIGHT_AES_CRYPTO_MAC_SIZE   (8U)
#define MF_ULTRALIGHT_AES_CMAC_MSG_MAX      (256U)

void mf_ultralight_aes_rol16(uint8_t* data) {
    if(!data) return;

    const uint8_t first = data[0];
    for(size_t i = 1; i < MF_ULTRALIGHT_AES_CRYPTO_BLOCK_SIZE; i++) {
        data[i - 1] = data[i];
    }
    data[MF_ULTRALIGHT_AES_CRYPTO_BLOCK_SIZE - 1] = first;
}

static void mf_ultralight_aes_ror16(uint8_t* data) {
    const uint8_t last = data[MF_ULTRALIGHT_AES_CRYPTO_BLOCK_SIZE - 1];
    for(size_t i = MF_ULTRALIGHT_AES_CRYPTO_BLOCK_SIZE - 1; i > 0; i--) {
        data[i] = data[i - 1];
    }
    data[0] = last;
}

bool mf_ultralight_aes_equal(const uint8_t* left, const uint8_t* right, size_t length) {
    if(!left || !right) return false;

    uint8_t difference = 0;
    for(size_t i = 0; i < length; i++) {
        difference |= left[i] ^ right[i];
    }
    return difference == 0;
}

static uint8_t mf_ultralight_aes_cmac_input_byte(
    const uint8_t* prefix,
    size_t prefix_len,
    const uint8_t* data,
    size_t index) {
    return (index < prefix_len) ? prefix[index] : data[index - prefix_len];
}

static bool mf_ultralight_aes_cmac_parts(
    const uint8_t* key,
    const uint8_t* prefix,
    size_t prefix_len,
    const uint8_t* data,
    size_t data_len,
    uint8_t* mac) {
    if(!key || !mac || (prefix_len && !prefix) || (data_len && !data) ||
       data_len > SIZE_MAX - prefix_len) {
        if(mac) mbedtls_platform_zeroize(mac, MF_ULTRALIGHT_AES_CRYPTO_BLOCK_SIZE);
        return false;
    }

    mbedtls_aes_context context;
    mbedtls_aes_init(&context);

    uint8_t l[MF_ULTRALIGHT_AES_CRYPTO_BLOCK_SIZE] = {0};
    uint8_t k1[MF_ULTRALIGHT_AES_CRYPTO_BLOCK_SIZE] = {0};
    uint8_t k2[MF_ULTRALIGHT_AES_CRYPTO_BLOCK_SIZE] = {0};
    uint8_t block[MF_ULTRALIGHT_AES_CRYPTO_BLOCK_SIZE] = {0};
    uint8_t chain[MF_ULTRALIGHT_AES_CRYPTO_BLOCK_SIZE] = {0};
    uint8_t encrypted[MF_ULTRALIGHT_AES_CRYPTO_BLOCK_SIZE] = {0};
    bool success = false;

    do {
        if(mbedtls_aes_setkey_enc(&context, key, 128) != 0) break;
        if(mbedtls_aes_crypt_ecb(&context, MBEDTLS_AES_ENCRYPT, l, l) != 0) break;

        uint8_t carry = 0;
        for(size_t i = MF_ULTRALIGHT_AES_CRYPTO_BLOCK_SIZE; i > 0; i--) {
            const size_t index = i - 1;
            k1[index] = (uint8_t)((l[index] << 1) | carry);
            carry = (l[index] & 0x80U) ? 1U : 0U;
        }
        if(l[0] & 0x80U) k1[MF_ULTRALIGHT_AES_CRYPTO_BLOCK_SIZE - 1] ^= 0x87U;

        carry = 0;
        for(size_t i = MF_ULTRALIGHT_AES_CRYPTO_BLOCK_SIZE; i > 0; i--) {
            const size_t index = i - 1;
            k2[index] = (uint8_t)((k1[index] << 1) | carry);
            carry = (k1[index] & 0x80U) ? 1U : 0U;
        }
        if(k1[0] & 0x80U) k2[MF_ULTRALIGHT_AES_CRYPTO_BLOCK_SIZE - 1] ^= 0x87U;

        const size_t total_len = prefix_len + data_len;
        size_t block_count =
            (total_len + MF_ULTRALIGHT_AES_CRYPTO_BLOCK_SIZE - 1) /
            MF_ULTRALIGHT_AES_CRYPTO_BLOCK_SIZE;
        const bool complete =
            total_len != 0 && (total_len % MF_ULTRALIGHT_AES_CRYPTO_BLOCK_SIZE) == 0;
        if(block_count == 0) block_count = 1;

        bool crypto_ok = true;
        for(size_t block_index = 0; block_index + 1 < block_count; block_index++) {
            for(size_t i = 0; i < MF_ULTRALIGHT_AES_CRYPTO_BLOCK_SIZE; i++) {
                const size_t input_index =
                    block_index * MF_ULTRALIGHT_AES_CRYPTO_BLOCK_SIZE + i;
                block[i] = chain[i] ^
                           mf_ultralight_aes_cmac_input_byte(
                               prefix, prefix_len, data, input_index);
            }
            if(mbedtls_aes_crypt_ecb(
                   &context, MBEDTLS_AES_ENCRYPT, block, encrypted) != 0) {
                crypto_ok = false;
                break;
            }
            memcpy(chain, encrypted, sizeof(chain));
        }
        if(!crypto_ok) break;

        mbedtls_platform_zeroize(block, sizeof(block));
        const size_t last_offset =
            (block_count - 1) * MF_ULTRALIGHT_AES_CRYPTO_BLOCK_SIZE;
        const size_t remainder = total_len - last_offset;
        for(size_t i = 0; i < remainder; i++) {
            block[i] =
                mf_ultralight_aes_cmac_input_byte(prefix, prefix_len, data, last_offset + i);
        }
        if(!complete) block[remainder] = 0x80U;

        const uint8_t* subkey = complete ? k1 : k2;
        for(size_t i = 0; i < MF_ULTRALIGHT_AES_CRYPTO_BLOCK_SIZE; i++) {
            block[i] ^= chain[i] ^ subkey[i];
        }

        success =
            mbedtls_aes_crypt_ecb(&context, MBEDTLS_AES_ENCRYPT, block, mac) == 0;
    } while(false);

    if(!success) mbedtls_platform_zeroize(mac, MF_ULTRALIGHT_AES_CRYPTO_BLOCK_SIZE);
    mbedtls_platform_zeroize(l, sizeof(l));
    mbedtls_platform_zeroize(k1, sizeof(k1));
    mbedtls_platform_zeroize(k2, sizeof(k2));
    mbedtls_platform_zeroize(block, sizeof(block));
    mbedtls_platform_zeroize(chain, sizeof(chain));
    mbedtls_platform_zeroize(encrypted, sizeof(encrypted));
    mbedtls_aes_free(&context);
    return success;
}

bool mf_ultralight_aes_cmac(
    const uint8_t* key,
    const uint8_t* data,
    size_t length,
    uint8_t* mac) {
    return mf_ultralight_aes_cmac_parts(key, NULL, 0, data, length, mac);
}

bool mf_ultralight_aes_cmac8_ctr(
    const uint8_t* key,
    uint16_t counter,
    const uint8_t* data,
    size_t length,
    uint8_t* mac) {
    if(!mac || length > MF_ULTRALIGHT_AES_CMAC_MSG_MAX) {
        if(mac) mbedtls_platform_zeroize(mac, MF_ULTRALIGHT_AES_CRYPTO_MAC_SIZE);
        return false;
    }

    const uint8_t counter_le[2] = {
        (uint8_t)(counter & 0xFFU),
        (uint8_t)((counter >> 8) & 0xFFU),
    };
    uint8_t full_mac[MF_ULTRALIGHT_AES_CRYPTO_BLOCK_SIZE] = {0};
    const bool success =
        mf_ultralight_aes_cmac_parts(key, counter_le, sizeof(counter_le), data, length, full_mac);
    if(success) {
        for(size_t i = 0; i < MF_ULTRALIGHT_AES_CRYPTO_MAC_SIZE; i++) {
            mac[i] = full_mac[2 * i + 1];
        }
    } else {
        mbedtls_platform_zeroize(mac, MF_ULTRALIGHT_AES_CRYPTO_MAC_SIZE);
    }
    mbedtls_platform_zeroize(full_mac, sizeof(full_mac));
    return success;
}

bool mf_ultralight_aes_derive_session_key(
    const uint8_t* key,
    const uint8_t* rnd_a_rot,
    const uint8_t* rnd_b_rot,
    uint8_t* session_key) {
    if(!key || !rnd_a_rot || !rnd_b_rot || !session_key) {
        if(session_key) mbedtls_platform_zeroize(session_key, MF_ULTRALIGHT_AES_CRYPTO_KEY_SIZE);
        return false;
    }

    uint8_t rnd_a[MF_ULTRALIGHT_AES_CRYPTO_BLOCK_SIZE];
    uint8_t rnd_b[MF_ULTRALIGHT_AES_CRYPTO_BLOCK_SIZE];
    memcpy(rnd_a, rnd_a_rot, sizeof(rnd_a));
    memcpy(rnd_b, rnd_b_rot, sizeof(rnd_b));
    mf_ultralight_aes_ror16(rnd_a);
    mf_ultralight_aes_ror16(rnd_b);

    uint8_t sv2[2 * MF_ULTRALIGHT_AES_CRYPTO_BLOCK_SIZE] = {
        0x5a,
        0xa5,
        0x00,
        0x01,
        0x00,
        0x80,
        rnd_a[0],
        rnd_a[1],
        (uint8_t)(rnd_a[2] ^ rnd_b[0]),
        (uint8_t)(rnd_a[3] ^ rnd_b[1]),
        (uint8_t)(rnd_a[4] ^ rnd_b[2]),
        (uint8_t)(rnd_a[5] ^ rnd_b[3]),
        (uint8_t)(rnd_a[6] ^ rnd_b[4]),
        (uint8_t)(rnd_a[7] ^ rnd_b[5]),
        rnd_b[6],
        rnd_b[7],
        rnd_b[8],
        rnd_b[9],
        rnd_b[10],
        rnd_b[11],
        rnd_b[12],
        rnd_b[13],
        rnd_b[14],
        rnd_b[15],
        rnd_a[8],
        rnd_a[9],
        rnd_a[10],
        rnd_a[11],
        rnd_a[12],
        rnd_a[13],
        rnd_a[14],
        rnd_a[15],
    };

    const bool success =
        mf_ultralight_aes_cmac(key, sv2, sizeof(sv2), session_key);
    mbedtls_platform_zeroize(rnd_a, sizeof(rnd_a));
    mbedtls_platform_zeroize(rnd_b, sizeof(rnd_b));
    mbedtls_platform_zeroize(sv2, sizeof(sv2));
    return success;
}
