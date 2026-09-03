/**
 * @file nfc_key_dict.h
 * @brief Description of a user key dictionary, shared by the key management scenes.
 *
 * MIFARE Classic, Plus, Ultralight C and Ultralight AES all offer the same
 * add/list/delete flow over their own dictionary file. The scenes are generic;
 * this is the only thing that differs between them.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <nfc/nfc_device.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    /**
     * @brief Reserved: NfcApp is zero-filled, so an unset selector lands here.
     *
     * Without it a scene entered without setting the type would silently act on the MIFARE
     * Classic dictionary - including Delete.
     */
    NfcKeyDictTypeNone,

    NfcKeyDictTypeMfClassic,
    NfcKeyDictTypeMfPlus,
    NfcKeyDictTypeMfUltralightC,
    NfcKeyDictTypeMfUltralightAes,

    NfcKeyDictTypeNum,
} NfcKeyDictType;

typedef struct {
    const char* title; /**< Header shown on the dictionary screen. */
    const char* system_path; /**< Read-only dictionary shipped with the firmware. */
    const char* user_path; /**< User dictionary, the one add/delete operate on. */
    size_t key_size; /**< Key length in bytes, bounded by NFC_BYTE_INPUT_STORE_SIZE
                          (enforced in nfc_key_dict.c). */
} NfcKeyDict;

/** MIFARE Classic 4K has 40 sectors with one Key A and one Key B each. */
#define NFC_KEY_DICT_DEVICE_KEYS_MAX (80)

typedef enum {
    NfcKeyDictImportStatusSuccess,
    NfcKeyDictImportStatusSystemDictionaryMissing,
    NfcKeyDictImportStatusDictionaryReadFailed,
    NfcKeyDictImportStatusBackupFailed,
    NfcKeyDictImportStatusWriteFailed,
    NfcKeyDictImportStatusRollbackFailed,
} NfcKeyDictImportStatus;

typedef struct {
    size_t candidates;
    size_t added;
    size_t known;
    NfcKeyDictImportStatus status;
    bool backup_preserved;
} NfcKeyDictImportStats;

const NfcKeyDict* nfc_key_dict(NfcKeyDictType type);

/** Collect distinct keys marked as recovered in the loaded device. */
size_t nfc_key_dict_collect_from_device(
    NfcKeyDictType type,
    const NfcDevice* device,
    uint8_t* keys,
    size_t keys_max);

/** Import new keys transactionally without replacing existing dictionary contents on failure. */
void nfc_key_dict_import(
    NfcKeyDictType type,
    const uint8_t* keys,
    size_t key_count,
    NfcKeyDictImportStats* stats);

#ifdef __cplusplus
}
#endif
