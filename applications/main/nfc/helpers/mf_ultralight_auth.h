#pragma once

#include <lib/nfc/protocols/mf_ultralight/mf_ultralight.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MfUltralightAuthTypeNone,
    MfUltralightAuthTypeReader,
    MfUltralightAuthTypeManual,
    MfUltralightAuthTypeXiaomi,
    MfUltralightAuthTypeAmiibo,
} MfUltralightAuthType;

/** Result of the authentication requested by the current read/unlock flow. */
typedef enum {
    MfUltralightAuthOutcomeNone,
    MfUltralightAuthOutcomeSuccess,
    MfUltralightAuthOutcomeFailed,
    MfUltralightAuthOutcomeSkippedUid,
} MfUltralightAuthOutcome;

typedef struct {
    MfUltralightAuthType type;
    MfUltralightAuthPassword password;
    MfUltralightC3DesAuthKey tdes_key;
    MfUltralightAesKey aes_key;
    MfUltralightAuthPack pack;
    // Cleared when a new read/attack starts, not by auth_reset(), because reset runs after the
    // read result is rendered and only scrubs credentials.
    MfUltralightAuthOutcome outcome;
} MfUltralightAuth;

MfUltralightAuth* mf_ultralight_auth_alloc(void);

void mf_ultralight_auth_free(MfUltralightAuth* instance);

void mf_ultralight_auth_reset(MfUltralightAuth* instance);

bool mf_ultralight_generate_amiibo_pass(MfUltralightAuth* instance, uint8_t* uid, uint16_t uid_len);

bool mf_ultralight_generate_xiaomi_pass(MfUltralightAuth* instance, uint8_t* uid, uint16_t uid_len);

#ifdef __cplusplus
}
#endif
