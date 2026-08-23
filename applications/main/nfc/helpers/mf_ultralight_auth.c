#include "mf_ultralight_auth.h"

#include <furi.h>
#include <mbedtls/platform_util.h>
#include <mbedtls/sha1.h>

MfUltralightAuth* mf_ultralight_auth_alloc(void) {
    MfUltralightAuth* instance = calloc(1, sizeof(MfUltralightAuth));
    furi_check(instance);
    instance->outcome = MfUltralightAuthOutcomeNone;

    return instance;
}

void mf_ultralight_auth_free(MfUltralightAuth* instance) {
    furi_assert(instance);

    mbedtls_platform_zeroize(instance, sizeof(*instance));
    free(instance);
}

void mf_ultralight_auth_reset(MfUltralightAuth* instance) {
    furi_assert(instance);

    instance->type = MfUltralightAuthTypeNone;
    mbedtls_platform_zeroize(&instance->password, sizeof(instance->password));
    mbedtls_platform_zeroize(&instance->tdes_key, sizeof(instance->tdes_key));
    mbedtls_platform_zeroize(&instance->aes_key, sizeof(instance->aes_key));
    mbedtls_platform_zeroize(&instance->pack, sizeof(instance->pack));
}

bool mf_ultralight_generate_amiibo_pass(MfUltralightAuth* instance, uint8_t* uid, uint16_t uid_len) {
    furi_assert(instance);
    furi_assert(uid);

    bool generated = false;
    if(uid_len == 7) {
        instance->password.data[0] = uid[1] ^ uid[3] ^ 0xAA;
        instance->password.data[1] = uid[2] ^ uid[4] ^ 0x55;
        instance->password.data[2] = uid[3] ^ uid[5] ^ 0xAA;
        instance->password.data[3] = uid[4] ^ uid[6] ^ 0x55;
        generated = true;
    }

    return generated;
}

bool mf_ultralight_generate_xiaomi_pass(MfUltralightAuth* instance, uint8_t* uid, uint16_t uid_len) {
    furi_assert(instance);
    furi_assert(uid);

    uint8_t hash[20];
    bool generated = false;
    if(uid_len == 7) {
        if(mbedtls_sha1(uid, uid_len, hash) != 0) {
            mbedtls_platform_zeroize(hash, sizeof(hash));
            return false;
        }
        instance->password.data[0] = (hash[hash[0] % 20]);
        instance->password.data[1] = (hash[(hash[0] + 5) % 20]);
        instance->password.data[2] = (hash[(hash[0] + 13) % 20]);
        instance->password.data[3] = (hash[(hash[0] + 17) % 20]);
        generated = true;
    }

    mbedtls_platform_zeroize(hash, sizeof(hash));
    return generated;
}
