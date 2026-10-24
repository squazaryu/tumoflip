#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Shared FAP-linked reader, retaining the existing ARF source interface. It
 * reads at most85 bytes per callback and never invokes the application loader. */
typedef bool (*ArfElfRead)(void* context, uint32_t offset, void* data, size_t size);
typedef enum { ArfElfOk, ArfElfInvalid, ArfElfBadManifest, ArfElfIoError } ArfElfStatus;
typedef struct {
    uint16_t api_major, api_minor, target;
    uint32_t app_version;
    char name[33];
} ArfElfMetadata;
ArfElfStatus arf_elf_metadata_read(ArfElfRead read, void* context, uint64_t file_size, ArfElfMetadata* metadata);
