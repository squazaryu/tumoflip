#include "tumonet_crypto.h"

#include <mbedtls/aes.h>
#include <mbedtls/md.h>

#include <string.h>

static bool tumonet_crypto_hmac_sha256(
    const uint8_t* key,
    size_t key_size,
    const uint8_t* data,
    size_t data_size,
    uint8_t output[TUMONET_MAC_KEY_SIZE]) {
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if(info == NULL) return false;
    return mbedtls_md_hmac(info, key, key_size, data, data_size, output) == 0;
}

bool tumonet_crypto_derive_direction(
    const uint8_t master[TUMONET_MASTER_KEY_SIZE],
    uint8_t source,
    uint8_t destination,
    uint8_t encryption_key[TUMONET_ENC_KEY_SIZE],
    uint8_t authentication_key[TUMONET_MAC_KEY_SIZE]) {
    static const uint8_t encryption_label[] = "TumoNet v1 ENC";
    static const uint8_t authentication_label[] = "TumoNet v1 MAC";
    uint8_t input[sizeof(authentication_label) + 1U] = {0};
    uint8_t derived[TUMONET_MAC_KEY_SIZE] = {0};

    memcpy(input, encryption_label, sizeof(encryption_label) - 1U);
    input[sizeof(encryption_label) - 1U] = source;
    input[sizeof(encryption_label)] = destination;
    if(!tumonet_crypto_hmac_sha256(
           master, TUMONET_MASTER_KEY_SIZE, input, sizeof(encryption_label) + 1U, derived)) {
        tumonet_crypto_zero(input, sizeof(input));
        return false;
    }
    memcpy(encryption_key, derived, TUMONET_ENC_KEY_SIZE);

    memset(input, 0, sizeof(input));
    memcpy(input, authentication_label, sizeof(authentication_label) - 1U);
    input[sizeof(authentication_label) - 1U] = source;
    input[sizeof(authentication_label)] = destination;
    const bool ok = tumonet_crypto_hmac_sha256(
        master,
        TUMONET_MASTER_KEY_SIZE,
        input,
        sizeof(authentication_label) + 1U,
        authentication_key);

    tumonet_crypto_zero(input, sizeof(input));
    tumonet_crypto_zero(derived, sizeof(derived));
    return ok;
}

bool tumonet_crypto_crypt(
    const uint8_t key[TUMONET_ENC_KEY_SIZE],
    const uint8_t nonce[TUMONET_NONCE_SIZE],
    const uint8_t* input,
    uint8_t* output,
    size_t size) {
    if(size == 0U) return true;
    if(input == NULL || output == NULL) return false;

    mbedtls_aes_context context;
    uint8_t nonce_counter[TUMONET_NONCE_SIZE];
    uint8_t stream_block[TUMONET_NONCE_SIZE] = {0};
    size_t offset = 0U;
    memcpy(nonce_counter, nonce, sizeof(nonce_counter));

    mbedtls_aes_init(&context);
    bool ok = mbedtls_aes_setkey_enc(&context, key, TUMONET_ENC_KEY_SIZE * 8U) == 0;
    if(ok) {
        ok = mbedtls_aes_crypt_ctr(
                 &context, size, &offset, nonce_counter, stream_block, input, output) == 0;
    }
    mbedtls_aes_free(&context);
    tumonet_crypto_zero(nonce_counter, sizeof(nonce_counter));
    tumonet_crypto_zero(stream_block, sizeof(stream_block));
    return ok;
}

bool tumonet_crypto_tag(
    const uint8_t key[TUMONET_MAC_KEY_SIZE],
    const uint8_t* data,
    size_t size,
    uint8_t tag[TUMONET_TAG_SIZE]) {
    uint8_t full_tag[TUMONET_MAC_KEY_SIZE] = {0};
    const bool ok = tumonet_crypto_hmac_sha256(key, TUMONET_MAC_KEY_SIZE, data, size, full_tag);
    if(ok) memcpy(tag, full_tag, TUMONET_TAG_SIZE);
    tumonet_crypto_zero(full_tag, sizeof(full_tag));
    return ok;
}

bool tumonet_crypto_tag_equal(
    const uint8_t left[TUMONET_TAG_SIZE],
    const uint8_t right[TUMONET_TAG_SIZE]) {
    uint8_t difference = 0U;
    for(size_t i = 0; i < TUMONET_TAG_SIZE; i++) {
        difference |= left[i] ^ right[i];
    }
    return difference == 0U;
}

void tumonet_crypto_zero(void* data, size_t size) {
    volatile uint8_t* bytes = data;
    while(size-- > 0U) {
        *bytes++ = 0U;
    }
}
