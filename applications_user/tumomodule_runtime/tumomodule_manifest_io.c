#include "tumomodule_manifest_io.h"

#include <flipper_format/flipper_format.h>

#include <string.h>

#define TUMOMODULE_FILETYPE "TumoModule Package"

static bool tumomodule_has_extension(const char* name, const char* extension) {
    const char* dot = strrchr(name, '.');
    return dot != NULL && strcmp(dot, extension) == 0;
}

static TumoModuleManifestStatus tumomodule_package_read(
    Storage* storage,
    const char* path,
    uint32_t current_api,
    TumoModuleManifest* manifest) {
    FlipperFormat* format = flipper_format_file_alloc(storage);
    FuriString* header = furi_string_alloc();
    FuriString* id = furi_string_alloc();
    FuriString* name = furi_string_alloc();
    FuriString* adapter = furi_string_alloc();
    FuriString* bus = furi_string_alloc();
    uint32_t version = 0;
    uint32_t minimum_api = 0;
    uint32_t maximum_api = 0;
    uint32_t address = 0;
    uint32_t baud = 0;
    bool external_power = false;
    TumoModuleManifestStatus status = TumoModuleManifestInvalidArgument;

    do {
        if(!flipper_format_file_open_existing(format, path)) break;
        if(!flipper_format_read_header(format, header, &version)) break;
        if(furi_string_cmp_str(header, TUMOMODULE_FILETYPE) != 0) {
            status = TumoModuleManifestUnsupportedVersion;
            break;
        }
        if(!flipper_format_read_string(format, "Module ID", id) ||
           !flipper_format_read_string(format, "Name", name) ||
           !flipper_format_read_string(format, "Adapter", adapter) ||
           !flipper_format_read_string(format, "Bus", bus) ||
           !flipper_format_read_uint32(format, "Minimum API", &minimum_api, 1) ||
           !flipper_format_read_uint32(format, "Maximum API", &maximum_api, 1) ||
           !flipper_format_read_uint32(format, "Address", &address, 1) ||
           !flipper_format_read_uint32(format, "Baud", &baud, 1) ||
           !flipper_format_read_bool(format, "External Power", &external_power, 1)) {
            break;
        }
        const TumoModuleManifestInput input = {
            .version = version,
            .id = furi_string_get_cstr(id),
            .name = furi_string_get_cstr(name),
            .adapter = furi_string_get_cstr(adapter),
            .bus = furi_string_get_cstr(bus),
            .minimum_api = minimum_api,
            .maximum_api = maximum_api,
            .address = address,
            .baud = baud,
            .external_power = external_power,
        };
        status = tumomodule_manifest_build(&input, current_api, manifest);
    } while(false);

    furi_string_free(bus);
    furi_string_free(adapter);
    furi_string_free(name);
    furi_string_free(id);
    furi_string_free(header);
    flipper_format_free(format);
    return status;
}

static void tumomodule_package_swap(TumoModulePackage* first, TumoModulePackage* second) {
    TumoModulePackage temporary = *first;
    *first = *second;
    *second = temporary;
}

size_t tumomodule_packages_load(
    Storage* storage,
    uint32_t current_api,
    TumoModulePackage* packages,
    size_t capacity) {
    if(storage == NULL || packages == NULL || capacity == 0) return 0;
    const size_t bounded_capacity =
        capacity < TumoModuleMaximumPackages ? capacity : TumoModuleMaximumPackages;
    memset(packages, 0, sizeof(*packages) * bounded_capacity);

    File* directory = storage_file_alloc(storage);
    FileInfo info;
    char filename[64];
    size_t count = 0;
    if(storage_dir_open(directory, TUMOMODULE_PACKAGE_DIR)) {
        while(count < bounded_capacity &&
              storage_dir_read(directory, &info, filename, sizeof(filename))) {
            if(file_info_is_dir(&info) || !tumomodule_has_extension(filename, ".tmod")) continue;
            TumoModulePackage* package = &packages[count++];
            strlcpy(package->filename, filename, sizeof(package->filename));
            if(info.size == 0 || info.size > TumoModuleMaximumManifestSize) {
                package->status = TumoModuleManifestInvalidArgument;
                continue;
            }
            char path[160];
            snprintf(path, sizeof(path), TUMOMODULE_PACKAGE_DIR "/%s", filename);
            package->status =
                tumomodule_package_read(storage, path, current_api, &package->manifest);
        }
        storage_dir_close(directory);
    }
    storage_file_free(directory);

    for(size_t outer = 0; outer < count; outer++) {
        for(size_t inner = outer + 1; inner < count; inner++) {
            const char* first = packages[outer].status == TumoModuleManifestOk ?
                                    packages[outer].manifest.name :
                                    packages[outer].filename;
            const char* second = packages[inner].status == TumoModuleManifestOk ?
                                     packages[inner].manifest.name :
                                     packages[inner].filename;
            if(strcmp(first, second) > 0) {
                tumomodule_package_swap(&packages[outer], &packages[inner]);
            }
        }
    }
    return count;
}
