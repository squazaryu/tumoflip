#include "arf_file_probe.h"
#include <flipper_application/flipper_application.h>
#include <flipper_application/elf/elf_api_interface.h>
#include <string.h>

extern const ElfApiInterface* const firmware_api_interface;

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
    FlipperApplication* app = flipper_application_alloc(storage, firmware_api_interface);
    if(!app) {
        result->status = ArfFileNoMemory;
        return;
    }
    // Manifest-only: no asset extraction, mapping, constructors or app execution.
    const FlipperApplicationPreloadStatus status = flipper_application_preload_manifest(app, path);
    switch(status) {
    case FlipperApplicationPreloadStatusSuccess: {
        const FlipperApplicationManifest* manifest = flipper_application_get_manifest(app);
        result->status = ArfFileHeaderCompatible;
        result->api_major = manifest->base.api_version.major;
        result->api_minor = manifest->base.api_version.minor;
        result->target = manifest->base.hardware_target_id;
        result->app_version = manifest->app_version;
        memcpy(result->name, manifest->name, 32);
        result->name[32] = 0;
        break;
    }
    case FlipperApplicationPreloadStatusInvalidFile:
        result->status = ArfFileInvalid;
        break;
    case FlipperApplicationPreloadStatusInvalidManifest:
        result->status = ArfFileInvalidManifest;
        break;
    case FlipperApplicationPreloadStatusApiTooOld:
        result->status = ArfFileApiOld;
        break;
    case FlipperApplicationPreloadStatusApiTooNew:
        result->status = ArfFileApiNew;
        break;
    case FlipperApplicationPreloadStatusTargetMismatch:
        result->status = ArfFileWrongTarget;
        break;
    case FlipperApplicationPreloadStatusNotEnoughMemory:
        result->status = ArfFileNoMemory;
        break;
    }
    flipper_application_free(app);
    // A compatible header proves neither payload integrity nor resolved imports.
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
        return "Invalid ELF file";
    case ArfFileInvalidManifest:
        return "Invalid manifest";
    case ArfFileApiOld:
        return "API too old";
    case ArfFileApiNew:
        return "API too new";
    case ArfFileWrongTarget:
        return "Wrong hardware target";
    case ArfFileNoMemory:
        return "Not enough memory";
    default:
        return "Not checked";
    }
}
