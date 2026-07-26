#include "xremote_device_profile.h"

#include <flipper_format/flipper_format.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static void xremote_device_copy(char* output, size_t output_size, const char* value) {
    if(output_size == 0U) return;
    snprintf(output, output_size, "%s", value ? value : "");
}

static bool xremote_device_name_valid(const char* name) {
    if(!name) return false;
    for(size_t index = 0U; name[index] != '\0'; index++) {
        if(!isspace((unsigned char)name[index])) return true;
    }
    return false;
}

static bool xremote_device_read_string(
    FlipperFormat* format,
    const char* key,
    char* output,
    size_t output_size) {
    FuriString* value = furi_string_alloc();
    const bool success = flipper_format_read_string(format, key, value);
    if(success) xremote_device_copy(output, output_size, furi_string_get_cstr(value));
    furi_string_free(value);
    return success;
}

bool xremote_device_profile_storage_ready(Storage* storage) {
    return storage && storage_simply_mkdir(storage, XREMOTE_DEVICE_PROFILE_ROOT) &&
           storage_simply_mkdir(storage, XREMOTE_DEVICE_PROFILE_FOLDER);
}

static bool xremote_device_profile_verify_string(
    FlipperFormat* format,
    const char* key,
    const char* expected,
    char* buffer,
    size_t buffer_size) {
    flipper_format_rewind(format);
    return xremote_device_read_string(format, key, buffer, buffer_size) &&
           strcmp(buffer, expected) == 0;
}

static bool xremote_device_profile_verify(Storage* storage, const XRemoteDeviceProfile* profile) {
    FlipperFormat* format = flipper_format_buffered_file_alloc(storage);
    FuriString* header = furi_string_alloc();
    char key[24];
    char value[XREMOTE_DEVICE_PATH_MAX];
    uint32_t version = 0U;
    uint32_t rf_count = 0U;
    bool success = false;

    do {
        if(!flipper_format_buffered_file_open_existing(format, profile->path)) break;
        if(!flipper_format_read_header(format, header, &version)) break;
        if(strcmp(furi_string_get_cstr(header), XREMOTE_DEVICE_PROFILE_HEADER) != 0 ||
           version != XREMOTE_DEVICE_PROFILE_VERSION) {
            break;
        }
        if(!xremote_device_profile_verify_string(
               format, "Name", profile->name, value, sizeof(value))) {
            break;
        }
        if(!xremote_device_profile_verify_string(
               format, "IR File", profile->ir_path, value, sizeof(value))) {
            break;
        }
        if(!xremote_device_profile_verify_string(
               format, "Radio", xremote_device_radio_name(profile->radio), value, sizeof(value))) {
            break;
        }
        if(!flipper_format_read_uint32(format, "RF Count", &rf_count, 1U) ||
           rf_count != profile->rf_count) {
            break;
        }

        bool commands_valid = true;
        for(uint32_t index = 0U; index < profile->rf_count; index++) {
            const XRemoteDeviceRfCommand* command = &profile->rf[index];
            snprintf(key, sizeof(key), "RF%lu Name", (unsigned long)index);
            if(!xremote_device_profile_verify_string(
                   format, key, command->name, value, sizeof(value))) {
                commands_valid = false;
                break;
            }
            snprintf(key, sizeof(key), "RF%lu File", (unsigned long)index);
            if(!xremote_device_profile_verify_string(
                   format, key, command->path, value, sizeof(value))) {
                commands_valid = false;
                break;
            }
            snprintf(key, sizeof(key), "RF%lu Protocol", (unsigned long)index);
            if(!xremote_device_profile_verify_string(
                   format, key, command->protocol, value, sizeof(value))) {
                commands_valid = false;
                break;
            }
            snprintf(key, sizeof(key), "RF%lu Adapter", (unsigned long)index);
            if(!xremote_device_profile_verify_string(
                   format,
                   key,
                   xremote_device_adapter_name(command->adapter),
                   value,
                   sizeof(value))) {
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

static void xremote_device_safe_name(const char* source, char* output, size_t output_size) {
    size_t written = 0U;
    if(output_size == 0U) return;
    for(size_t i = 0U; source && source[i] != '\0' && written + 1U < output_size; i++) {
        const unsigned char value = (unsigned char)source[i];
        if(isalnum(value) || value == '-' || value == '_') {
            output[written++] = (char)value;
        } else if(value == ' ' && written > 0U && output[written - 1U] != '_') {
            output[written++] = '_';
        }
    }
    if(written == 0U) {
        xremote_device_copy(output, output_size, "Device");
    } else {
        output[written] = '\0';
    }
}

XRemoteDeviceProfile* xremote_device_profile_alloc(void) {
    XRemoteDeviceProfile* profile = calloc(1U, sizeof(*profile));
    if(profile) profile->radio = XRemoteDeviceRadioInternal;
    return profile;
}

void xremote_device_profile_free(XRemoteDeviceProfile* profile) {
    free(profile);
}

void xremote_device_profile_reset(XRemoteDeviceProfile* profile) {
    if(!profile) return;
    memset(profile, 0, sizeof(*profile));
    profile->radio = XRemoteDeviceRadioInternal;
}

const char* xremote_device_radio_name(XRemoteDeviceRadio radio) {
    return radio == XRemoteDeviceRadioExternal ? "External" : "Internal";
}

const char* xremote_device_adapter_name(XRemoteDeviceAdapter adapter) {
    return adapter == XRemoteDeviceAdapterPrinceton4 ? "Princeton4" : "ReplayOnly";
}

XRemoteDeviceAdapter xremote_device_adapter_from_name(const char* name) {
    if(name && strcmp(name, "Princeton4") == 0) return XRemoteDeviceAdapterPrinceton4;
    return XRemoteDeviceAdapterReplayOnly;
}

bool xremote_device_profile_load(Storage* storage, const char* path, XRemoteDeviceProfile* profile) {
    if(!storage || !path || !profile) return false;

    FlipperFormat* format = flipper_format_buffered_file_alloc(storage);
    FuriString* header = furi_string_alloc();
    FuriString* value = furi_string_alloc();
    uint32_t version = 0U;
    uint32_t rf_count = 0U;
    bool success = false;

    xremote_device_profile_reset(profile);
    do {
        if(!flipper_format_buffered_file_open_existing(format, path)) break;
        if(!flipper_format_read_header(format, header, &version)) break;
        if(strcmp(furi_string_get_cstr(header), XREMOTE_DEVICE_PROFILE_HEADER) != 0 ||
           version != XREMOTE_DEVICE_PROFILE_VERSION) {
            break;
        }
        if(!xremote_device_read_string(format, "Name", profile->name, sizeof(profile->name))) {
            break;
        }
        flipper_format_rewind(format);
        xremote_device_read_string(format, "IR File", profile->ir_path, sizeof(profile->ir_path));
        flipper_format_rewind(format);
        if(flipper_format_read_string(format, "Radio", value) &&
           strcmp(furi_string_get_cstr(value), "External") == 0) {
            profile->radio = XRemoteDeviceRadioExternal;
        }
        flipper_format_rewind(format);
        if(!flipper_format_read_uint32(format, "RF Count", &rf_count, 1U) ||
           rf_count > XREMOTE_DEVICE_RF_COMMAND_MAX) {
            break;
        }

        for(uint32_t index = 0U; index < rf_count; index++) {
            char key[24];
            char adapter_name[24];
            XRemoteDeviceRfCommand* command = &profile->rf[index];

            snprintf(key, sizeof(key), "RF%lu Name", (unsigned long)index);
            flipper_format_rewind(format);
            if(!xremote_device_read_string(format, key, command->name, sizeof(command->name))) {
                break;
            }

            snprintf(key, sizeof(key), "RF%lu File", (unsigned long)index);
            flipper_format_rewind(format);
            if(!xremote_device_read_string(format, key, command->path, sizeof(command->path))) {
                break;
            }

            snprintf(key, sizeof(key), "RF%lu Protocol", (unsigned long)index);
            flipper_format_rewind(format);
            if(!xremote_device_read_string(
                   format, key, command->protocol, sizeof(command->protocol))) {
                break;
            }

            snprintf(key, sizeof(key), "RF%lu Adapter", (unsigned long)index);
            flipper_format_rewind(format);
            if(xremote_device_read_string(format, key, adapter_name, sizeof(adapter_name))) {
                command->adapter = xremote_device_adapter_from_name(adapter_name);
            }
            profile->rf_count++;
        }
        if(profile->rf_count != rf_count) break;
        if(profile->ir_path[0] == '\0' && profile->rf_count == 0U) break;

        xremote_device_copy(profile->path, sizeof(profile->path), path);
        success = true;
    } while(false);

    furi_string_free(value);
    furi_string_free(header);
    flipper_format_free(format);
    if(!success) xremote_device_profile_reset(profile);
    return success;
}

bool xremote_device_profile_store(Storage* storage, const XRemoteDeviceProfile* profile) {
    if(!storage || !profile || profile->path[0] == '\0' || profile->name[0] == '\0' ||
       (profile->ir_path[0] == '\0' && profile->rf_count == 0U) ||
       profile->rf_count > XREMOTE_DEVICE_RF_COMMAND_MAX) {
        return false;
    }

    if(!xremote_device_profile_storage_ready(storage)) return false;
    char temporary[XREMOTE_DEVICE_PATH_MAX + 5U];
    char backup[XREMOTE_DEVICE_PATH_MAX + 5U];
    const int temporary_length = snprintf(temporary, sizeof(temporary), "%s.tmp", profile->path);
    const int backup_length = snprintf(backup, sizeof(backup), "%s.bak", profile->path);
    if(temporary_length <= 0 || (size_t)temporary_length >= sizeof(temporary) ||
       backup_length <= 0 || (size_t)backup_length >= sizeof(backup)) {
        return false;
    }

    storage_common_remove(storage, temporary);
    FlipperFormat* format = flipper_format_file_alloc(storage);
    bool success = false;
    do {
        if(!flipper_format_file_open_always(format, temporary)) break;
        if(!flipper_format_write_header_cstr(
               format, XREMOTE_DEVICE_PROFILE_HEADER, XREMOTE_DEVICE_PROFILE_VERSION)) {
            break;
        }
        if(!flipper_format_write_string_cstr(format, "Name", profile->name)) break;
        if(!flipper_format_write_string_cstr(format, "IR File", profile->ir_path)) break;
        if(!flipper_format_write_string_cstr(
               format, "Radio", xremote_device_radio_name(profile->radio))) {
            break;
        }
        const uint32_t rf_count = profile->rf_count;
        if(!flipper_format_write_uint32(format, "RF Count", &rf_count, 1U)) break;

        bool commands_written = true;
        for(uint32_t index = 0U; index < profile->rf_count; index++) {
            char key[24];
            const XRemoteDeviceRfCommand* command = &profile->rf[index];
            snprintf(key, sizeof(key), "RF%lu Name", (unsigned long)index);
            if(!flipper_format_write_string_cstr(format, key, command->name)) {
                commands_written = false;
                break;
            }
            snprintf(key, sizeof(key), "RF%lu File", (unsigned long)index);
            if(!flipper_format_write_string_cstr(format, key, command->path)) {
                commands_written = false;
                break;
            }
            snprintf(key, sizeof(key), "RF%lu Protocol", (unsigned long)index);
            if(!flipper_format_write_string_cstr(format, key, command->protocol)) {
                commands_written = false;
                break;
            }
            snprintf(key, sizeof(key), "RF%lu Adapter", (unsigned long)index);
            if(!flipper_format_write_string_cstr(
                   format, key, xremote_device_adapter_name(command->adapter))) {
                commands_written = false;
                break;
            }
        }
        if(!commands_written) break;
        success = flipper_format_file_close(format);
    } while(false);

    flipper_format_free(format);
    if(!success) {
        storage_common_remove(storage, temporary);
        return false;
    }

    storage_common_remove(storage, backup);
    const bool had_profile = storage_file_exists(storage, profile->path);
    if(had_profile && storage_common_rename(storage, profile->path, backup) != FSE_OK) {
        storage_common_remove(storage, temporary);
        return false;
    }
    if(storage_common_rename(storage, temporary, profile->path) != FSE_OK) {
        storage_common_remove(storage, temporary);
        if(had_profile) storage_common_rename(storage, backup, profile->path);
        return false;
    }
    if(!xremote_device_profile_verify(storage, profile)) {
        storage_common_remove(storage, profile->path);
        if(had_profile) storage_common_rename(storage, backup, profile->path);
        return false;
    }
    storage_common_remove(storage, backup);
    return true;
}

bool xremote_device_profile_create_path(
    Storage* storage,
    const char* preferred_name,
    char* output,
    size_t output_size) {
    if(!storage || !output || output_size == 0U) return false;

    char safe_name[XREMOTE_DEVICE_NAME_MAX + 1U];
    xremote_device_safe_name(preferred_name, safe_name, sizeof(safe_name));
    if(!xremote_device_profile_storage_ready(storage)) {
        output[0] = '\0';
        return false;
    }
    for(uint32_t suffix = 0U; suffix < 100U; suffix++) {
        int written = 0;
        if(suffix == 0U) {
            written = snprintf(
                output,
                output_size,
                "%s/%s%s",
                XREMOTE_DEVICE_PROFILE_FOLDER,
                safe_name,
                XREMOTE_DEVICE_PROFILE_EXTENSION);
        } else {
            written = snprintf(
                output,
                output_size,
                "%s/%s_%lu%s",
                XREMOTE_DEVICE_PROFILE_FOLDER,
                safe_name,
                (unsigned long)(suffix + 1U),
                XREMOTE_DEVICE_PROFILE_EXTENSION);
        }
        if(written <= 0 || (size_t)written >= output_size) {
            output[0] = '\0';
            return false;
        }
        if(!storage_file_exists(storage, output)) return true;
    }
    output[0] = '\0';
    return false;
}

bool xremote_device_profile_duplicate(
    Storage* storage,
    const XRemoteDeviceProfile* source,
    XRemoteDeviceProfile* duplicate) {
    if(!storage || !source || !duplicate || source->path[0] == '\0' || source->name[0] == '\0') {
        return false;
    }

    *duplicate = *source;
    snprintf(
        duplicate->name,
        sizeof(duplicate->name),
        "%.*s Copy",
        (int)(XREMOTE_DEVICE_NAME_MAX - 5U),
        source->name);
    duplicate->path[0] = '\0';
    if(!xremote_device_profile_create_path(
           storage, duplicate->name, duplicate->path, sizeof(duplicate->path))) {
        xremote_device_profile_reset(duplicate);
        return false;
    }
    if(!xremote_device_profile_store(storage, duplicate)) {
        xremote_device_profile_reset(duplicate);
        return false;
    }
    return true;
}

bool xremote_device_profile_add_rf(
    XRemoteDeviceProfile* profile,
    const char* name,
    const char* path,
    const char* protocol,
    XRemoteDeviceAdapter adapter) {
    if(!profile || !name || !path || !protocol ||
       profile->rf_count >= XREMOTE_DEVICE_RF_COMMAND_MAX) {
        return false;
    }

    XRemoteDeviceRfCommand* command = &profile->rf[profile->rf_count];
    xremote_device_copy(command->name, sizeof(command->name), name);
    xremote_device_copy(command->path, sizeof(command->path), path);
    xremote_device_copy(command->protocol, sizeof(command->protocol), protocol);
    command->adapter = adapter;
    profile->rf_count++;
    return true;
}

bool xremote_device_profile_rename(XRemoteDeviceProfile* profile, const char* name) {
    if(!profile || !xremote_device_name_valid(name)) return false;
    xremote_device_copy(profile->name, sizeof(profile->name), name);
    return true;
}

bool xremote_device_profile_set_ir(XRemoteDeviceProfile* profile, const char* path) {
    if(!profile || !path) return false;
    if(path[0] == '\0' && profile->rf_count == 0U) return false;
    xremote_device_copy(profile->ir_path, sizeof(profile->ir_path), path);
    return true;
}

bool xremote_device_profile_rename_rf(
    XRemoteDeviceProfile* profile,
    uint8_t index,
    const char* name) {
    if(!profile || index >= profile->rf_count || !xremote_device_name_valid(name)) return false;
    xremote_device_copy(profile->rf[index].name, sizeof(profile->rf[index].name), name);
    return true;
}

bool xremote_device_profile_replace_rf_source(
    XRemoteDeviceProfile* profile,
    uint8_t index,
    const char* path,
    const char* protocol,
    XRemoteDeviceAdapter adapter) {
    if(!profile || index >= profile->rf_count || !path || path[0] == '\0' || !protocol ||
       protocol[0] == '\0') {
        return false;
    }

    XRemoteDeviceRfCommand* command = &profile->rf[index];
    xremote_device_copy(command->path, sizeof(command->path), path);
    xremote_device_copy(command->protocol, sizeof(command->protocol), protocol);
    command->adapter = adapter;
    return true;
}

bool xremote_device_profile_move_rf(XRemoteDeviceProfile* profile, uint8_t from, uint8_t to) {
    if(!profile || from >= profile->rf_count || to >= profile->rf_count) return false;
    if(from == to) return true;

    XRemoteDeviceRfCommand moving = profile->rf[from];
    if(from < to) {
        memmove(
            &profile->rf[from],
            &profile->rf[from + 1U],
            (size_t)(to - from) * sizeof(profile->rf[0]));
    } else {
        memmove(
            &profile->rf[to + 1U], &profile->rf[to], (size_t)(from - to) * sizeof(profile->rf[0]));
    }
    profile->rf[to] = moving;
    return true;
}

bool xremote_device_profile_remove_rf(XRemoteDeviceProfile* profile, uint8_t index) {
    if(!profile || index >= profile->rf_count) return false;
    if(profile->ir_path[0] == '\0' && profile->rf_count == 1U) return false;

    if(index + 1U < profile->rf_count) {
        memmove(
            &profile->rf[index],
            &profile->rf[index + 1U],
            (size_t)(profile->rf_count - index - 1U) * sizeof(profile->rf[0]));
    }
    profile->rf_count--;
    memset(&profile->rf[profile->rf_count], 0, sizeof(profile->rf[0]));
    return true;
}

bool xremote_device_profile_delete(Storage* storage, const XRemoteDeviceProfile* profile) {
    return profile && xremote_device_profile_delete_path(storage, profile->path);
}

bool xremote_device_profile_delete_path(Storage* storage, const char* path) {
    if(!storage || !path || path[0] == '\0') return false;

    const size_t folder_length = strlen(XREMOTE_DEVICE_PROFILE_FOLDER);
    const size_t path_length = strlen(path);
    const size_t extension_length = strlen(XREMOTE_DEVICE_PROFILE_EXTENSION);
    if(path_length <= folder_length + 1U + extension_length ||
       strncmp(path, XREMOTE_DEVICE_PROFILE_FOLDER "/", folder_length + 1U) != 0) {
        return false;
    }

    const char* filename = path + folder_length + 1U;
    if(filename[0] == '\0' || strchr(filename, '/') != NULL ||
       strcmp(path + path_length - extension_length, XREMOTE_DEVICE_PROFILE_EXTENSION) != 0) {
        return false;
    }
    return storage_simply_remove(storage, path);
}
