#include "arf_keeloq_keys.h"

#include <furi.h>
#include <lib/subghz/types.h>

struct ArfKeeloqKeys {
    SubGhzKeystore* user_keystore;
    SubGhzKeystore* system_keystore;
    size_t user_count;
};

ArfKeeloqKeys* arf_keeloq_keys_alloc(void) {
    ArfKeeloqKeys* keys = malloc(sizeof(ArfKeeloqKeys));

    keys->user_keystore = subghz_keystore_alloc();
    subghz_keystore_load(keys->user_keystore, SUBGHZ_KEYSTORE_DIR_USER_NAME);
    keys->user_count = SubGhzKeyArray_size(*subghz_keystore_get_data(keys->user_keystore));

    keys->system_keystore = subghz_keystore_alloc();
    subghz_keystore_load(keys->system_keystore, SUBGHZ_KEYSTORE_DIR_NAME);

    return keys;
}

void arf_keeloq_keys_free(ArfKeeloqKeys* keys) {
    furi_assert(keys);
    subghz_keystore_free(keys->user_keystore);
    subghz_keystore_free(keys->system_keystore);
    free(keys);
}

size_t arf_keeloq_keys_count(ArfKeeloqKeys* keys) {
    furi_assert(keys);
    return keys->user_count +
           SubGhzKeyArray_size(*subghz_keystore_get_data(keys->system_keystore));
}

size_t arf_keeloq_keys_user_count(ArfKeeloqKeys* keys) {
    furi_assert(keys);
    return keys->user_count;
}

SubGhzKey* arf_keeloq_keys_get(ArfKeeloqKeys* keys, size_t index) {
    furi_assert(keys);
    if(index < keys->user_count) {
        return SubGhzKeyArray_get(*subghz_keystore_get_data(keys->user_keystore), index);
    }

    size_t system_index = index - keys->user_count;
    furi_assert(
        system_index < SubGhzKeyArray_size(*subghz_keystore_get_data(keys->system_keystore)));
    return SubGhzKeyArray_get(*subghz_keystore_get_data(keys->system_keystore), system_index);
}
