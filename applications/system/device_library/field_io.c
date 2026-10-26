#include "field_io.h"
#include <string.h>

typedef struct {
    uint32_t magic, generation, size;
    uint8_t hash[32];
} FieldRecord;

bool field_text_valid(const char* text, size_t capacity) {
    if(!text) return false;
    for(size_t i = 0; i < capacity; i++) {
        if(!text[i]) return true;
        if((uint8_t)text[i] < 32 || (uint8_t)text[i] == 127) return false;
    }
    return false;
}

bool field_file_hash(Storage* storage, const char* path, uint32_t* size, uint8_t hash[32],
                     FieldProgress progress, void* context) {
    if(!storage || !library_path_valid(path) || !size || !hash) return false;
    File* file = storage_file_alloc(storage);
    bool opened = storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING);
    bool ok = opened && storage_file_size(file) <= FIELD_FILE_MAX;
    uint32_t total = ok ? (uint32_t)storage_file_size(file) : 0;
    uint32_t offset = 0;
    uint8_t buffer[512];
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    if(mbedtls_sha256_starts(&sha, 0)) ok = false;
    while(ok && offset < total) {
        size_t count = total - offset < sizeof(buffer) ? total - offset : sizeof(buffer);
        if((progress && !progress(context, path, offset)) ||
           storage_file_read(file, buffer, count) != count ||
           mbedtls_sha256_update(&sha, buffer, count)) {
            ok = false;
            break;
        }
        offset += count;
    }
    if(ok) ok = storage_file_size(file) == total && storage_file_get_error(file) == FSE_OK &&
                !mbedtls_sha256_finish(&sha, hash);
    if(opened) ok = storage_file_close(file) && ok;
    storage_file_free(file);
    mbedtls_sha256_free(&sha);
    if(ok) *size = total;
    return ok;
}

bool field_copy_checked(Storage* storage, File* source, File* destination, uint32_t size,
                        const uint8_t expected[32], FieldProgress progress, void* context) {
    (void)storage;
    if(!source || !destination || !expected || size > FIELD_FILE_MAX) return false;
    uint8_t buffer[512], hash[32];
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    bool ok = !mbedtls_sha256_starts(&sha, 0);
    for(uint32_t offset = 0; ok && offset < size;) {
        size_t count = size - offset < sizeof(buffer) ? size - offset : sizeof(buffer);
        ok = (!progress || progress(context, NULL, offset)) &&
             storage_file_read(source, buffer, count) == count &&
             storage_file_write(destination, buffer, count) == count &&
             !mbedtls_sha256_update(&sha, buffer, count);
        offset += count;
    }
    if(ok) ok = !mbedtls_sha256_finish(&sha, hash) && !memcmp(hash, expected, 32);
    mbedtls_sha256_free(&sha);
    return ok;
}

// Four fixed slots retain the latest valid generation. Failures may damage only
// an older staging slot; neither the last valid slot nor user captures are removed.
static int field_record_read(Storage* storage, const char* root, unsigned slot,
                             uint32_t magic, void* data, size_t size, uint32_t* generation) {
    FuriString* path = furi_string_alloc_printf("%s/%u.record", root, slot);
    FileInfo info;
    FS_Error status = storage_common_stat(storage, furi_string_get_cstr(path), &info);
    int result = status == FSE_NOT_EXIST ? 0 : -1;
    if(status == FSE_OK) {
        result = 0;
        if(info.size == sizeof(FieldRecord) + size) {
            File* file = storage_file_alloc(storage);
            bool opened = storage_file_open(file, furi_string_get_cstr(path), FSAM_READ, FSOM_OPEN_EXISTING);
            FieldRecord record;
            uint8_t digest[32];
            bool ok = opened && storage_file_read(file, &record, sizeof(record)) == sizeof(record) &&
                      storage_file_read(file, data, size) == size;
            if(opened) ok = storage_file_close(file) && ok;
            storage_file_free(file);
            if(!ok) result = -1;
            else if(record.magic == magic && record.size == size && record.generation &&
                    !mbedtls_sha256(data, size, digest, 0) && !memcmp(digest, record.hash, 32)) {
                *generation = record.generation;
                result = 1;
            }
        }
    }
    furi_string_free(path);
    return result;
}

static bool field_record_generations(Storage* storage, const char* root, uint32_t magic,
                                      void* scratch, size_t size, uint32_t generations[4]) {
    memset(generations, 0, 4 * sizeof(uint32_t));
    for(unsigned i = 0; i < 4; i++)
        if(field_record_read(storage, root, i, magic, scratch, size, &generations[i]) < 0)
            return false;
    return storage_sd_status(storage) == FSE_OK;
}

bool field_record_load(Storage* storage, const char* root, uint32_t magic, void* data, size_t size) {
    if(!storage || !library_path_valid(root) || !data || !size || size > 8192) return false;
    uint32_t generations[4];
    bool ok = field_record_generations(storage, root, magic, data, size, generations);
    int newest = library_newest_slot(generations);
    return ok && newest >= 0 &&
           field_record_read(storage, root, newest, magic, data, size, &generations[newest]) == 1;
}

bool field_record_save(Storage* storage, const char* root, uint32_t magic, const void* data, size_t size) {
    if(!storage || !library_path_valid(root) || !data || !size || size > 8192 ||
       storage_sd_status(storage) != FSE_OK || !storage_simply_mkdir(storage, LIBRARY_ROOT) ||
       !storage_simply_mkdir(storage, root)) return false;
    void* scratch = malloc(size);
    uint32_t generations[4];
    bool ok = field_record_generations(storage, root, magic, scratch, size, generations);
    int newest = library_newest_slot(generations);
    if(newest >= 0 && generations[newest] == UINT32_MAX) ok = false;
    if(ok) {
        unsigned slot = library_write_slot(generations);
        FieldRecord record = {.magic = magic, .generation = newest < 0 ? 1 : generations[newest] + 1, .size = size};
        ok = !mbedtls_sha256(data, size, record.hash, 0);
        FuriString* path = furi_string_alloc_printf("%s/%u.record", root, slot);
        FS_Error removed = ok ? storage_common_remove(storage, furi_string_get_cstr(path)) : FSE_INTERNAL;
        File* file = storage_file_alloc(storage);
        bool created = (removed == FSE_OK || removed == FSE_NOT_EXIST) &&
                       storage_file_open(file, furi_string_get_cstr(path), FSAM_WRITE, FSOM_CREATE_NEW);
        ok = created && storage_file_write(file, &record, sizeof(record)) == sizeof(record) &&
             storage_file_write(file, data, size) == size && storage_file_sync(file);
        if(created) ok = storage_file_close(file) && ok;
        storage_file_free(file);
        uint32_t read_generation = 0;
        if(ok) ok = field_record_read(storage, root, slot, magic, scratch, size, &read_generation) == 1 &&
                    read_generation == record.generation && !memcmp(scratch, data, size);
        if(!ok && created) storage_common_remove(storage, furi_string_get_cstr(path));
        furi_string_free(path);
    }
    free(scratch);
    return ok;
}
