#include "tumospectrum_capture_flow.h"

#include <flipper_format/flipper_format.h>

#include <stdio.h>
#include <string.h>

#define TUMOSPECTRUM_CAPTURE_PENDING_PATH     EXT_PATH("apps_data/signal_workbench/pending_capture.ff")
#define TUMOSPECTRUM_CAPTURE_PENDING_FILETYPE "TumoSpectrum Capture Snapshot"
#define TUMOSPECTRUM_CAPTURE_PENDING_VERSION  1U
#define TUMOSPECTRUM_CAPTURE_MAX_HASHES       96U

static const char* tumospectrum_capture_flow_directory(TumoSpectrumCaptureType type) {
    if(type == TumoSpectrumCaptureSubGhzRaw) return EXT_PATH("subghz");
    if(type == TumoSpectrumCaptureInfraredRaw) return EXT_PATH("infrared");
    return NULL;
}

static const char* tumospectrum_capture_flow_extension(TumoSpectrumCaptureType type) {
    if(type == TumoSpectrumCaptureSubGhzRaw) return ".sub";
    if(type == TumoSpectrumCaptureInfraredRaw) return ".ir";
    return NULL;
}

static bool tumospectrum_capture_flow_has_extension(const char* name, const char* extension) {
    if(!name || !extension) return false;
    const size_t name_length = strlen(name);
    const size_t extension_length = strlen(extension);
    return name_length > extension_length &&
           strcmp(name + name_length - extension_length, extension) == 0;
}

static uint32_t tumospectrum_capture_flow_hash(const char* text) {
    uint32_t hash = UINT32_C(2166136261);
    for(const unsigned char* cursor = (const unsigned char*)text; *cursor; cursor++) {
        hash ^= *cursor;
        hash *= UINT32_C(16777619);
    }
    return hash;
}

static bool tumospectrum_capture_flow_hash_exists(
    const uint32_t* hashes,
    size_t hash_count,
    uint32_t hash) {
    for(size_t index = 0U; index < hash_count; index++) {
        if(hashes[index] == hash) return true;
    }
    return false;
}

static size_t tumospectrum_capture_flow_snapshot(
    Storage* storage,
    TumoSpectrumCaptureType type,
    uint32_t* hashes,
    bool* overflow) {
    const char* directory_path = tumospectrum_capture_flow_directory(type);
    const char* extension = tumospectrum_capture_flow_extension(type);
    if(!directory_path || !extension) return 0U;

    File* directory = storage_file_alloc(storage);
    FileInfo info;
    char name[96];
    size_t count = 0U;
    *overflow = false;
    if(storage_dir_open(directory, directory_path)) {
        while(storage_dir_read(directory, &info, name, sizeof(name))) {
            if(file_info_is_dir(&info) ||
               !tumospectrum_capture_flow_has_extension(name, extension)) {
                continue;
            }
            if(count >= TUMOSPECTRUM_CAPTURE_MAX_HASHES) {
                *overflow = true;
                continue;
            }
            hashes[count++] = tumospectrum_capture_flow_hash(name);
        }
        storage_dir_close(directory);
    }
    storage_file_free(directory);
    return count;
}

bool tumospectrum_capture_flow_prepare(Storage* storage, TumoSpectrumCaptureType type) {
    if(!storage || !tumospectrum_capture_flow_directory(type)) return false;
    const FS_Error mkdir_result =
        storage_common_mkdir(storage, EXT_PATH("apps_data/signal_workbench"));
    if(mkdir_result != FSE_OK && mkdir_result != FSE_EXIST) return false;

    uint32_t hashes[TUMOSPECTRUM_CAPTURE_MAX_HASHES];
    bool overflow = false;
    const size_t hash_count = tumospectrum_capture_flow_snapshot(storage, type, hashes, &overflow);
    const uint32_t stored_type = (uint32_t)type;
    const uint32_t stored_count = (uint32_t)hash_count;
    const uint32_t stored_overflow = overflow ? 1U : 0U;

    FlipperFormat* format = flipper_format_file_alloc(storage);
    bool success = false;
    do {
        if(!flipper_format_file_open_always(format, TUMOSPECTRUM_CAPTURE_PENDING_PATH) ||
           !flipper_format_write_header_cstr(
               format,
               TUMOSPECTRUM_CAPTURE_PENDING_FILETYPE,
               TUMOSPECTRUM_CAPTURE_PENDING_VERSION) ||
           !flipper_format_write_uint32(format, "Capture type", &stored_type, 1U) ||
           !flipper_format_write_uint32(format, "Snapshot count", &stored_count, 1U) ||
           !flipper_format_write_uint32(format, "Snapshot overflow", &stored_overflow, 1U)) {
            break;
        }
        if(hash_count > 0U &&
           !flipper_format_write_uint32(format, "Name hashes", hashes, hash_count)) {
            break;
        }
        success = true;
    } while(false);
    flipper_format_free(format);
    if(!success) storage_common_remove(storage, TUMOSPECTRUM_CAPTURE_PENDING_PATH);
    return success;
}

bool tumospectrum_capture_flow_resume(
    Storage* storage,
    TumoSpectrumCaptureType* type,
    char* path,
    size_t path_size,
    bool* needs_selection) {
    if(!storage || !type || !path || path_size == 0U || !needs_selection) return false;
    path[0] = '\0';
    *needs_selection = false;
    if(!storage_file_exists(storage, TUMOSPECTRUM_CAPTURE_PENDING_PATH)) return false;

    uint32_t stored_type = 0U;
    uint32_t stored_count = 0U;
    uint32_t stored_overflow = 0U;
    uint32_t hashes[TUMOSPECTRUM_CAPTURE_MAX_HASHES];
    FuriString* header = furi_string_alloc();
    FlipperFormat* format = flipper_format_file_alloc(storage);
    uint32_t version = 0U;
    bool valid = false;
    do {
        if(!flipper_format_file_open_existing(format, TUMOSPECTRUM_CAPTURE_PENDING_PATH) ||
           !flipper_format_read_header(format, header, &version) ||
           version != TUMOSPECTRUM_CAPTURE_PENDING_VERSION ||
           furi_string_cmp_str(header, TUMOSPECTRUM_CAPTURE_PENDING_FILETYPE) != 0 ||
           !flipper_format_read_uint32(format, "Capture type", &stored_type, 1U) ||
           !flipper_format_read_uint32(format, "Snapshot count", &stored_count, 1U) ||
           !flipper_format_read_uint32(format, "Snapshot overflow", &stored_overflow, 1U) ||
           stored_count > TUMOSPECTRUM_CAPTURE_MAX_HASHES) {
            break;
        }
        if(stored_count > 0U) {
            flipper_format_rewind(format);
            if(!flipper_format_read_uint32(format, "Name hashes", hashes, stored_count)) break;
        }
        valid = true;
    } while(false);
    flipper_format_free(format);
    furi_string_free(header);
    storage_common_remove(storage, TUMOSPECTRUM_CAPTURE_PENDING_PATH);
    if(!valid) return false;

    *type = (TumoSpectrumCaptureType)stored_type;
    const char* directory_path = tumospectrum_capture_flow_directory(*type);
    const char* extension = tumospectrum_capture_flow_extension(*type);
    if(!directory_path || !extension) return false;
    if(stored_overflow != 0U) {
        *needs_selection = true;
        return true;
    }

    File* directory = storage_file_alloc(storage);
    FileInfo info;
    char name[96];
    size_t new_count = 0U;
    if(storage_dir_open(directory, directory_path)) {
        while(storage_dir_read(directory, &info, name, sizeof(name))) {
            if(file_info_is_dir(&info) ||
               !tumospectrum_capture_flow_has_extension(name, extension) ||
               tumospectrum_capture_flow_hash_exists(
                   hashes, stored_count, tumospectrum_capture_flow_hash(name))) {
                continue;
            }
            new_count++;
            if(new_count == 1U) {
                const int length = snprintf(path, path_size, "%s/%s", directory_path, name);
                if(length <= 0 || (size_t)length >= path_size) path[0] = '\0';
            }
        }
        storage_dir_close(directory);
    }
    storage_file_free(directory);

    if(new_count > 1U) {
        path[0] = '\0';
        *needs_selection = true;
    }
    return true;
}
