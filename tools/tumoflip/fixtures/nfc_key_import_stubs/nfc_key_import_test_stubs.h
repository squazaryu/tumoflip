#pragma once

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct NfcDevice NfcDevice;
typedef struct FuriString FuriString;
typedef struct Storage Storage;
typedef struct File File;
typedef struct File Stream;

typedef enum {
    FSAM_READ = 1,
    FSAM_WRITE = 2,
    FSAM_READ_WRITE = 3
} FS_AccessMode;
typedef enum {
    FSOM_OPEN_EXISTING = 1,
    FSOM_OPEN_ALWAYS = 2,
    FSOM_CREATE_NEW = 8
} FS_OpenMode;
typedef enum {
    FSE_OK,
    FSE_NOT_READY,
    FSE_EXIST,
    FSE_NOT_EXIST,
    FSE_INTERNAL
} FS_Error;
typedef struct {
    uint8_t flags;
    uint64_t size;
} FileInfo;

#define RECORD_STORAGE        "storage"
#define furi_check(condition) assert(condition)
#define FURI_LOG_W(...)       ((void)0)
#define FURI_LOG_E(...)       ((void)0)

void* furi_record_open(const char* name);
void furi_record_close(const char* name);
FuriString* furi_string_alloc(void);
void furi_string_free(FuriString* string);
void furi_string_reset(FuriString* string);
bool furi_string_empty(const FuriString* string);
size_t furi_string_size(const FuriString* string);
char furi_string_get_char(const FuriString* string, size_t index);
const char* furi_string_get_cstr(const FuriString* string);
void furi_string_push_back(FuriString* string, char value);
void furi_string_printf(FuriString* string, const char* format, ...);
void furi_string_cat_printf(FuriString* string, const char* format, ...);

File* storage_file_alloc(Storage* storage);
void storage_file_free(File* file);
bool storage_file_open(File* file, const char* path, FS_AccessMode access, FS_OpenMode mode);
bool storage_file_close(File* file);
bool storage_file_exists(Storage* storage, const char* path);
uint64_t storage_file_size(File* file);
bool storage_file_seek(File* file, uint32_t offset, bool from_start);
size_t storage_file_read(File* file, void* data, size_t size);
size_t storage_file_write(File* file, const void* data, size_t size);
bool storage_file_sync(File* file);
bool storage_file_truncate(File* file);
FS_Error storage_common_stat(Storage* storage, const char* path, FileInfo* info);
FS_Error storage_common_copy(Storage* storage, const char* source, const char* destination);
FS_Error storage_common_remove(Storage* storage, const char* path);
void storage_get_next_filename(
    Storage* storage,
    const char* directory,
    const char* name,
    const char* extension,
    FuriString* next,
    uint8_t max_length);
void path_extract_dirname(const char* path, FuriString* directory);
bool args_char_to_hex(char high, char low, uint8_t* value);

Stream* file_stream_alloc(Storage* storage);
bool file_stream_open(Stream* stream, const char* path, FS_AccessMode access, FS_OpenMode mode);
bool file_stream_close(Stream* stream);
FS_Error file_stream_get_error(Stream* stream);
bool stream_read_line(Stream* stream, FuriString* line);
void stream_free(Stream* stream);
