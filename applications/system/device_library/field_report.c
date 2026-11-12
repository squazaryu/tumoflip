#include "field_report.h"
#include <stdio.h>
#include <string.h>

static bool field_report_write(File* file, mbedtls_sha256_context* hash, const char* text,
                               uint32_t* size) {
    const size_t length = strlen(text);
    if(length > UINT32_MAX - *size || storage_file_write(file, text, length) != length ||
       mbedtls_sha256_update(hash, (const uint8_t*)text, length)) return false;
    *size += length;
    return true;
}

bool field_report_save(
    Storage* storage, const char* kind, uint32_t timestamp, const char* header,
    size_t rows, FieldReportRow row, void* context, char* path, size_t capacity) {
    if(!storage || !kind || !kind[0] || strlen(kind) > 16U || !header || !rows ||
       rows > 4096U || !row || !path) return false;
    for(const char* p = kind; *p; p++)
        if((*p < 'a' || *p > 'z') && *p != '_') return false;
    const char* root = LIBRARY_ROOT "/reports";
    if(!storage_simply_mkdir(storage, EXT_PATH("apps_data")) ||
       !storage_simply_mkdir(storage, LIBRARY_ROOT) || !storage_simply_mkdir(storage, root)) return false;
    char target[160];
    bool available = false;
    for(unsigned i = 0; i < 32U; i++) {
        snprintf(target, sizeof(target), "%s/%s_%lu_%u.csv", root, kind,
                 (unsigned long)timestamp, i);
        const FS_Error status = storage_common_stat(storage, target, NULL);
        if(status == FSE_NOT_EXIST) { available = true; break; }
        if(status != FSE_OK) return false;
    }
    if(!available || strlen(target) >= capacity) return false;
    File* file = storage_file_alloc(storage);
    const bool created = storage_file_open(file, target, FSAM_WRITE, FSOM_CREATE_NEW);
    mbedtls_sha256_context hash;
    mbedtls_sha256_init(&hash);
    uint32_t size = 0;
    bool ok = created && !mbedtls_sha256_starts(&hash, 0) &&
              field_report_write(file, &hash, header, &size);
    for(size_t i = 0; ok && i < rows; i++) {
        char line[256] = {0};
        ok = row(context, i, line, sizeof(line)) && memchr(line, 0, sizeof(line)) &&
             field_report_write(file, &hash, line, &size);
    }
    uint8_t expected[32], actual[32];
    uint32_t actual_size = 0;
    if(ok) ok = !mbedtls_sha256_finish(&hash, expected) && storage_file_sync(file);
    mbedtls_sha256_free(&hash);
    if(created) ok = storage_file_close(file) && ok;
    storage_file_free(file);
    if(ok) ok = field_file_hash(storage, target, &actual_size, actual, NULL, NULL) &&
                actual_size == size && !memcmp(actual, expected, sizeof(expected));
    if(!ok) {
        if(created) storage_common_remove(storage, target); // only our newly created export
        return false;
    }
    memcpy(path, target, strlen(target) + 1U);
    return true;
}
