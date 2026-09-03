#define _POSIX_C_SOURCE 200809L
#include "nfc_key_dict.h"
#include "nfc_key_import_test_stubs.h"

#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

struct FuriString {
    char* data;
    size_t size;
    size_t capacity;
};
struct Storage {
    int unused;
};
struct File {
    FILE* handle;
    char path[1024];
    FS_Error error;
    FS_AccessMode access;
};

static Storage storage;
static char root[1024], system_path[1024], user_path[1024];
static NfcKeyDict dictionary;
static int partial_write = -1;
static int sync_failures;
static bool truncate_failure, backup_failure, restore_failure;
static const char* read_failure_path;
static const char* open_failure_path;
static bool user_write_open_failure, user_write_close_failure, backup_sync_failure;
static bool user_remove_failure, backup_remove_failure;

static void reserve(FuriString* string, size_t length) {
    if(length + 1 > string->capacity) {
        string->capacity = length + 64;
        string->data = realloc(string->data, string->capacity);
        assert(string->data);
    }
}

FuriString* furi_string_alloc(void) {
    FuriString* string = calloc(1, sizeof(*string));
    reserve(string, 0);
    string->data[0] = 0;
    return string;
}
void furi_string_free(FuriString* string) {
    free(string->data);
    free(string);
}
void furi_string_reset(FuriString* string) {
    string->size = 0;
    string->data[0] = 0;
}
bool furi_string_empty(const FuriString* string) {
    return string->size == 0;
}
size_t furi_string_size(const FuriString* string) {
    return string->size;
}
const char* furi_string_get_cstr(const FuriString* string) {
    return string->data;
}
char furi_string_get_char(const FuriString* string, size_t index) {
    assert(index < string->size);
    return string->data[index];
}
void furi_string_push_back(FuriString* string, char value) {
    reserve(string, string->size + 1);
    string->data[string->size++] = value;
    string->data[string->size] = 0;
}
static void format_append(FuriString* string, const char* format, va_list args) {
    va_list copy;
    va_copy(copy, args);
    int length = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    assert(length >= 0);
    reserve(string, string->size + (size_t)length);
    vsnprintf(string->data + string->size, (size_t)length + 1, format, args);
    string->size += (size_t)length;
}
void furi_string_printf(FuriString* string, const char* format, ...) {
    furi_string_reset(string);
    va_list args;
    va_start(args, format);
    format_append(string, format, args);
    va_end(args);
}
void furi_string_cat_printf(FuriString* string, const char* format, ...) {
    va_list args;
    va_start(args, format);
    format_append(string, format, args);
    va_end(args);
}

void* furi_record_open(const char* name) {
    (void)name;
    return &storage;
}
void furi_record_close(const char* name) {
    (void)name;
}
const NfcKeyDict* nfc_key_dict(NfcKeyDictType type) {
    assert(type == NfcKeyDictTypeMfClassic);
    return &dictionary;
}

File* storage_file_alloc(Storage* instance) {
    (void)instance;
    return calloc(1, sizeof(File));
}
void storage_file_free(File* file) {
    if(file->handle) fclose(file->handle);
    free(file);
}
bool storage_file_exists(Storage* instance, const char* path) {
    (void)instance;
    struct stat info;
    return stat(path, &info) == 0 && S_ISREG(info.st_mode);
}
bool storage_file_open(File* file, const char* path, FS_AccessMode access, FS_OpenMode mode) {
    assert(!file->handle);
    if((open_failure_path && strcmp(path, open_failure_path) == 0) ||
       (user_write_open_failure && access == FSAM_READ_WRITE && strcmp(path, user_path) == 0)) {
        file->error = FSE_INTERNAL;
        return false;
    }
    const bool exists = storage_file_exists(&storage, path);
    if((mode == FSOM_OPEN_EXISTING && !exists) || (mode == FSOM_CREATE_NEW && exists)) {
        file->error = FSE_NOT_EXIST;
        return false;
    }
    const char* flags = access == FSAM_READ ? "rb" : (exists ? "r+b" : "w+b");
    file->handle = fopen(path, flags);
    snprintf(file->path, sizeof(file->path), "%s", path);
    file->error = file->handle ? FSE_OK : FSE_INTERNAL;
    file->access = access;
    return file->handle != NULL;
}
bool storage_file_close(File* file) {
    if(!file->handle) return false;
    const bool success = fclose(file->handle) == 0;
    file->handle = NULL;
    if(user_write_close_failure && file->access == FSAM_READ_WRITE &&
       strcmp(file->path, user_path) == 0) {
        user_write_close_failure = false;
        return false;
    }
    return success;
}
uint64_t storage_file_size(File* file) {
    struct stat info;
    assert(fstat(fileno(file->handle), &info) == 0);
    return (uint64_t)info.st_size;
}
bool storage_file_seek(File* file, uint32_t offset, bool from_start) {
    assert(from_start);
    if(offset > storage_file_size(file)) return false;
    return fseek(file->handle, (long)offset, SEEK_SET) == 0;
}
size_t storage_file_read(File* file, void* data, size_t size) {
    return fread(data, 1, size, file->handle);
}
size_t storage_file_write(File* file, const void* data, size_t size) {
    if(strcmp(file->path, user_path) == 0 && partial_write >= 0) {
        const size_t limited = (size_t)partial_write < size ? (size_t)partial_write : size;
        partial_write = -1;
        return fwrite(data, 1, limited, file->handle);
    }
    return fwrite(data, 1, size, file->handle);
}
bool storage_file_sync(File* file) {
    const bool success = fflush(file->handle) == 0;
    if(backup_sync_failure && strstr(file->path, ".bak")) return false;
    if(strcmp(file->path, user_path) == 0 && sync_failures > 0) {
        sync_failures--;
        return false;
    }
    return success;
}
bool storage_file_truncate(File* file) {
    if(truncate_failure) return false;
    return fflush(file->handle) == 0 && ftruncate(fileno(file->handle), ftell(file->handle)) == 0;
}
FS_Error storage_common_stat(Storage* instance, const char* path, FileInfo* output) {
    (void)instance;
    struct stat info;
    if(stat(path, &info) != 0) return FSE_NOT_EXIST;
    if(output) {
        output->flags = 0;
        output->size = (uint64_t)info.st_size;
    }
    return FSE_OK;
}
FS_Error storage_common_remove(Storage* instance, const char* path) {
    (void)instance;
    if((user_remove_failure && strcmp(path, user_path) == 0) ||
       (backup_remove_failure && strstr(path, ".bak")))
        return FSE_INTERNAL;
    return unlink(path) == 0 ? FSE_OK : FSE_INTERNAL;
}
FS_Error storage_common_copy(Storage* instance, const char* source, const char* destination) {
    (void)instance;
    if(storage_file_exists(&storage, destination)) return FSE_EXIST;
    FILE* input = fopen(source, "rb");
    FILE* output = fopen(destination, "wb");
    assert(input && output);
    const bool fail = (backup_failure && strstr(destination, ".bak")) ||
                      (restore_failure && strcmp(destination, user_path) == 0);
    char data[256];
    size_t count;
    while((count = fread(data, 1, sizeof(data), input)) > 0) {
        if(fail) {
            fwrite(data, 1, count > 2 ? 2 : count, output);
            break;
        }
        assert(fwrite(data, 1, count, output) == count);
    }
    fclose(input);
    fclose(output);
    return fail ? FSE_INTERNAL : FSE_OK;
}
void storage_get_next_filename(
    Storage* instance,
    const char* directory,
    const char* name,
    const char* extension,
    FuriString* next,
    uint8_t max_length) {
    (void)instance;
    (void)max_length;
    char path[2048];
    for(unsigned suffix = 0;; suffix++) {
        if(suffix)
            furi_string_printf(next, "%s%u", name, suffix);
        else
            furi_string_printf(next, "%s", name);
        snprintf(path, sizeof(path), "%s/%s%s", directory, furi_string_get_cstr(next), extension);
        if(access(path, F_OK) != 0) return;
    }
}
void path_extract_dirname(const char* path, FuriString* directory) {
    const char* slash = strrchr(path, '/');
    assert(slash);
    furi_string_printf(directory, "%.*s", (int)(slash - path), path);
}
static int hex_value(char value) {
    if(value >= '0' && value <= '9') return value - '0';
    if(value >= 'a' && value <= 'f') return value - 'a' + 10;
    if(value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}
bool args_char_to_hex(char high, char low, uint8_t* value) {
    const int h = hex_value(high), l = hex_value(low);
    if(h < 0 || l < 0) return false;
    *value = (uint8_t)((h << 4) | l);
    return true;
}
Stream* file_stream_alloc(Storage* instance) {
    return storage_file_alloc(instance);
}
bool file_stream_open(Stream* stream, const char* path, FS_AccessMode access, FS_OpenMode mode) {
    return storage_file_open(stream, path, access, mode);
}
bool file_stream_close(Stream* stream) {
    return storage_file_close(stream);
}
FS_Error file_stream_get_error(Stream* stream) {
    return stream->error;
}
void stream_free(Stream* stream) {
    storage_file_free(stream);
}
bool stream_read_line(Stream* stream, FuriString* line) {
    furi_string_reset(line);
    if(read_failure_path && strcmp(stream->path, read_failure_path) == 0) {
        stream->error = FSE_INTERNAL;
        return false;
    }
    int value;
    while((value = fgetc(stream->handle)) != EOF) {
        furi_string_push_back(line, (char)value);
        if(value == '\n') break;
    }
    if(ferror(stream->handle)) stream->error = FSE_INTERNAL;
    return !furi_string_empty(line);
}

static void put_file(const char* path, const char* data) {
    FILE* file = fopen(path, "wb");
    assert(file);
    assert(fwrite(data, 1, strlen(data), file) == strlen(data));
    assert(fclose(file) == 0);
}
static void expect_file(const char* path, const char* expected) {
    FILE* file = fopen(path, "rb");
    assert(file);
    char data[4096] = {};
    const size_t size = fread(data, 1, sizeof(data), file);
    fclose(file);
    assert(size == strlen(expected));
    assert(memcmp(data, expected, size) == 0);
}
static unsigned backup_count(bool verify_original) {
    DIR* directory = opendir(root);
    assert(directory);
    unsigned count = 0;
    struct dirent* entry;
    while((entry = readdir(directory))) {
        if(strstr(entry->d_name, ".bak")) {
            count++;
            if(verify_original) {
                char path[2048];
                snprintf(path, sizeof(path), "%s/%s", root, entry->d_name);
                expect_file(path, "a0a1a2a3a4a5");
            }
        }
    }
    closedir(directory);
    return count;
}
static void reset_case(bool with_user) {
    DIR* directory = opendir(root);
    assert(directory);
    struct dirent* entry;
    while((entry = readdir(directory))) {
        if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        char path[2048];
        snprintf(path, sizeof(path), "%s/%s", root, entry->d_name);
        assert(unlink(path) == 0);
    }
    closedir(directory);
    partial_write = -1;
    sync_failures = 0;
    truncate_failure = backup_failure = restore_failure = false;
    read_failure_path = NULL;
    open_failure_path = NULL;
    user_write_open_failure = user_write_close_failure = backup_sync_failure = false;
    user_remove_failure = backup_remove_failure = false;
    put_file(system_path, "# system\n\nffffffffffff\ninvalid-data\n");
    if(with_user) put_file(user_path, "a0a1a2a3a4a5");
}
static NfcKeyDictImportStats run_import(void) {
    const uint8_t keys[][6] = {
        {0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
        {0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5},
        {1, 2, 3, 4, 5, 6},
    };
    NfcKeyDictImportStats stats;
    nfc_key_dict_import(NfcKeyDictTypeMfClassic, &keys[0][0], 3, &stats);
    return stats;
}

int main(int argc, char** argv) {
    assert(argc == 2);
    snprintf(root, sizeof(root), "%s", argv[1]);
    snprintf(system_path, sizeof(system_path), "%s/system.nfc", root);
    snprintf(user_path, sizeof(user_path), "%s/user.nfc", root);
    dictionary = (NfcKeyDict){"Classic", system_path, user_path, 6};
    NfcKeyDictImportStats result;

    reset_case(true);
    result = run_import();
    assert(
        result.status == NfcKeyDictImportStatusSuccess && result.added == 1 && result.known == 2);
    expect_file(user_path, "a0a1a2a3a4a5\n010203040506\n");
    assert(backup_count(false) == 0);

    reset_case(true);
    assert(unlink(system_path) == 0);
    result = run_import();
    assert(result.status == NfcKeyDictImportStatusSystemDictionaryMissing && result.added == 0);
    expect_file(user_path, "a0a1a2a3a4a5");
    assert(backup_count(false) == 0);

    reset_case(true);
    partial_write = 5;
    result = run_import();
    assert(result.status == NfcKeyDictImportStatusWriteFailed && result.added == 0);
    expect_file(user_path, "a0a1a2a3a4a5");
    assert(backup_count(false) == 0);

    reset_case(true);
    sync_failures = 1;
    result = run_import();
    assert(result.status == NfcKeyDictImportStatusWriteFailed && result.added == 0);
    expect_file(user_path, "a0a1a2a3a4a5");

    reset_case(true);
    partial_write = 5;
    truncate_failure = true;
    result = run_import();
    assert(result.status == NfcKeyDictImportStatusWriteFailed && result.added == 0);
    expect_file(user_path, "a0a1a2a3a4a5");
    assert(backup_count(false) == 0);

    reset_case(true);
    partial_write = 5;
    truncate_failure = true;
    restore_failure = true;
    result = run_import();
    assert(result.status == NfcKeyDictImportStatusRollbackFailed && result.backup_preserved);
    assert(result.added == 0 && backup_count(true) == 1);

    reset_case(true);
    backup_failure = true;
    result = run_import();
    assert(result.status == NfcKeyDictImportStatusBackupFailed && result.added == 0);
    expect_file(user_path, "a0a1a2a3a4a5");
    assert(backup_count(false) == 0);

    reset_case(false);
    partial_write = 5;
    result = run_import();
    assert(result.status == NfcKeyDictImportStatusWriteFailed && result.added == 0);
    assert(!storage_file_exists(&storage, user_path));

    reset_case(true);
    read_failure_path = system_path;
    result = run_import();
    assert(result.status == NfcKeyDictImportStatusDictionaryReadFailed);
    expect_file(user_path, "a0a1a2a3a4a5");

    reset_case(true);
    read_failure_path = user_path;
    result = run_import();
    assert(result.status == NfcKeyDictImportStatusDictionaryReadFailed);
    expect_file(user_path, "a0a1a2a3a4a5");

    reset_case(true);
    put_file(system_path, "# empty\n");
    result = run_import();
    assert(result.status == NfcKeyDictImportStatusDictionaryReadFailed);
    expect_file(user_path, "a0a1a2a3a4a5");

    reset_case(true);
    const uint8_t known_key[6] = {0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5};
    nfc_key_dict_import(NfcKeyDictTypeMfClassic, known_key, 1, &result);
    assert(
        result.status == NfcKeyDictImportStatusSuccess && result.added == 0 && result.known == 1);
    expect_file(user_path, "a0a1a2a3a4a5");
    assert(backup_count(false) == 0);

    reset_case(false);
    uint8_t keys[80][6] = {};
    for(size_t i = 0; i < 80; i++)
        keys[i][0] = (uint8_t)i;
    nfc_key_dict_import(NfcKeyDictTypeMfClassic, &keys[0][0], 80, &result);
    assert(
        result.status == NfcKeyDictImportStatusSuccess && result.added == 80 && result.known == 0);
    FileInfo info;
    assert(storage_common_stat(&storage, user_path, &info) == FSE_OK && info.size == 1040);

    reset_case(true);
    user_write_open_failure = true;
    result = run_import();
    assert(result.status == NfcKeyDictImportStatusWriteFailed && result.added == 0);
    expect_file(user_path, "a0a1a2a3a4a5");
    assert(backup_count(false) == 0);

    reset_case(true);
    user_write_close_failure = true;
    result = run_import();
    assert(result.status == NfcKeyDictImportStatusWriteFailed && result.added == 0);
    expect_file(user_path, "a0a1a2a3a4a5");

    reset_case(true);
    backup_sync_failure = true;
    result = run_import();
    assert(result.status == NfcKeyDictImportStatusBackupFailed && result.added == 0);
    expect_file(user_path, "a0a1a2a3a4a5");

    reset_case(true);
    backup_remove_failure = true;
    result = run_import();
    assert(result.status == NfcKeyDictImportStatusSuccess && result.backup_preserved);
    expect_file(user_path, "a0a1a2a3a4a5\n010203040506\n");
    assert(backup_count(true) == 1);

    reset_case(false);
    partial_write = 5;
    user_remove_failure = true;
    result = run_import();
    assert(result.status == NfcKeyDictImportStatusRollbackFailed && !result.backup_preserved);
    assert(result.added == 0);

    reset_case(true);
    open_failure_path = system_path;
    result = run_import();
    assert(result.status == NfcKeyDictImportStatusDictionaryReadFailed);
    expect_file(user_path, "a0a1a2a3a4a5");

    reset_case(true);
    put_file(user_path, "a0a1a2a3a4a5\n");
    result = run_import();
    assert(result.status == NfcKeyDictImportStatusSuccess && result.added == 1);
    expect_file(user_path, "a0a1a2a3a4a5\n010203040506\n");

    reset_case(true);
    nfc_key_dict_import(NfcKeyDictTypeMfClassic, known_key, 0, &result);
    assert(result.status == NfcKeyDictImportStatusSuccess && result.candidates == 0);
    expect_file(user_path, "a0a1a2a3a4a5");

    reset_case(false);
    assert(unlink(system_path) == 0);
    puts("21 key-import behavior cases passed");
    return 0;
}
