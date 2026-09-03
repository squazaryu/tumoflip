#include "nfc_key_dict.h"

#include <furi.h>
#include <storage/storage.h>
#include <toolbox/args.h>
#include <toolbox/path.h>
#include <toolbox/stream/file_stream.h>

#define TAG "NfcKeyDictImport"

// KeysDict opens files read/write and normalizes a missing final newline. Import scans must not
// mutate either input, so use a read-only stream and check its final I/O status explicitly.
static bool nfc_key_dict_mark_path_present(
    Storage* storage,
    const char* path,
    const uint8_t* keys,
    size_t key_count,
    size_t key_size,
    bool* known,
    size_t* known_count,
    size_t* scanned_count) {
    Stream* stream = file_stream_alloc(storage);
    FuriString* line = furi_string_alloc();
    uint8_t* dict_key = malloc(key_size);
    bool success = false;

    if(file_stream_open(stream, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        while(stream_read_line(stream, line)) {
            if(furi_string_size(line) < key_size * 2 || furi_string_get_char(line, 0) == '#') {
                continue;
            }

            bool valid = true;
            for(size_t byte = 0; byte < key_size; byte++) {
                if(!args_char_to_hex(
                       furi_string_get_char(line, byte * 2),
                       furi_string_get_char(line, byte * 2 + 1),
                       &dict_key[byte])) {
                    valid = false;
                    break;
                }
            }
            if(!valid) continue;
            (*scanned_count)++;

            for(size_t i = 0; i < key_count; i++) {
                if(known[i] || memcmp(keys + i * key_size, dict_key, key_size) != 0) continue;
                known[i] = true;
                (*known_count)++;
                break;
            }
        }
        success = file_stream_get_error(stream) == FSE_OK;
        const bool closed = file_stream_close(stream);
        success = success && closed;
    }

    free(dict_key);
    furi_string_free(line);
    stream_free(stream);
    return success;
}

static bool nfc_key_dict_sync_size(Storage* storage, const char* path, uint64_t expected_size) {
    File* file = storage_file_alloc(storage);
    bool success = false;
    if(storage_file_open(file, path, FSAM_READ_WRITE, FSOM_OPEN_EXISTING)) {
        success = storage_file_size(file) == expected_size && storage_file_sync(file);
        const bool closed = storage_file_close(file);
        success = success && closed;
    }
    storage_file_free(file);
    return success;
}

static bool nfc_key_dict_create_backup(
    Storage* storage,
    const char* user_path,
    uint64_t original_size,
    FuriString* backup_path) {
    FuriString* directory = furi_string_alloc();
    FuriString* backup_name = furi_string_alloc();

    path_extract_dirname(user_path, directory);
    storage_get_next_filename(
        storage, furi_string_get_cstr(directory), ".nfc_key_import", ".bak", backup_name, 255);
    furi_string_printf(
        backup_path,
        "%s/%s.bak",
        furi_string_get_cstr(directory),
        furi_string_get_cstr(backup_name));

    const bool success =
        storage_common_copy(storage, user_path, furi_string_get_cstr(backup_path)) == FSE_OK &&
        nfc_key_dict_sync_size(storage, furi_string_get_cstr(backup_path), original_size);
    if(!success && storage_file_exists(storage, furi_string_get_cstr(backup_path))) {
        storage_common_remove(storage, furi_string_get_cstr(backup_path));
    }

    furi_string_free(backup_name);
    furi_string_free(directory);
    return success;
}

static bool nfc_key_dict_restore_original(
    Storage* storage,
    const char* user_path,
    bool user_existed,
    uint64_t original_size,
    FuriString* backup_path) {
    if(!user_existed) {
        if(storage_file_exists(storage, user_path)) storage_common_remove(storage, user_path);
        return !storage_file_exists(storage, user_path);
    }

    bool restored = false;
    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, user_path, FSAM_READ_WRITE, FSOM_OPEN_EXISTING)) {
        restored = storage_file_seek(file, (uint32_t)original_size, true) &&
                   storage_file_truncate(file) && storage_file_sync(file);
        const bool closed = storage_file_close(file);
        restored = restored && closed;
    }
    storage_file_free(file);

    if(restored) {
        FileInfo restored_info;
        restored = storage_common_stat(storage, user_path, &restored_info) == FSE_OK &&
                   restored_info.size == original_size;
    }

    // Storage's rename is remove+copy, not an atomic filesystem rename. Keep the backup until
    // the fallback copy has been synchronized and its size verified.
    if(!restored && !furi_string_empty(backup_path) &&
       storage_file_exists(storage, furi_string_get_cstr(backup_path))) {
        const bool destination_ready = !storage_file_exists(storage, user_path) ||
                                       storage_common_remove(storage, user_path) == FSE_OK;
        restored = destination_ready &&
                   storage_common_copy(storage, furi_string_get_cstr(backup_path), user_path) ==
                       FSE_OK &&
                   nfc_key_dict_sync_size(storage, user_path, original_size);
    }

    return restored;
}

static NfcKeyDictImportStatus nfc_key_dict_append_transaction(
    Storage* storage,
    const NfcKeyDict* dict,
    const uint8_t* keys,
    size_t key_count,
    const bool* known,
    NfcKeyDictImportStats* stats) {
    size_t keys_to_add = 0;
    for(size_t i = 0; i < key_count; i++) {
        if(!known[i]) keys_to_add++;
    }
    if(keys_to_add == 0) return NfcKeyDictImportStatusSuccess;

    FileInfo original_info = {};
    const FS_Error stat_result = storage_common_stat(storage, dict->user_path, &original_info);
    const bool user_existed = stat_result == FSE_OK;
    if((stat_result != FSE_OK && stat_result != FSE_NOT_EXIST) ||
       original_info.size > UINT32_MAX) {
        return NfcKeyDictImportStatusDictionaryReadFailed;
    }

    FuriString* backup_path = furi_string_alloc();
    if(user_existed &&
       !nfc_key_dict_create_backup(storage, dict->user_path, original_info.size, backup_path)) {
        furi_string_free(backup_path);
        return NfcKeyDictImportStatusBackupFailed;
    }

    FuriString* append_data = furi_string_alloc();
    File* file = storage_file_alloc(storage);
    bool file_open = false;
    bool needs_rollback = false;
    bool committed = false;

    do {
        if(!storage_file_open(file, dict->user_path, FSAM_READ_WRITE, FSOM_OPEN_ALWAYS)) break;
        file_open = true;
        needs_rollback = !user_existed;
        if(storage_file_size(file) != original_info.size) break;

        if(original_info.size > 0) {
            uint8_t last_char = 0;
            if(!storage_file_seek(file, (uint32_t)original_info.size - 1U, true) ||
               storage_file_read(file, &last_char, 1) != 1) {
                break;
            }
            if(last_char != '\n') furi_string_push_back(append_data, '\n');
        }

        for(size_t i = 0; i < key_count; i++) {
            if(known[i]) continue;
            for(size_t byte = 0; byte < dict->key_size; byte++) {
                furi_string_cat_printf(append_data, "%02X", keys[i * dict->key_size + byte]);
            }
            furi_string_push_back(append_data, '\n');
        }

        if(!storage_file_seek(file, (uint32_t)original_info.size, true)) break;
        const size_t append_size = furi_string_size(append_data);
        needs_rollback = true;
        if(storage_file_write(file, furi_string_get_cstr(append_data), append_size) !=
           append_size) {
            break;
        }
        if(!storage_file_sync(file)) break;
        const bool closed = storage_file_close(file);
        file_open = false;
        if(!closed) break;

        stats->added = keys_to_add;
        committed = true;
    } while(false);

    if(file_open) storage_file_close(file);
    storage_file_free(file);
    furi_string_free(append_data);

    NfcKeyDictImportStatus status = NfcKeyDictImportStatusSuccess;
    if(!committed) {
        stats->added = 0;
        if(!needs_rollback ||
           nfc_key_dict_restore_original(
               storage, dict->user_path, user_existed, original_info.size, backup_path)) {
            status = NfcKeyDictImportStatusWriteFailed;
        } else {
            status = NfcKeyDictImportStatusRollbackFailed;
            stats->backup_preserved =
                !furi_string_empty(backup_path) &&
                storage_file_exists(storage, furi_string_get_cstr(backup_path));
            FURI_LOG_E(
                TAG,
                "Import rollback failed; backup %s",
                stats->backup_preserved ? furi_string_get_cstr(backup_path) : "unavailable");
        }
    }

    if(status != NfcKeyDictImportStatusRollbackFailed && !furi_string_empty(backup_path) &&
       storage_file_exists(storage, furi_string_get_cstr(backup_path)) &&
       storage_common_remove(storage, furi_string_get_cstr(backup_path)) != FSE_OK) {
        stats->backup_preserved = true;
        FURI_LOG_W(TAG, "Import backup retained at %s", furi_string_get_cstr(backup_path));
    }

    furi_string_free(backup_path);
    return status;
}

void nfc_key_dict_import(
    NfcKeyDictType type,
    const uint8_t* keys,
    size_t key_count,
    NfcKeyDictImportStats* stats) {
    furi_check(type == NfcKeyDictTypeMfClassic);
    furi_check(keys);
    furi_check(key_count <= NFC_KEY_DICT_DEVICE_KEYS_MAX);
    furi_check(stats);

    memset(stats, 0, sizeof(NfcKeyDictImportStats));
    stats->candidates = key_count;
    if(key_count == 0) return;

    const NfcKeyDict* dict = nfc_key_dict(type);
    Storage* storage = furi_record_open(RECORD_STORAGE);
    bool known[NFC_KEY_DICT_DEVICE_KEYS_MAX] = {};
    size_t scanned_count = 0;

    if(!storage_file_exists(storage, dict->system_path)) {
        stats->status = NfcKeyDictImportStatusSystemDictionaryMissing;
        furi_record_close(RECORD_STORAGE);
        return;
    }
    if(!nfc_key_dict_mark_path_present(
           storage,
           dict->system_path,
           keys,
           key_count,
           dict->key_size,
           known,
           &stats->known,
           &scanned_count) ||
       scanned_count == 0) {
        stats->status = NfcKeyDictImportStatusDictionaryReadFailed;
        furi_record_close(RECORD_STORAGE);
        return;
    }

    const FS_Error user_stat = storage_common_stat(storage, dict->user_path, NULL);
    if(user_stat == FSE_OK) {
        if(!nfc_key_dict_mark_path_present(
               storage,
               dict->user_path,
               keys,
               key_count,
               dict->key_size,
               known,
               &stats->known,
               &scanned_count)) {
            stats->status = NfcKeyDictImportStatusDictionaryReadFailed;
            furi_record_close(RECORD_STORAGE);
            return;
        }
    } else if(user_stat != FSE_NOT_EXIST) {
        stats->status = NfcKeyDictImportStatusDictionaryReadFailed;
        furi_record_close(RECORD_STORAGE);
        return;
    }

    stats->status = nfc_key_dict_append_transaction(storage, dict, keys, key_count, known, stats);
    furi_record_close(RECORD_STORAGE);
}
