#pragma once

#include <furi.h>

#define INT_PATH(path) "/int/" path
#define RECORD_STORAGE "storage"

typedef struct Storage Storage;
typedef struct File File;
typedef enum {
    FSAM_READ,
    FSAM_WRITE
} FS_AccessMode;
typedef enum {
    FSOM_OPEN_EXISTING,
    FSOM_CREATE_ALWAYS
} FS_OpenMode;

File* storage_file_alloc(Storage* storage);
bool storage_file_open(File* file, const char* path, FS_AccessMode access, FS_OpenMode mode);
size_t storage_file_read(File* file, void* data, size_t size);
size_t storage_file_write(File* file, const void* data, size_t size);
uint64_t storage_file_size(File* file);
const char* storage_file_get_error_desc(File* file);
void storage_file_close(File* file);
void storage_file_free(File* file);
