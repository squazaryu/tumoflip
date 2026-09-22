#include "library_model.h"
#include <toolbox/file_history.h>
#include <mbedtls/sha256.h>
#include <furi_hal_rtc.h>
#include <stdio.h>

typedef struct {
    uint32_t magic;
    FileHistoryRecord record;
    uint8_t checksum[32];
} HistoryEnvelope;
#define HISTORY_MAGIC 0x31484654U

static bool history_dir(Storage* storage, const char* source, FuriString* output, bool create) {
    if(!library_path_valid(source) || !file_history_supported(source)) return false;
    uint8_t hash[32];
    if(mbedtls_sha256((const uint8_t*)source, strlen(source), hash, 0)) return false;
    if(create && (!storage_simply_mkdir(storage, EXT_PATH("apps_data/device_library")) ||
                  !storage_simply_mkdir(storage, FILE_HISTORY_ROOT)))
        return false;
    furi_string_set(output, FILE_HISTORY_ROOT "/");
    for(unsigned i = 0; i < 32; i++)
        furi_string_cat_printf(output, "%02x", hash[i]);
    return !create || storage_simply_mkdir(storage, furi_string_get_cstr(output));
}

static bool
    history_read_record(Storage* storage, const char* dir, unsigned slot, HistoryEnvelope* e) {
    FuriString* path = furi_string_alloc_printf("%s/%u.meta", dir, slot);
    File* file = storage_file_alloc(storage);
    bool ok = storage_file_open(file, furi_string_get_cstr(path), FSAM_READ, FSOM_OPEN_EXISTING) &&
              storage_file_size(file) == sizeof(*e) &&
              storage_file_read(file, e, sizeof(*e)) == sizeof(*e) && e->magic == HISTORY_MAGIC &&
              e->record.generation && e->record.size <= FILE_HISTORY_MAX_BYTES &&
              memchr(e->record.source, 0, sizeof(e->record.source));
    uint8_t hash[32];
    ok = ok && !mbedtls_sha256((const uint8_t*)&e->record, sizeof(e->record), hash, 0) &&
         !memcmp(hash, e->checksum, sizeof(hash));
    storage_file_free(file);
    furi_string_free(path);
    return ok;
}

static bool history_list(Storage* storage, const char* source, FileHistoryRecord records[4]) {
    memset(records, 0, sizeof(FileHistoryRecord) * 4);
    FuriString* dir = furi_string_alloc();
    bool ok = history_dir(storage, source, dir, false);
    if(ok)
        for(unsigned i = 0; i < 4; i++) {
            HistoryEnvelope e = {0};
            if(history_read_record(storage, furi_string_get_cstr(dir), i, &e)) {
                if(strcmp(e.record.source, source)) {
                    ok = false;
                    break;
                }
                records[i] = e.record;
            }
        }
    furi_string_free(dir);
    return ok && storage_sd_status(storage) == FSE_OK;
}

static bool
    history_hash_file(Storage* storage, const char* path, uint8_t digest[32], uint32_t* size) {
    File* file = storage_file_alloc(storage);
    mbedtls_sha256_context hash;
    mbedtls_sha256_init(&hash);
    bool ok = storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING) &&
              storage_file_size(file) <= FILE_HISTORY_MAX_BYTES &&
              !mbedtls_sha256_starts(&hash, 0);
    uint32_t total = 0;
    if(ok) {
        uint64_t expected = storage_file_size(file);
        uint8_t buffer[512];
        while(ok) {
            size_t n = storage_file_read(file, buffer, sizeof(buffer));
            if(storage_file_get_error(file) != FSE_OK) {
                ok = false;
                break;
            }
            if(!n) break;
            total += n;
            ok = total <= FILE_HISTORY_MAX_BYTES && !mbedtls_sha256_update(&hash, buffer, n);
        }
        ok = ok && total == expected && !mbedtls_sha256_finish(&hash, digest);
    }
    *size = total;
    mbedtls_sha256_free(&hash);
    storage_file_free(file);
    return ok;
}

static bool history_copy_new(Storage* storage, const char* from, const char* to) {
    File* input = storage_file_alloc(storage);
    File* output = storage_file_alloc(storage);
    bool created = false;
    bool ok = storage_file_open(input, from, FSAM_READ, FSOM_OPEN_EXISTING);
    if(ok) created = storage_file_open(output, to, FSAM_WRITE, FSOM_CREATE_NEW);
    ok = ok && created && storage_file_size(input) <= FILE_HISTORY_MAX_BYTES;
    uint64_t expected = ok ? storage_file_size(input) : 0;
    uint32_t copied = 0;
    uint8_t buffer[512];
    while(ok) {
        size_t n = storage_file_read(input, buffer, sizeof(buffer));
        if(storage_file_get_error(input) != FSE_OK) {
            ok = false;
            break;
        }
        if(!n) break;
        copied += n;
        ok = copied <= FILE_HISTORY_MAX_BYTES && storage_file_write(output, buffer, n) == n;
    }
    ok = ok && copied == expected && storage_file_sync(output);
    if(created) ok = storage_file_close(output) && ok;
    storage_file_free(output);
    storage_file_free(input);
    if(created && !ok) storage_common_remove(storage, to);
    return ok;
}

static bool history_remove_slot(Storage* storage, const char* dir, unsigned slot) {
    FuriString* path = furi_string_alloc();
    bool ok = true;
    const char* suffixes[] = {"meta", "bin"};
    for(unsigned i = 0; i < 2; i++) {
        furi_string_printf(path, "%s/%u.%s", dir, slot, suffixes[i]);
        FS_Error error = storage_common_remove(storage, furi_string_get_cstr(path));
        if(error != FSE_OK && error != FSE_NOT_EXIST) ok = false;
    }
    furi_string_free(path);
    return ok;
}

static bool history_snapshot(Storage* storage, const char* source) {
    FileHistoryRecord* records = calloc(4, sizeof(*records));
    HistoryEnvelope next = {.magic = HISTORY_MAGIC};
    FuriString* dir = furi_string_alloc();
    FuriString* path = furi_string_alloc();
    bool ok = false;
    do {
        if(!history_dir(storage, source, dir, true) || !history_list(storage, source, records))
            break;
        if(!history_hash_file(storage, source, next.record.digest, &next.record.size)) break;
        uint32_t gen[4];
        for(unsigned i = 0; i < 4; i++)
            gen[i] = records[i].generation;
        int newest = library_newest_slot(gen);
        if(newest >= 0 && records[newest].size == next.record.size &&
           !memcmp(records[newest].digest, next.record.digest, 32)) {
            uint8_t hash[32];
            uint32_t size;
            furi_string_printf(path, "%s/%u.bin", furi_string_get_cstr(dir), newest);
            // Metadata alone must never permit overwriting a source whose backup is corrupt.
            ok = history_hash_file(storage, furi_string_get_cstr(path), hash, &size) &&
                 size == next.record.size && !memcmp(hash, next.record.digest, 32);
            if(ok) break;
        }
        if(newest >= 0 && gen[newest] == UINT32_MAX) break;
        unsigned slot = library_write_slot(gen);
        next.record.generation = newest < 0 ? 1 : gen[newest] + 1;
        next.record.timestamp = furi_hal_rtc_get_timestamp();
        strlcpy(next.record.source, source, sizeof(next.record.source));
        if(!history_remove_slot(storage, furi_string_get_cstr(dir), slot)) break;
        furi_string_printf(path, "%s/%u.bin", furi_string_get_cstr(dir), slot);
        if(!history_copy_new(storage, source, furi_string_get_cstr(path))) break;
        uint8_t hash[32];
        uint32_t size;
        if(!history_hash_file(storage, furi_string_get_cstr(path), hash, &size) ||
           size != next.record.size || memcmp(hash, next.record.digest, 32))
            break;
        if(mbedtls_sha256((const uint8_t*)&next.record, sizeof(next.record), next.checksum, 0))
            break;
        furi_string_printf(path, "%s/%u.meta", furi_string_get_cstr(dir), slot);
        File* file = storage_file_alloc(storage);
        bool created =
            storage_file_open(file, furi_string_get_cstr(path), FSAM_WRITE, FSOM_CREATE_NEW);
        ok = created && storage_file_write(file, &next, sizeof(next)) == sizeof(next) &&
             storage_file_sync(file);
        if(created) ok = storage_file_close(file) && ok;
        storage_file_free(file);
        if(!ok) break;
        gen[slot] = next.record.generation;
        unsigned valid = 0;
        for(unsigned i = 0; i < 4; i++)
            if(gen[i]) valid++;
        if(valid > 3) {
            // Only the oldest owned history slot is eligible for retention cleanup.
            unsigned oldest = library_write_slot(gen);
            if(!history_remove_slot(storage, furi_string_get_cstr(dir), oldest)) ok = false;
        }
    } while(false);
    furi_string_free(path);
    furi_string_free(dir);
    free(records);
    return ok;
}

static bool history_restore_copy(
    Storage* storage,
    const char* source,
    unsigned slot,
    FuriString* destination) {
    if(slot >= 4) return false;
    FuriString* dir = furi_string_alloc();
    FuriString* from = furi_string_alloc();
    HistoryEnvelope e = {0};
    bool ok = false;
    do {
        if(!history_dir(storage, source, dir, false) ||
           !history_read_record(storage, furi_string_get_cstr(dir), slot, &e) ||
           strcmp(source, e.record.source))
            break;
        furi_string_printf(from, "%s/%u.bin", furi_string_get_cstr(dir), slot);
        uint8_t hash[32];
        uint32_t size;
        if(!history_hash_file(storage, furi_string_get_cstr(from), hash, &size) ||
           size != e.record.size || memcmp(hash, e.record.digest, 32))
            break;
        const char* suffix = strrchr(source, '.');
        size_t base = suffix - source;
        for(unsigned i = 0; i < 100; i++) {
            furi_string_printf(destination, "%.*s_restored_%02u%s", (int)base, source, i, suffix);
            if(furi_string_size(destination) >= 256) break;
            FS_Error exists =
                storage_common_stat(storage, furi_string_get_cstr(destination), NULL);
            if(exists == FSE_OK) continue;
            if(exists != FSE_NOT_EXIST) break;
            ok = history_copy_new(
                storage, furi_string_get_cstr(from), furi_string_get_cstr(destination));
            if(ok) {
                ok = history_hash_file(storage, furi_string_get_cstr(destination), hash, &size) &&
                     size == e.record.size && !memcmp(hash, e.record.digest, 32);
                if(!ok) storage_common_remove(storage, furi_string_get_cstr(destination));
            }
            break;
        }
    } while(false);
    furi_string_free(from);
    furi_string_free(dir);
    return ok;
}
static bool history_sources(
    Storage* storage,
    uint32_t offset,
    char paths[32][256],
    uint32_t* count,
    bool* more) {
    *count = 0;
    *more = false;
    File* directory = storage_file_alloc(storage);
    FS_Error state = storage_common_stat(storage, FILE_HISTORY_ROOT, NULL);
    if(state == FSE_NOT_EXIST) {
        storage_file_free(directory);
        return true;
    }
    bool ok = storage_dir_open(directory, FILE_HISTORY_ROOT);
    FileInfo info;
    char name[80];
    FuriString* path = furi_string_alloc();
    FuriString* expected = furi_string_alloc();
    uint32_t seen = 0;
    while(ok && !*more && storage_dir_read(directory, &info, name, sizeof(name))) {
        if(!file_info_is_dir(&info) || strlen(name) != 64) continue;
        furi_string_printf(path, FILE_HISTORY_ROOT "/%s", name);
        for(unsigned i = 0; i < 4; i++) {
            HistoryEnvelope e = {0};
            if(history_read_record(storage, furi_string_get_cstr(path), i, &e) &&
               history_dir(storage, e.record.source, expected, false) &&
               !strcmp(furi_string_get_cstr(path), furi_string_get_cstr(expected))) {
                if(seen++ < offset) break;
                if(*count == 32) {
                    *more = true;
                    break;
                }
                strlcpy(paths[(*count)++], e.record.source, 256);
                break;
            }
        }
    }
    ok = ok && storage_sd_status(storage) == FSE_OK;
    storage_file_free(directory);
    furi_string_free(path);
    furi_string_free(expected);
    return ok;
}
static const FileHistoryApi history_api =
    {history_snapshot, history_list, history_restore_copy, history_sources};
static const FlipperAppPluginDescriptor descriptor = {
    .appid = FILE_HISTORY_APP_ID,
    .ep_api_version = FILE_HISTORY_ABI,
    .entry_point = &history_api,
};
const FlipperAppPluginDescriptor* file_history_ep(void) {
    return &descriptor;
}
