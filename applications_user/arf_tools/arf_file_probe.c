#include "arf_file_probe.h"
#include "arf_elf_metadata.h"
#include <flipper_application/elf/elf_api_interface.h>
#include <furi_hal_version.h>
#include <string.h>

extern const ElfApiInterface* const firmware_api_interface;

static bool arf_probe_read(void* context, uint32_t offset, void* bytes, size_t size) {
    File* file = context;
    return storage_file_seek(file, offset, true) && storage_file_read(file, bytes, size) == size;
}

void arf_file_probe(Storage* storage, const char* path, ArfFileProbe* result) {
    memset(result, 0, sizeof(*result));
    result->status = ArfFileIoError;
    FileInfo info;
    const FS_Error error = storage_common_stat(storage, path, &info);
    if(error == FSE_NOT_EXIST) {
        result->status = ArfFileMissing;
        return;
    }
    if(error != FSE_OK) return;
    if(file_info_is_dir(&info)) {
        result->status = ArfFileNotRegular;
        return;
    }
    result->bytes = info.size;
    if(!info.size) {
        result->status = ArfFileEmpty;
        return;
    }
    if(info.size > 16U * 1024U * 1024U) {
        result->status = ArfFileTooLarge;
        return;
    }
    File* file = storage_file_alloc(storage);
    do {
        if(!storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
            if(storage_file_get_error(file) == FSE_ALREADY_OPEN) result->status = ArfFileInUse;
            break;
        }
        const uint64_t size = storage_file_size(file);
        if(size != info.size) break;
        ArfElfMetadata metadata;
        const ArfElfStatus status = arf_elf_metadata_read(arf_probe_read, file, size, &metadata);
        if(status != ArfElfOk) {
            result->status =
                status == ArfElfIoError ?
                    ArfFileIoError :
                    (status == ArfElfBadManifest ? ArfFileInvalidManifest : ArfFileInvalid);
            break;
        }
        result->api_major = metadata.api_major;
        result->api_minor = metadata.api_minor;
        result->target = metadata.target;
        result->app_version = metadata.app_version;
        memcpy(result->name, metadata.name, sizeof(result->name));
        if(metadata.target != furi_hal_version_get_hw_target())
            result->status = ArfFileWrongTarget;
        else if(metadata.api_major < firmware_api_interface->api_version_major)
            result->status = ArfFileApiOld;
        else if(metadata.api_major > firmware_api_interface->api_version_major)
            result->status = ArfFileApiNew;
        else if(metadata.api_minor > firmware_api_interface->api_version_minor)
            result->status = ArfFileNewerMinor;
        else
            result->status = ArfFileHeaderCompatible;
    } while(false);
    if(!storage_file_close(file)) result->status = ArfFileIoError;
    storage_file_free(file);
    // Neither payload hashes, imports, assets, nor executable code are loaded.
}

const char* arf_file_status_text(ArfFileStatus status) {
    switch(status) {
    case ArfFileHeaderCompatible:
        return "Header compatible";
    case ArfFileMissing:
        return "Missing file";
    case ArfFileIoError:
        return "Storage error";
    case ArfFileNotRegular:
        return "Not a regular file";
    case ArfFileEmpty:
        return "Empty file";
    case ArfFileTooLarge:
        return "File too large";
    case ArfFileInvalid:
        return "Invalid/unsupported ELF";
    case ArfFileInvalidManifest:
        return "Invalid manifest";
    case ArfFileApiOld:
        return "API too old";
    case ArfFileApiNew:
        return "API too new";
    case ArfFileWrongTarget:
        return "Wrong hardware target";
    case ArfFileInUse:
        return "In use; not rescanned";
    case ArfFileNewerMinor:
        return "Newer API minor; unchecked";
    default:
        return "Not checked";
    }
}
