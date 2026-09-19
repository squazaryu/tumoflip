#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef bool (*ArfElfRead)(void* context, uint32_t offset, void* data, size_t size);
typedef enum {
    ArfElfOk,
    ArfElfInvalid,
    ArfElfBadManifest,
    ArfElfIoError
} ArfElfStatus;
typedef struct {
    uint16_t api_major;
    uint16_t api_minor;
    uint16_t target;
    uint32_t app_version;
    char name[33];
} ArfElfMetadata;

ArfElfStatus arf_elf_metadata_read(
    ArfElfRead read,
    void* context,
    uint64_t file_size,
    ArfElfMetadata* metadata);
