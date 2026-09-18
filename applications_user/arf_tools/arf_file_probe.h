#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <storage/storage.h>

typedef enum {
    ArfFileHeaderCompatible,
    ArfFileMissing,
    ArfFileIoError,
    ArfFileNotRegular,
    ArfFileEmpty,
    ArfFileTooLarge,
    ArfFileInvalid,
    ArfFileInvalidManifest,
    ArfFileApiOld,
    ArfFileApiNew,
    ArfFileWrongTarget,
    ArfFileNoMemory,
} ArfFileStatus;

typedef struct {
    ArfFileStatus status;
    uint64_t bytes;
    uint16_t api_major;
    uint16_t api_minor;
    uint16_t target;
    uint32_t app_version;
    char name[33];
    bool integrity_verified;
    bool imports_verified;
} ArfFileProbe;

void arf_file_probe(Storage* storage, const char* path, ArfFileProbe* result);
const char* arf_file_status_text(ArfFileStatus status);
