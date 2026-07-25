#include "xremote_device_bundle.h"

#include "infrared/infrared_remote.h"
#include "xremote_subghz.h"

#include <flipper_format/flipper_format.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define XREMOTE_DEVICE_BUNDLE_PROFILE_FILE  "profile.tdevice"
#define XREMOTE_DEVICE_BUNDLE_MANIFEST_FILE "manifest.tdeck"
#define XREMOTE_DEVICE_BUNDLE_IR_FILE       "remote.ir"
#define XREMOTE_DEVICE_BUNDLE_LEAF_MAX      48U

typedef struct {
    char name[XREMOTE_DEVICE_NAME_MAX + 1U];
    char profile_file[XREMOTE_DEVICE_BUNDLE_LEAF_MAX];
    char ir_file[XREMOTE_DEVICE_BUNDLE_LEAF_MAX];
    uint8_t rf_count;
    char rf_file[XREMOTE_DEVICE_RF_COMMAND_MAX][XREMOTE_DEVICE_BUNDLE_LEAF_MAX];
} XRemoteDeviceBundleManifest;

typedef struct {
    XRemoteDeviceBundleManifest manifest;
    char final_folder[XREMOTE_DEVICE_PATH_MAX];
    char staging_folder[XREMOTE_DEVICE_PATH_MAX + 5U];
    char destination[XREMOTE_DEVICE_PATH_MAX];
    char staging_manifest[XREMOTE_DEVICE_PATH_MAX];
} XRemoteDeviceBundleExportWorkspace;

typedef struct {
    XRemoteDeviceBundleManifest manifest;
    char bundle_folder[XREMOTE_DEVICE_PATH_MAX];
    char source_profile_path[XREMOTE_DEVICE_PATH_MAX];
    char final_folder[XREMOTE_DEVICE_PATH_MAX];
    char staging_folder[XREMOTE_DEVICE_PATH_MAX + 5U];
    char source_path[XREMOTE_DEVICE_PATH_MAX];
    char staging_path[XREMOTE_DEVICE_PATH_MAX];
    char final_path[XREMOTE_DEVICE_PATH_MAX];
    char installed_profile_path[XREMOTE_DEVICE_PATH_MAX];
    char staged_profile_path[XREMOTE_DEVICE_PATH_MAX];
    char bundled_profile_path[XREMOTE_DEVICE_PATH_MAX];
} XRemoteDeviceBundleImportWorkspace;

static void xremote_device_bundle_copy(char* output, size_t output_size, const char* value) {
    if(output_size == 0U) return;
    snprintf(output, output_size, "%s", value ? value : "");
}

static bool xremote_device_bundle_has_extension(const char* path, const char* extension) {
    if(!path || !extension) return false;
    const size_t path_length = strlen(path);
    const size_t extension_length = strlen(extension);
    return path_length > extension_length &&
           strcmp(path + path_length - extension_length, extension) == 0;
}

static bool xremote_device_bundle_leaf_valid(const char* leaf, const char* extension) {
    if(!leaf || leaf[0] == '\0' || strchr(leaf, '/') || strchr(leaf, '\\') ||
       strcmp(leaf, ".") == 0 || strcmp(leaf, "..") == 0) {
        return false;
    }
    return xremote_device_bundle_has_extension(leaf, extension);
}

static void xremote_device_bundle_safe_name(const char* source, char* output, size_t output_size) {
    size_t written = 0U;
    if(output_size == 0U) return;
    for(size_t index = 0U; source && source[index] != '\0' && written + 1U < output_size;
        index++) {
        const unsigned char value = (unsigned char)source[index];
        if(isalnum(value) || value == '-' || value == '_') {
            output[written++] = (char)value;
        } else if(value == ' ' && written > 0U && output[written - 1U] != '_') {
            output[written++] = '_';
        }
    }
    if(written == 0U) {
        xremote_device_bundle_copy(output, output_size, "Device");
    } else {
        output[written] = '\0';
    }
}

static bool xremote_device_bundle_join(
    char* output,
    size_t output_size,
    const char* folder,
    const char* leaf) {
    const int written = snprintf(output, output_size, "%s/%s", folder, leaf);
    return written > 0 && (size_t)written < output_size;
}

static bool xremote_device_bundle_make_unique_folder(
    Storage* storage,
    const char* root,
    const char* preferred_name,
    const char* suffix_name,
    char* output,
    size_t output_size,
    uint32_t* sequence) {
    char safe_name[XREMOTE_DEVICE_NAME_MAX + 1U];
    xremote_device_bundle_safe_name(preferred_name, safe_name, sizeof(safe_name));
    for(uint32_t suffix = 0U; suffix < 100U; suffix++) {
        int written = 0;
        if(suffix == 0U) {
            written = snprintf(output, output_size, "%s/%s%s", root, safe_name, suffix_name);
        } else {
            written = snprintf(
                output,
                output_size,
                "%s/%s%s_%lu",
                root,
                safe_name,
                suffix_name,
                (unsigned long)(suffix + 1U));
        }
        if(written <= 0 || (size_t)written >= output_size) {
            output[0] = '\0';
            return false;
        }
        if(!storage_common_exists(storage, output)) {
            if(sequence) *sequence = suffix + 1U;
            return true;
        }
    }
    output[0] = '\0';
    return false;
}

static bool
    xremote_device_bundle_source_valid(Storage* storage, const char* path, const char* extension) {
    FileInfo info;
    return path && path[0] != '\0' && xremote_device_bundle_has_extension(path, extension) &&
           storage_common_stat(storage, path, &info) == FSE_OK && !file_info_is_dir(&info) &&
           info.size > 0U;
}

static bool xremote_device_bundle_ir_valid(Storage* storage, const char* path) {
    if(!xremote_device_bundle_source_valid(storage, path, ".ir")) return false;
    FuriString* source_path = furi_string_alloc_set_str(path);
    InfraredRemote* remote = infrared_remote_alloc();
    const bool valid = remote && infrared_remote_load(remote, source_path) &&
                       infrared_remote_get_button_count(remote) > 0U;
    if(remote) infrared_remote_free(remote);
    furi_string_free(source_path);
    return valid;
}

static bool xremote_device_bundle_rf_valid(
    Storage* storage,
    const char* path,
    const XRemoteDeviceRfCommand* command) {
    if(!xremote_device_bundle_source_valid(storage, path, ".sub")) return false;
    XRemoteSubGhzInfo info;
    const XRemoteSubGhzStatus status = xremote_subghz_inspect(storage, path, &info);
    return status == XRemoteSubGhzStatusOk && !info.changing_code &&
           strcmp(info.protocol, command->protocol) == 0 && info.adapter == command->adapter;
}

static bool xremote_device_bundle_write_manifest(
    Storage* storage,
    const char* path,
    const XRemoteDeviceProfile* profile) {
    FlipperFormat* format = flipper_format_file_alloc(storage);
    bool success = false;
    do {
        if(!flipper_format_file_open_always(format, path)) break;
        if(!flipper_format_write_header_cstr(
               format, XREMOTE_DEVICE_BUNDLE_HEADER, XREMOTE_DEVICE_BUNDLE_VERSION)) {
            break;
        }
        if(!flipper_format_write_string_cstr(format, "Name", profile->name)) break;
        if(!flipper_format_write_string_cstr(
               format, "Profile File", XREMOTE_DEVICE_BUNDLE_PROFILE_FILE)) {
            break;
        }
        if(!flipper_format_write_string_cstr(
               format, "IR File", profile->ir_path[0] ? XREMOTE_DEVICE_BUNDLE_IR_FILE : "")) {
            break;
        }
        const uint32_t rf_count = profile->rf_count;
        if(!flipper_format_write_uint32(format, "RF Count", &rf_count, 1U)) break;

        bool commands_written = true;
        for(uint32_t index = 0U; index < profile->rf_count; index++) {
            char key[24];
            char filename[XREMOTE_DEVICE_BUNDLE_LEAF_MAX];
            snprintf(key, sizeof(key), "RF%lu File", (unsigned long)index);
            snprintf(filename, sizeof(filename), "rf_%lu.sub", (unsigned long)index);
            if(!flipper_format_write_string_cstr(format, key, filename)) {
                commands_written = false;
                break;
            }
        }
        if(!commands_written) break;
        success = flipper_format_file_close(format);
    } while(false);
    flipper_format_free(format);
    return success;
}

static bool xremote_device_bundle_read_string(
    FlipperFormat* format,
    const char* key,
    char* output,
    size_t output_size) {
    FuriString* value = furi_string_alloc();
    flipper_format_rewind(format);
    const bool success = flipper_format_read_string(format, key, value);
    if(success) {
        xremote_device_bundle_copy(output, output_size, furi_string_get_cstr(value));
    }
    furi_string_free(value);
    return success;
}

static bool xremote_device_bundle_read_manifest(
    Storage* storage,
    const char* path,
    XRemoteDeviceBundleManifest* manifest) {
    if(!storage || !path || !manifest) return false;
    memset(manifest, 0, sizeof(*manifest));
    FlipperFormat* format = flipper_format_buffered_file_alloc(storage);
    FuriString* header = furi_string_alloc();
    uint32_t version = 0U;
    uint32_t rf_count = 0U;
    bool success = false;

    do {
        if(!flipper_format_buffered_file_open_existing(format, path)) break;
        if(!flipper_format_read_header(format, header, &version)) break;
        if(strcmp(furi_string_get_cstr(header), XREMOTE_DEVICE_BUNDLE_HEADER) != 0 ||
           version != XREMOTE_DEVICE_BUNDLE_VERSION) {
            break;
        }
        if(!xremote_device_bundle_read_string(
               format, "Name", manifest->name, sizeof(manifest->name))) {
            break;
        }
        if(!xremote_device_bundle_read_string(
               format, "Profile File", manifest->profile_file, sizeof(manifest->profile_file)) ||
           !xremote_device_bundle_leaf_valid(
               manifest->profile_file, XREMOTE_DEVICE_PROFILE_EXTENSION)) {
            break;
        }
        if(!xremote_device_bundle_read_string(
               format, "IR File", manifest->ir_file, sizeof(manifest->ir_file))) {
            break;
        }
        if(manifest->ir_file[0] != '\0' &&
           !xremote_device_bundle_leaf_valid(manifest->ir_file, ".ir")) {
            break;
        }
        flipper_format_rewind(format);
        if(!flipper_format_read_uint32(format, "RF Count", &rf_count, 1U) ||
           rf_count > XREMOTE_DEVICE_RF_COMMAND_MAX) {
            break;
        }
        manifest->rf_count = (uint8_t)rf_count;
        bool commands_valid = true;
        for(uint32_t index = 0U; index < rf_count; index++) {
            char key[24];
            snprintf(key, sizeof(key), "RF%lu File", (unsigned long)index);
            if(!xremote_device_bundle_read_string(
                   format, key, manifest->rf_file[index], sizeof(manifest->rf_file[index])) ||
               !xremote_device_bundle_leaf_valid(manifest->rf_file[index], ".sub")) {
                commands_valid = false;
                break;
            }
        }
        if(!commands_valid) break;
        success = true;
    } while(false);

    furi_string_free(header);
    flipper_format_free(format);
    return success;
}

static bool xremote_device_bundle_manifest_folder(
    const char* manifest_path,
    char* folder,
    size_t folder_size) {
    if(!manifest_path || !folder || folder_size == 0U ||
       !xremote_device_bundle_has_extension(manifest_path, XREMOTE_DEVICE_BUNDLE_EXTENSION) ||
       strstr(manifest_path, "/../") || strstr(manifest_path, "//") ||
       strchr(manifest_path, '\\')) {
        return false;
    }
    const size_t root_length = strlen(XREMOTE_DEVICE_BUNDLE_FOLDER);
    if(strncmp(manifest_path, XREMOTE_DEVICE_BUNDLE_FOLDER "/", root_length + 1U) != 0) {
        return false;
    }
    const char* relative = manifest_path + root_length + 1U;
    const char* slash = strchr(relative, '/');
    if(!slash || strchr(slash + 1, '/') ||
       strcmp(slash + 1, XREMOTE_DEVICE_BUNDLE_MANIFEST_FILE) != 0) {
        return false;
    }
    const size_t folder_length = (size_t)(slash - manifest_path);
    if(folder_length == 0U || folder_length >= folder_size) return false;
    memcpy(folder, manifest_path, folder_length);
    folder[folder_length] = '\0';
    return true;
}

static XRemoteDeviceBundleStatus xremote_device_bundle_validate_sources(
    Storage* storage,
    const char* folder,
    const XRemoteDeviceBundleManifest* manifest,
    const XRemoteDeviceProfile* profile) {
    if(strcmp(manifest->name, profile->name) != 0 || manifest->rf_count != profile->rf_count ||
       (manifest->ir_file[0] == '\0') != (profile->ir_path[0] == '\0')) {
        return XRemoteDeviceBundleStatusInvalidBundle;
    }

    char path[XREMOTE_DEVICE_PATH_MAX];
    if(manifest->ir_file[0] != '\0') {
        if(!xremote_device_bundle_join(path, sizeof(path), folder, manifest->ir_file) ||
           !xremote_device_bundle_ir_valid(storage, path)) {
            return XRemoteDeviceBundleStatusInvalidSource;
        }
    }
    for(uint8_t index = 0U; index < profile->rf_count; index++) {
        if(!xremote_device_bundle_join(path, sizeof(path), folder, manifest->rf_file[index]) ||
           !xremote_device_bundle_rf_valid(storage, path, &profile->rf[index])) {
            return XRemoteDeviceBundleStatusInvalidSource;
        }
    }
    return XRemoteDeviceBundleStatusOk;
}

XRemoteDeviceBundleStatus xremote_device_bundle_export(
    Storage* storage,
    const XRemoteDeviceProfile* profile,
    char* manifest_path,
    size_t manifest_path_size) {
    if(!storage || !profile || !manifest_path || manifest_path_size == 0U ||
       profile->path[0] == '\0' || profile->name[0] == '\0') {
        return XRemoteDeviceBundleStatusInvalidProfile;
    }
    manifest_path[0] = '\0';
    if(!storage_file_exists(storage, profile->path)) {
        return XRemoteDeviceBundleStatusInvalidProfile;
    }
    if(profile->ir_path[0] != '\0' && !storage_file_exists(storage, profile->ir_path)) {
        return XRemoteDeviceBundleStatusMissingSource;
    }
    if(profile->ir_path[0] != '\0' && !xremote_device_bundle_ir_valid(storage, profile->ir_path)) {
        return XRemoteDeviceBundleStatusInvalidSource;
    }
    for(uint8_t index = 0U; index < profile->rf_count; index++) {
        if(!storage_file_exists(storage, profile->rf[index].path)) {
            return XRemoteDeviceBundleStatusMissingSource;
        }
        if(!xremote_device_bundle_rf_valid(storage, profile->rf[index].path, &profile->rf[index])) {
            return XRemoteDeviceBundleStatusInvalidSource;
        }
    }
    if(!xremote_device_profile_storage_ready(storage) ||
       !storage_simply_mkdir(storage, XREMOTE_DEVICE_BUNDLE_FOLDER)) {
        return XRemoteDeviceBundleStatusStorageError;
    }

    XRemoteDeviceBundleExportWorkspace* workspace = calloc(1U, sizeof(*workspace));
    if(!workspace) return XRemoteDeviceBundleStatusStorageError;
    if(!xremote_device_bundle_make_unique_folder(
           storage,
           XREMOTE_DEVICE_BUNDLE_FOLDER,
           profile->name,
           "",
           workspace->final_folder,
           sizeof(workspace->final_folder),
           NULL)) {
        free(workspace);
        return XRemoteDeviceBundleStatusStorageError;
    }
    const int staging_length = snprintf(
        workspace->staging_folder,
        sizeof(workspace->staging_folder),
        "%s.tmp",
        workspace->final_folder);
    if(staging_length <= 0 || (size_t)staging_length >= sizeof(workspace->staging_folder)) {
        free(workspace);
        return XRemoteDeviceBundleStatusStorageError;
    }
    storage_simply_remove_recursive(storage, workspace->staging_folder);
    if(!storage_simply_mkdir(storage, workspace->staging_folder)) {
        free(workspace);
        return XRemoteDeviceBundleStatusStorageError;
    }

    bool success = xremote_device_bundle_join(
                       workspace->destination,
                       sizeof(workspace->destination),
                       workspace->staging_folder,
                       XREMOTE_DEVICE_BUNDLE_PROFILE_FILE) &&
                   storage_common_copy(storage, profile->path, workspace->destination) == FSE_OK;
    if(success && profile->ir_path[0] != '\0') {
        success = xremote_device_bundle_join(
                      workspace->destination,
                      sizeof(workspace->destination),
                      workspace->staging_folder,
                      XREMOTE_DEVICE_BUNDLE_IR_FILE) &&
                  storage_common_copy(storage, profile->ir_path, workspace->destination) == FSE_OK;
    }
    for(uint8_t index = 0U; success && index < profile->rf_count; index++) {
        char filename[XREMOTE_DEVICE_BUNDLE_LEAF_MAX];
        snprintf(filename, sizeof(filename), "rf_%u.sub", (unsigned int)index);
        success = xremote_device_bundle_join(
                      workspace->destination,
                      sizeof(workspace->destination),
                      workspace->staging_folder,
                      filename) &&
                  storage_common_copy(storage, profile->rf[index].path, workspace->destination) ==
                      FSE_OK;
    }

    success = success &&
              xremote_device_bundle_join(
                  workspace->staging_manifest,
                  sizeof(workspace->staging_manifest),
                  workspace->staging_folder,
                  XREMOTE_DEVICE_BUNDLE_MANIFEST_FILE) &&
              xremote_device_bundle_write_manifest(storage, workspace->staging_manifest, profile);
    success = success &&
              xremote_device_bundle_read_manifest(
                  storage, workspace->staging_manifest, &workspace->manifest) &&
              strcmp(workspace->manifest.name, profile->name) == 0 &&
              workspace->manifest.rf_count == profile->rf_count;
    if(success) {
        success = storage_common_rename(
                      storage, workspace->staging_folder, workspace->final_folder) == FSE_OK;
    }
    if(!success) {
        storage_simply_remove_recursive(storage, workspace->staging_folder);
        free(workspace);
        return XRemoteDeviceBundleStatusStorageError;
    }
    if(!xremote_device_bundle_join(
           manifest_path,
           manifest_path_size,
           workspace->final_folder,
           XREMOTE_DEVICE_BUNDLE_MANIFEST_FILE)) {
        storage_simply_remove_recursive(storage, workspace->final_folder);
        free(workspace);
        return XRemoteDeviceBundleStatusStorageError;
    }
    free(workspace);
    return XRemoteDeviceBundleStatusOk;
}

XRemoteDeviceBundleStatus xremote_device_bundle_import(
    Storage* storage,
    const char* manifest_path,
    XRemoteDeviceProfile* imported_profile) {
    if(!storage || !manifest_path || !imported_profile) {
        return XRemoteDeviceBundleStatusInvalidBundle;
    }
    xremote_device_profile_reset(imported_profile);
    XRemoteDeviceBundleImportWorkspace* workspace = calloc(1U, sizeof(*workspace));
    if(!workspace) return XRemoteDeviceBundleStatusStorageError;
    if(!xremote_device_bundle_manifest_folder(
           manifest_path, workspace->bundle_folder, sizeof(workspace->bundle_folder)) ||
       !xremote_device_bundle_read_manifest(storage, manifest_path, &workspace->manifest)) {
        free(workspace);
        return XRemoteDeviceBundleStatusInvalidBundle;
    }

    if(!xremote_device_bundle_join(
           workspace->source_profile_path,
           sizeof(workspace->source_profile_path),
           workspace->bundle_folder,
           workspace->manifest.profile_file)) {
        free(workspace);
        return XRemoteDeviceBundleStatusInvalidBundle;
    }
    XRemoteDeviceProfile* source = xremote_device_profile_alloc();
    if(!source) {
        free(workspace);
        return XRemoteDeviceBundleStatusStorageError;
    }
    if(!xremote_device_profile_load(storage, workspace->source_profile_path, source)) {
        xremote_device_profile_free(source);
        free(workspace);
        return XRemoteDeviceBundleStatusInvalidProfile;
    }
    XRemoteDeviceBundleStatus status = xremote_device_bundle_validate_sources(
        storage, workspace->bundle_folder, &workspace->manifest, source);
    if(status != XRemoteDeviceBundleStatusOk) {
        xremote_device_profile_free(source);
        free(workspace);
        return status;
    }
    if(!xremote_device_profile_storage_ready(storage) ||
       !storage_simply_mkdir(storage, XREMOTE_DEVICE_IMPORT_FOLDER)) {
        xremote_device_profile_free(source);
        free(workspace);
        return XRemoteDeviceBundleStatusStorageError;
    }

    uint32_t import_sequence = 0U;
    if(!xremote_device_bundle_make_unique_folder(
           storage,
           XREMOTE_DEVICE_IMPORT_FOLDER,
           source->name,
           "_import",
           workspace->final_folder,
           sizeof(workspace->final_folder),
           &import_sequence)) {
        xremote_device_profile_free(source);
        free(workspace);
        return XRemoteDeviceBundleStatusStorageError;
    }
    const int staging_length = snprintf(
        workspace->staging_folder,
        sizeof(workspace->staging_folder),
        "%s.tmp",
        workspace->final_folder);
    if(staging_length <= 0 || (size_t)staging_length >= sizeof(workspace->staging_folder)) {
        xremote_device_profile_free(source);
        free(workspace);
        return XRemoteDeviceBundleStatusStorageError;
    }
    storage_simply_remove_recursive(storage, workspace->staging_folder);
    if(!storage_simply_mkdir(storage, workspace->staging_folder)) {
        xremote_device_profile_free(source);
        free(workspace);
        return XRemoteDeviceBundleStatusStorageError;
    }

    *imported_profile = *source;
    if(import_sequence <= 1U) {
        snprintf(
            imported_profile->name,
            sizeof(imported_profile->name),
            "%.*s Import",
            (int)(XREMOTE_DEVICE_NAME_MAX - 7U),
            source->name);
    } else {
        const uint8_t display_sequence = import_sequence > UINT8_MAX ? UINT8_MAX :
                                                                       (uint8_t)import_sequence;
        snprintf(
            imported_profile->name,
            sizeof(imported_profile->name),
            "%.*s Import %u",
            (int)(XREMOTE_DEVICE_NAME_MAX - 11U),
            source->name,
            (unsigned int)display_sequence);
    }
    imported_profile->path[0] = '\0';

    bool success = true;
    if(workspace->manifest.ir_file[0] != '\0') {
        success = xremote_device_bundle_join(
                      workspace->source_path,
                      sizeof(workspace->source_path),
                      workspace->bundle_folder,
                      workspace->manifest.ir_file) &&
                  xremote_device_bundle_join(
                      workspace->staging_path,
                      sizeof(workspace->staging_path),
                      workspace->staging_folder,
                      workspace->manifest.ir_file) &&
                  xremote_device_bundle_join(
                      workspace->final_path,
                      sizeof(workspace->final_path),
                      workspace->final_folder,
                      workspace->manifest.ir_file) &&
                  storage_common_copy(storage, workspace->source_path, workspace->staging_path) ==
                      FSE_OK;
        if(success) {
            xremote_device_bundle_copy(
                imported_profile->ir_path,
                sizeof(imported_profile->ir_path),
                workspace->final_path);
        }
    }
    for(uint8_t index = 0U; success && index < source->rf_count; index++) {
        success = xremote_device_bundle_join(
                      workspace->source_path,
                      sizeof(workspace->source_path),
                      workspace->bundle_folder,
                      workspace->manifest.rf_file[index]) &&
                  xremote_device_bundle_join(
                      workspace->staging_path,
                      sizeof(workspace->staging_path),
                      workspace->staging_folder,
                      workspace->manifest.rf_file[index]) &&
                  xremote_device_bundle_join(
                      workspace->final_path,
                      sizeof(workspace->final_path),
                      workspace->final_folder,
                      workspace->manifest.rf_file[index]) &&
                  storage_common_copy(storage, workspace->source_path, workspace->staging_path) ==
                      FSE_OK;
        if(success) {
            xremote_device_bundle_copy(
                imported_profile->rf[index].path,
                sizeof(imported_profile->rf[index].path),
                workspace->final_path);
        }
    }

    success = success &&
              xremote_device_profile_create_path(
                  storage,
                  imported_profile->name,
                  workspace->installed_profile_path,
                  sizeof(workspace->installed_profile_path)) &&
              xremote_device_bundle_join(
                  workspace->staged_profile_path,
                  sizeof(workspace->staged_profile_path),
                  workspace->staging_folder,
                  XREMOTE_DEVICE_BUNDLE_PROFILE_FILE);
    if(success) {
        xremote_device_bundle_copy(
            imported_profile->path,
            sizeof(imported_profile->path),
            workspace->staged_profile_path);
        success = xremote_device_profile_store(storage, imported_profile);
    }
    if(success) {
        success = storage_common_rename(
                      storage, workspace->staging_folder, workspace->final_folder) == FSE_OK;
    }
    if(success) {
        success =
            xremote_device_bundle_join(
                workspace->bundled_profile_path,
                sizeof(workspace->bundled_profile_path),
                workspace->final_folder,
                XREMOTE_DEVICE_BUNDLE_PROFILE_FILE) &&
            storage_common_rename(
                storage, workspace->bundled_profile_path, workspace->installed_profile_path) ==
                FSE_OK;
    }
    if(!success) {
        storage_simply_remove_recursive(storage, workspace->staging_folder);
        storage_simply_remove_recursive(storage, workspace->final_folder);
        xremote_device_profile_reset(imported_profile);
        xremote_device_profile_free(source);
        free(workspace);
        return XRemoteDeviceBundleStatusStorageError;
    }

    xremote_device_bundle_copy(
        imported_profile->path, sizeof(imported_profile->path), workspace->installed_profile_path);
    const bool verified =
        xremote_device_profile_load(storage, workspace->installed_profile_path, imported_profile);
    if(!verified) {
        xremote_device_profile_delete_path(storage, workspace->installed_profile_path);
        storage_simply_remove_recursive(storage, workspace->final_folder);
        xremote_device_profile_reset(imported_profile);
        status = XRemoteDeviceBundleStatusStorageError;
    }
    xremote_device_profile_free(source);
    free(workspace);
    return verified ? XRemoteDeviceBundleStatusOk : status;
}

const char* xremote_device_bundle_status_name(XRemoteDeviceBundleStatus status) {
    switch(status) {
    case XRemoteDeviceBundleStatusOk:
        return "Bundle ready";
    case XRemoteDeviceBundleStatusInvalidProfile:
        return "Profile is invalid";
    case XRemoteDeviceBundleStatusMissingSource:
        return "A source is missing";
    case XRemoteDeviceBundleStatusInvalidSource:
        return "Source failed validation";
    case XRemoteDeviceBundleStatusInvalidBundle:
        return "Bundle is invalid";
    case XRemoteDeviceBundleStatusStorageError:
    default:
        return "Storage operation failed";
    }
}
