#include "nfc_key_dict.h"

#include "../nfc_app_i.h"

#include <furi.h>
#include <nfc/protocols/mf_classic/mf_classic.h>
#include <nfc/protocols/mf_plus/mf_plus.h>
#include <nfc/protocols/mf_ultralight/mf_ultralight.h>

#define TAG "NfcKeyDict"

_Static_assert(
    NFC_KEY_DICT_DEVICE_KEYS_MAX >= MF_CLASSIC_TOTAL_SECTORS_MAX * 2,
    "A 4K card can hand over two keys per sector");

static const NfcKeyDict nfc_key_dicts[NfcKeyDictTypeNum] = {
    [NfcKeyDictTypeMfClassic] =
        {
            .title = "MIFARE Classic Keys",
            .system_path = NFC_APP_MF_CLASSIC_DICT_SYSTEM_PATH,
            .user_path = NFC_APP_MF_CLASSIC_DICT_USER_PATH,
            .key_size = sizeof(MfClassicKey),
        },
    [NfcKeyDictTypeMfPlus] =
        {
            .title = "MIFARE Plus Keys",
            .system_path = NFC_APP_MF_PLUS_DICT_SYSTEM_PATH,
            .user_path = NFC_APP_MF_PLUS_DICT_USER_PATH,
            .key_size = sizeof(MfPlusKey),
        },
    [NfcKeyDictTypeMfUltralightC] =
        {
            .title = "MIFARE Ultralight C Keys",
            .system_path = NFC_APP_MF_ULTRALIGHT_C_DICT_SYSTEM_PATH,
            .user_path = NFC_APP_MF_ULTRALIGHT_C_DICT_USER_PATH,
            .key_size = sizeof(MfUltralightC3DesAuthKey),
        },
    [NfcKeyDictTypeMfUltralightAes] =
        {
            .title = "MIFARE Ultralight AES Keys",
            .system_path = NFC_APP_MF_ULTRALIGHT_AES_DICT_SYSTEM_PATH,
            .user_path = NFC_APP_MF_ULTRALIGHT_AES_DICT_USER_PATH,
            .key_size = sizeof(MfUltralightAesKey),
        },
};

// The key scenes read into uint8_t[NFC_BYTE_INPUT_STORE_SIZE] stack buffers using key_size as the
// length, and byte input writes into a store of the same size. Adding a longer key below - a
// 24-byte 3K3DES, say - must break the build here rather than smash a stack at runtime.
static_assert(sizeof(MfClassicKey) <= NFC_BYTE_INPUT_STORE_SIZE, "MfClassicKey too long");
static_assert(sizeof(MfPlusKey) <= NFC_BYTE_INPUT_STORE_SIZE, "MfPlusKey too long");
static_assert(
    sizeof(MfUltralightC3DesAuthKey) <= NFC_BYTE_INPUT_STORE_SIZE,
    "MfUltralightC3DesAuthKey too long");
static_assert(
    sizeof(MfUltralightAesKey) <= NFC_BYTE_INPUT_STORE_SIZE,
    "MfUltralightAesKey too long");

const NfcKeyDict* nfc_key_dict(NfcKeyDictType type) {
    furi_check(type > NfcKeyDictTypeNone && type < NfcKeyDictTypeNum);

    const NfcKeyDict* dict = &nfc_key_dicts[type];
    // Every key scene writes key_size bytes into the shared byte-input store.
    // Keep the check next to the table so a future row cannot silently exceed it.
    furi_check(dict->key_size > 0 && dict->key_size <= NFC_BYTE_INPUT_STORE_SIZE);

    return dict;
}

static size_t nfc_key_dict_push_unique(
    uint8_t* keys,
    size_t key_count,
    size_t keys_max,
    size_t key_size,
    const uint8_t* key) {
    if(key_count == keys_max) return key_count;

    for(size_t i = 0; i < key_count; i++) {
        if(memcmp(keys + i * key_size, key, key_size) == 0) return key_count;
    }

    memcpy(keys + key_count * key_size, key, key_size);
    return key_count + 1;
}

static size_t nfc_key_dict_collect_mf_classic(
    const NfcDevice* device,
    uint8_t* keys,
    size_t keys_max,
    size_t key_size) {
    const MfClassicData* data = nfc_device_get_data(device, NfcProtocolMfClassic);
    const uint8_t sector_num = mf_classic_get_total_sectors_num(data->type);

    size_t key_count = 0;
    for(uint8_t sector = 0; sector < sector_num; sector++) {
        const MfClassicSectorTrailer* sec_tr =
            mf_classic_get_sector_trailer_by_sector(data, sector);

        if(FURI_BIT(data->key_a_mask, sector)) {
            key_count =
                nfc_key_dict_push_unique(keys, key_count, keys_max, key_size, sec_tr->key_a.data);
        }
        if(FURI_BIT(data->key_b_mask, sector)) {
            key_count =
                nfc_key_dict_push_unique(keys, key_count, keys_max, key_size, sec_tr->key_b.data);
        }
    }

    return key_count;
}

size_t nfc_key_dict_collect_from_device(
    NfcKeyDictType type,
    const NfcDevice* device,
    uint8_t* keys,
    size_t keys_max) {
    furi_check(device);
    furi_check(keys);
    furi_check(keys_max > 0 && keys_max <= NFC_KEY_DICT_DEVICE_KEYS_MAX);

    furi_check(type == NfcKeyDictTypeMfClassic);
    const NfcKeyDict* dict = nfc_key_dict(type);
    furi_check(nfc_device_get_protocol(device) == NfcProtocolMfClassic);

    return nfc_key_dict_collect_mf_classic(device, keys, keys_max, dict->key_size);
}
