#include "arf_subghz_keeloq_keys.h"

#include <flipper_format/flipper_format.h>
#include <flipper_format/flipper_format_i.h>
#include <furi.h>
#include <lib/subghz/types.h>
#include <storage/storage.h>
#include <toolbox/stream/stream.h>

#define TAG "ArfSubGhzKeeloqKeys"

#define ARF_SUBGHZ_KEYSTORE_FILE_TYPE    "Flipper SubGhz Keystore File"
#define ARF_SUBGHZ_KEYSTORE_FILE_VERSION 0

struct ArfSubGhzKeeloqKeys {
    SubGhzKeystore* user_keystore;
    SubGhzKeystore* system_keystore;
    size_t user_count;
};

ArfSubGhzKeeloqKeys* arf_subghz_keeloq_keys_alloc(void) {
    ArfSubGhzKeeloqKeys* instance = malloc(sizeof(ArfSubGhzKeeloqKeys));

    instance->user_keystore = subghz_keystore_alloc();
    subghz_keystore_load(instance->user_keystore, SUBGHZ_KEYSTORE_DIR_USER_NAME);
    instance->user_count =
        SubGhzKeyArray_size(*subghz_keystore_get_data(instance->user_keystore));

    instance->system_keystore = subghz_keystore_alloc();
    subghz_keystore_load(instance->system_keystore, SUBGHZ_KEYSTORE_DIR_NAME);

    return instance;
}

void arf_subghz_keeloq_keys_free(ArfSubGhzKeeloqKeys* instance) {
    furi_assert(instance);

    subghz_keystore_free(instance->user_keystore);
    subghz_keystore_free(instance->system_keystore);
    free(instance);
}

size_t arf_subghz_keeloq_keys_count(ArfSubGhzKeeloqKeys* instance) {
    furi_assert(instance);

    return instance->user_count +
           SubGhzKeyArray_size(*subghz_keystore_get_data(instance->system_keystore));
}

size_t arf_subghz_keeloq_keys_user_count(ArfSubGhzKeeloqKeys* instance) {
    furi_assert(instance);

    return instance->user_count;
}

SubGhzKey* arf_subghz_keeloq_keys_get(ArfSubGhzKeeloqKeys* instance, size_t index) {
    furi_assert(instance);

    if(index < instance->user_count) {
        return SubGhzKeyArray_get(*subghz_keystore_get_data(instance->user_keystore), index);
    }

    const size_t system_index = index - instance->user_count;
    furi_assert(system_index < SubGhzKeyArray_size(
                                   *subghz_keystore_get_data(instance->system_keystore)));
    return SubGhzKeyArray_get(*subghz_keystore_get_data(instance->system_keystore), system_index);
}

void arf_subghz_keeloq_keys_add(
    ArfSubGhzKeeloqKeys* instance,
    uint64_t key,
    uint16_t type,
    const char* name) {
    furi_assert(instance);

    SubGhzKey* entry = SubGhzKeyArray_push_raw(*subghz_keystore_get_data(instance->user_keystore));
    entry->name = furi_string_alloc_set(name);
    entry->key = key;
    entry->type = type;
    instance->user_count++;
}

void arf_subghz_keeloq_keys_set(
    ArfSubGhzKeeloqKeys* instance,
    size_t index,
    uint64_t key,
    uint16_t type,
    const char* name) {
    furi_assert(instance);
    furi_assert(index < instance->user_count);

    SubGhzKey* entry = SubGhzKeyArray_get(*subghz_keystore_get_data(instance->user_keystore), index);
    furi_string_set_str(entry->name, name);
    entry->key = key;
    entry->type = type;
}

void arf_subghz_keeloq_keys_delete(ArfSubGhzKeeloqKeys* instance, size_t index) {
    furi_assert(instance);
    furi_assert(index < instance->user_count);

    SubGhzKeyArray_t* keys = subghz_keystore_get_data(instance->user_keystore);
    SubGhzKey* removed_entry = SubGhzKeyArray_get(*keys, index);
    furi_string_free(removed_entry->name);

    for(size_t cursor = index; cursor + 1 < instance->user_count; cursor++) {
        *SubGhzKeyArray_get(*keys, cursor) = *SubGhzKeyArray_get(*keys, cursor + 1);
    }

    SubGhzKey discarded_entry;
    SubGhzKeyArray_pop_back(&discarded_entry, *keys);
    UNUSED(discarded_entry);
    instance->user_count--;
}

bool arf_subghz_keeloq_keys_save(ArfSubGhzKeeloqKeys* instance) {
    furi_assert(instance);

    bool saved = false;
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* format = flipper_format_file_alloc(storage);

    do {
        storage_simply_mkdir(storage, EXT_PATH("subghz/assets"));

        if(!flipper_format_file_open_always(format, SUBGHZ_KEYSTORE_DIR_USER_NAME)) {
            FURI_LOG_E(TAG, "Cannot open user keystore");
            break;
        }

        if(!flipper_format_write_header_cstr(
               format, ARF_SUBGHZ_KEYSTORE_FILE_TYPE, ARF_SUBGHZ_KEYSTORE_FILE_VERSION)) {
            FURI_LOG_E(TAG, "Cannot write user keystore header");
            break;
        }

        uint32_t encryption = 0;
        if(!flipper_format_write_uint32(format, "Encryption", &encryption, 1)) {
            FURI_LOG_E(TAG, "Cannot write user keystore encryption");
            break;
        }

        Stream* stream = flipper_format_get_raw_stream(format);
        saved = true;

        SubGhzKeyArray_t* keys = subghz_keystore_get_data(instance->user_keystore);
        for(size_t index = 0; index < instance->user_count; index++) {
            SubGhzKey* entry = SubGhzKeyArray_get(*keys, index);
            const size_t written = stream_write_format(
                stream,
                "%016llX:%hu:%s\r\n",
                (unsigned long long)entry->key,
                entry->type,
                furi_string_get_cstr(entry->name));

            if(written == 0) {
                FURI_LOG_E(TAG, "Cannot write user keystore entry");
                saved = false;
                break;
            }
        }
    } while(false);

    flipper_format_free(format);
    furi_record_close(RECORD_STORAGE);

    return saved;
}
