#include "tumovm_peripheral_manifest.h"

#include <flipper_format/flipper_format.h>

#include <string.h>

#define TUMOVM_PERIPHERAL_FILETYPE       "TumoVM Peripheral"
#define TUMOVM_PERIPHERAL_STATE_FILETYPE "TumoVM Peripheral State"
#define TUMOVM_PERIPHERAL_STATE_VERSION  1U

static bool tumovm_peripheral_has_extension(const char* filename, const char* extension) {
    const char* dot = strrchr(filename, '.');
    return dot != NULL && strcmp(dot, extension) == 0;
}

static bool tumovm_peripheral_mkdir(Storage* storage, const char* path) {
    const FS_Error error = storage_common_mkdir(storage, path);
    return error == FSE_OK || error == FSE_EXIST;
}

static TumoVmPeripheralManifestStatus tumovm_peripheral_package_read(
    Storage* storage,
    const char* path,
    uint32_t current_api,
    TumoVmPeripheralManifest* manifest) {
    FlipperFormat* format = flipper_format_file_alloc(storage);
    FuriString* header = furi_string_alloc();
    FuriString* id = furi_string_alloc();
    FuriString* name = furi_string_alloc();
    FuriString* adapter = furi_string_alloc();
    FuriString* action = furi_string_alloc();
    uint32_t version = 0U;
    uint32_t minimum_api = 0U;
    uint32_t maximum_api = 0U;
    uint32_t aid_size = 0U;
    uint32_t state_size = 0U;
    uint8_t aid[TumoVmPeripheralAidMaximum] = {0};
    uint8_t state[TumoVmPeripheralStateMaximum] = {0};
    bool require_confirmation = false;
    TumoVmPeripheralManifestStatus status = TumoVmPeripheralManifestInvalidArgument;

    do {
        if(!flipper_format_file_open_existing(format, path)) break;
        if(!flipper_format_read_header(format, header, &version)) break;
        if(furi_string_cmp_str(header, TUMOVM_PERIPHERAL_FILETYPE) != 0) {
            status = TumoVmPeripheralManifestUnsupportedVersion;
            break;
        }
        if(!flipper_format_read_string(format, "Package ID", id) ||
           !flipper_format_read_string(format, "Name", name) ||
           !flipper_format_read_string(format, "Adapter", adapter) ||
           !flipper_format_read_string(format, "Action", action) ||
           !flipper_format_read_uint32(format, "Minimum API", &minimum_api, 1U) ||
           !flipper_format_read_uint32(format, "Maximum API", &maximum_api, 1U) ||
           !flipper_format_read_bool(format, "Require confirmation", &require_confirmation, 1U)) {
            break;
        }

        if(furi_string_cmp_str(adapter, "nfc.type4") == 0) {
            if(!flipper_format_read_uint32(format, "AID size", &aid_size, 1U) ||
               aid_size > sizeof(aid) || !flipper_format_read_hex(format, "AID", aid, aid_size) ||
               !flipper_format_read_uint32(format, "State size", &state_size, 1U) ||
               state_size > sizeof(state) ||
               !flipper_format_read_hex(format, "Initial state", state, state_size)) {
                break;
            }
        }

        const TumoVmPeripheralManifestInput input = {
            .version = version,
            .id = furi_string_get_cstr(id),
            .name = furi_string_get_cstr(name),
            .adapter = furi_string_get_cstr(adapter),
            .action = furi_string_get_cstr(action),
            .minimum_api = minimum_api,
            .maximum_api = maximum_api,
            .aid = aid,
            .aid_size = aid_size,
            .initial_state = state,
            .state_size = state_size,
            .require_confirmation = require_confirmation,
        };
        status = tumovm_peripheral_manifest_build(&input, current_api, manifest);
    } while(false);

    furi_string_free(action);
    furi_string_free(adapter);
    furi_string_free(name);
    furi_string_free(id);
    furi_string_free(header);
    flipper_format_free(format);
    return status;
}

static void tumovm_peripheral_package_swap(
    TumoVmPeripheralPackage* first,
    TumoVmPeripheralPackage* second) {
    const TumoVmPeripheralPackage temporary = *first;
    *first = *second;
    *second = temporary;
}

size_t tumovm_peripheral_packages_load(
    Storage* storage,
    uint32_t current_api,
    TumoVmPeripheralPackage* packages,
    size_t capacity) {
    if(storage == NULL || packages == NULL || capacity == 0U) return 0U;
    const size_t bounded_capacity =
        capacity < TumoVmPeripheralMaximumPackages ? capacity : TumoVmPeripheralMaximumPackages;
    memset(packages, 0, sizeof(*packages) * bounded_capacity);

    File* directory = storage_file_alloc(storage);
    FileInfo info;
    char filename[64];
    size_t count = 0U;
    if(storage_dir_open(directory, TUMOVM_PERIPHERAL_PACKAGE_DIR)) {
        while(count < bounded_capacity &&
              storage_dir_read(directory, &info, filename, sizeof(filename))) {
            if(file_info_is_dir(&info) || !tumovm_peripheral_has_extension(filename, ".tper")) {
                continue;
            }
            TumoVmPeripheralPackage* package = &packages[count++];
            strlcpy(package->filename, filename, sizeof(package->filename));
            if(info.size == 0U || info.size > TumoVmPeripheralMaximumManifestSize) {
                package->status = TumoVmPeripheralManifestInvalidArgument;
                continue;
            }
            char path[160];
            snprintf(path, sizeof(path), TUMOVM_PERIPHERAL_PACKAGE_DIR "/%s", filename);
            package->status =
                tumovm_peripheral_package_read(storage, path, current_api, &package->manifest);
        }
        storage_dir_close(directory);
    }
    storage_file_free(directory);

    for(size_t outer = 0U; outer < count; outer++) {
        for(size_t inner = outer + 1U; inner < count; inner++) {
            const char* first = packages[outer].status == TumoVmPeripheralManifestOk ?
                                    packages[outer].manifest.name :
                                    packages[outer].filename;
            const char* second = packages[inner].status == TumoVmPeripheralManifestOk ?
                                     packages[inner].manifest.name :
                                     packages[inner].filename;
            if(strcmp(first, second) > 0) {
                tumovm_peripheral_package_swap(&packages[outer], &packages[inner]);
            }
        }
    }
    return count;
}

static bool tumovm_peripheral_state_path(
    const TumoVmPeripheralManifest* manifest,
    const char* suffix,
    char* path,
    size_t path_size) {
    if(manifest == NULL || suffix == NULL || path == NULL) return false;
    const int written =
        snprintf(path, path_size, TUMOVM_PERIPHERAL_STATE_DIR "/%s.%s", manifest->id, suffix);
    return written > 0 && (size_t)written < path_size;
}

bool tumovm_peripheral_state_load(
    Storage* storage,
    const TumoVmPeripheralManifest* manifest,
    uint8_t state[TumoVmPeripheralStateMaximum]) {
    if(storage == NULL || manifest == NULL || state == NULL || manifest->state_size == 0U) {
        return false;
    }
    memcpy(state, manifest->initial_state, manifest->state_size);

    char path[128];
    if(!tumovm_peripheral_state_path(manifest, "tvs", path, sizeof(path)) ||
       !storage_file_exists(storage, path)) {
        return true;
    }

    FlipperFormat* format = flipper_format_file_alloc(storage);
    FuriString* header = furi_string_alloc();
    uint32_t version = 0U;
    uint32_t state_size = 0U;
    uint8_t persisted[TumoVmPeripheralStateMaximum] = {0};
    bool success = false;
    do {
        if(!flipper_format_file_open_existing(format, path) ||
           !flipper_format_read_header(format, header, &version) ||
           version != TUMOVM_PERIPHERAL_STATE_VERSION ||
           furi_string_cmp_str(header, TUMOVM_PERIPHERAL_STATE_FILETYPE) != 0 ||
           !flipper_format_read_uint32(format, "State size", &state_size, 1U) ||
           state_size != manifest->state_size ||
           !flipper_format_read_hex(format, "Data", persisted, state_size)) {
            break;
        }
        memcpy(state, persisted, state_size);
        success = true;
    } while(false);
    furi_string_free(header);
    flipper_format_free(format);
    return success;
}

bool tumovm_peripheral_state_save(
    Storage* storage,
    const TumoVmPeripheralManifest* manifest,
    const uint8_t state[TumoVmPeripheralStateMaximum]) {
    if(storage == NULL || manifest == NULL || state == NULL || manifest->state_size == 0U ||
       !tumovm_peripheral_mkdir(storage, TUMOVM_PERIPHERAL_DATA_DIR) ||
       !tumovm_peripheral_mkdir(storage, TUMOVM_PERIPHERAL_STATE_DIR)) {
        return false;
    }

    char path[128];
    char temporary_path[128];
    char backup_path[128];
    if(!tumovm_peripheral_state_path(manifest, "tvs", path, sizeof(path)) ||
       !tumovm_peripheral_state_path(manifest, "tmp", temporary_path, sizeof(temporary_path)) ||
       !tumovm_peripheral_state_path(manifest, "bak", backup_path, sizeof(backup_path))) {
        return false;
    }

    storage_common_remove(storage, temporary_path);
    FlipperFormat* format = flipper_format_file_alloc(storage);
    bool success = false;
    do {
        if(!flipper_format_file_open_always(format, temporary_path) ||
           !flipper_format_write_header_cstr(
               format, TUMOVM_PERIPHERAL_STATE_FILETYPE, TUMOVM_PERIPHERAL_STATE_VERSION)) {
            break;
        }
        const uint32_t state_size = manifest->state_size;
        if(!flipper_format_write_uint32(format, "State size", &state_size, 1U) ||
           !flipper_format_write_hex(format, "Data", state, state_size)) {
            break;
        }
        success = true;
    } while(false);
    flipper_format_free(format);
    if(!success) {
        storage_common_remove(storage, temporary_path);
        return false;
    }

    storage_common_remove(storage, backup_path);
    const bool had_state = storage_file_exists(storage, path);
    if(had_state && storage_common_rename(storage, path, backup_path) != FSE_OK) {
        storage_common_remove(storage, temporary_path);
        return false;
    }
    if(storage_common_rename(storage, temporary_path, path) != FSE_OK) {
        storage_common_remove(storage, temporary_path);
        if(had_state) storage_common_rename(storage, backup_path, path);
        return false;
    }
    storage_common_remove(storage, backup_path);
    return true;
}
