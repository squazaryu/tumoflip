#include "arf_elf_metadata.h"
#include <string.h>

static uint16_t arf_u16(const uint8_t* bytes) {
    return (uint16_t)bytes[0] | (uint16_t)bytes[1] << 8;
}

static uint32_t arf_u32(const uint8_t* bytes) {
    return (uint32_t)arf_u16(bytes) | (uint32_t)arf_u16(bytes + 2) << 16;
}

static bool arf_range(uint32_t offset, uint32_t size, uint64_t file_size) {
    return (uint64_t)offset + size <= file_size;
}

ArfElfStatus arf_elf_metadata_read(
    ArfElfRead read,
    void* context,
    uint64_t file_size,
    ArfElfMetadata* metadata) {
    memset(metadata, 0, sizeof(*metadata));
    uint8_t header[52];
    if(file_size < sizeof(header) || file_size > 16U * 1024U * 1024U) return ArfElfInvalid;
    if(!read(context, 0, header, sizeof(header))) return ArfElfIoError;
    if(memcmp(header, "\177ELF\1\1\1", 7) || arf_u16(header + 16) != 1 ||
       arf_u16(header + 18) != 40 || arf_u32(header + 20) != 1 ||
       arf_u16(header + 40) != sizeof(header) || arf_u16(header + 46) != 40)
        return ArfElfInvalid;
    uint32_t table = arf_u32(header + 32);
    uint16_t count = arf_u16(header + 48);
    uint16_t strings_index = arf_u16(header + 50);
    if(!count || count > 256 || strings_index >= count ||
       !arf_range(table, (uint32_t)count * 40, file_size))
        return ArfElfInvalid;
    uint8_t section[40];
    if(!read(context, table + (uint32_t)strings_index * 40, section, sizeof(section)))
        return ArfElfIoError;
    uint32_t strings = arf_u32(section + 16);
    uint32_t strings_size = arf_u32(section + 20);
    if(arf_u32(section + 4) != 3 || !strings_size || !arf_range(strings, strings_size, file_size))
        return ArfElfInvalid;
    bool found = false;
    for(uint16_t index = 0; index < count; index++) {
        if(!read(context, table + (uint32_t)index * 40, section, sizeof(section)))
            return ArfElfIoError;
        uint32_t name_offset = arf_u32(section);
        uint32_t offset = arf_u32(section + 16);
        uint32_t size = arf_u32(section + 20);
        // NOBITS occupies RAM, not bytes in the ELF file.
        if(arf_u32(section + 4) != 8 && !arf_range(offset, size, file_size)) return ArfElfInvalid;
        if(name_offset >= strings_size) return ArfElfInvalid;
        char name[64];
        size_t length = strings_size - name_offset;
        if(length > sizeof(name)) length = sizeof(name);
        if(!read(context, strings + name_offset, name, length)) return ArfElfIoError;
        if(!memchr(name, 0, length)) return ArfElfInvalid;
        if(strcmp(name, ".fapmeta")) continue;
        if(found || size != 85 || arf_u32(section + 4) != 1) return ArfElfInvalid;
        uint8_t manifest[85];
        if(!read(context, offset, manifest, sizeof(manifest))) return ArfElfIoError;
        if(arf_u32(manifest) != 0x52474448 || arf_u32(manifest + 4) != 1) return ArfElfBadManifest;
        metadata->api_minor = arf_u16(manifest + 8);
        metadata->api_major = arf_u16(manifest + 10);
        metadata->target = arf_u16(manifest + 12);
        metadata->app_version = arf_u32(manifest + 16);
        memcpy(metadata->name, manifest + 20, 32);
        metadata->name[32] = 0;
        found = true;
    }
    return found ? ArfElfOk : ArfElfInvalid;
}
