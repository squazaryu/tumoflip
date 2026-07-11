#include "tumocard_package.h"

#include <flipper_format/flipper_format.h>

#define TUMOCARD_MANIFEST_FILETYPE "TumoCard Applet"
#define TUMOCARD_MANIFEST_VERSION  1U
#define TUMOCARD_PROGRAM_FILETYPE  "TumoVM Program"
#define TUMOCARD_PROGRAM_VERSION   1U
#define TUMOCARD_STATE_FILETYPE    "TumoCard State"
#define TUMOCARD_STATE_VERSION     1U
#define TUMOCARD_SETTINGS_FILETYPE "TumoCard Settings"
#define TUMOCARD_SETTINGS_VERSION  1U

static const uint8_t tumocard_routes[] = {
    0x00,
    0xA4,
    0x00,
    0x00,
    0xB0,
    0x07,
    0x00,
    0xD6,
    0x0C,
};

static const uint8_t tumocard_bytecode[] = {
    TumoVmOpcodeCheckP1,
    0x04,
    TumoVmOpcodeCheckDataAid,
    TumoVmOpcodeSetSelected,
    TumoVmOpcodeReturnStatus,
    0x90,
    0x00,
    TumoVmOpcodeRequireSelected,
    TumoVmOpcodeReadState,
    TumoVmOpcodeReturnStatus,
    0x90,
    0x00,
    TumoVmOpcodeRequireSelected,
    TumoVmOpcodeWriteState,
    TumoVmOpcodeReturnStatus,
    0x90,
    0x00,
};

static bool
    tumocard_path(char output[TUMOCARD_PATH_MAX], const char* directory, const char* name) {
    const int result = snprintf(output, TUMOCARD_PATH_MAX, "%s/%s", directory, name);
    return result > 0 && result < (int)TUMOCARD_PATH_MAX;
}

static bool tumocard_make_dir(Storage* storage, const char* path) {
    return storage_simply_mkdir(storage, path) == FSE_OK || storage_dir_exists(storage, path);
}

static void tumocard_mark_invalid(TumoCardRegistry* registry) {
    if(registry->invalid_count < UINT8_MAX) registry->invalid_count++;
}

static bool tumocard_write_manifest(
    Storage* storage,
    const char* directory,
    const char* name,
    const uint8_t* aid,
    size_t aid_size,
    const uint8_t* initial_state,
    size_t state_size) {
    char path[TUMOCARD_PATH_MAX];
    if(!tumocard_path(path, directory, "manifest.tca")) return false;

    FlipperFormat* format = flipper_format_file_alloc(storage);
    bool success = false;
    do {
        if(!flipper_format_file_open_always(format, path)) break;
        if(!flipper_format_write_header_cstr(
               format, TUMOCARD_MANIFEST_FILETYPE, TUMOCARD_MANIFEST_VERSION))
            break;
        if(!flipper_format_write_string_cstr(format, "Name", name)) break;
        const uint32_t aid_length = aid_size;
        const uint32_t state_length = state_size;
        const uint32_t crypto_mask = 0U;
        const bool capability = true;
        if(!flipper_format_write_uint32(format, "AID size", &aid_length, 1U)) break;
        if(!flipper_format_write_hex(format, "AID", aid, aid_size)) break;
        if(!flipper_format_write_uint32(format, "State size", &state_length, 1U)) break;
        if(!flipper_format_write_hex(format, "Initial state", initial_state, state_size)) break;
        if(!flipper_format_write_bool(format, "Capability NFC Type4", &capability, 1U)) break;
        if(!flipper_format_write_bool(format, "Capability USB CCID", &capability, 1U)) break;
        if(!flipper_format_write_uint32(format, "Crypto mask", &crypto_mask, 1U)) break;
        success = true;
    } while(false);
    flipper_format_free(format);
    return success;
}

static bool tumocard_write_program(Storage* storage, const char* directory) {
    char path[TUMOCARD_PATH_MAX];
    if(!tumocard_path(path, directory, "program.tvm")) return false;

    FlipperFormat* format = flipper_format_file_alloc(storage);
    bool success = false;
    do {
        if(!flipper_format_file_open_always(format, path)) break;
        if(!flipper_format_write_header_cstr(
               format, TUMOCARD_PROGRAM_FILETYPE, TUMOCARD_PROGRAM_VERSION))
            break;
        const uint32_t route_count = sizeof(tumocard_routes) / 3U;
        const uint32_t bytecode_size = sizeof(tumocard_bytecode);
        if(!flipper_format_write_uint32(format, "Route count", &route_count, 1U)) break;
        if(!flipper_format_write_hex(format, "Routes", tumocard_routes, sizeof(tumocard_routes)))
            break;
        if(!flipper_format_write_uint32(format, "Bytecode size", &bytecode_size, 1U)) break;
        if(!flipper_format_write_hex(
               format, "Bytecode", tumocard_bytecode, sizeof(tumocard_bytecode)))
            break;
        success = true;
    } while(false);
    flipper_format_free(format);
    return success;
}

static bool tumocard_write_settings(Storage* storage, const char* path, bool enabled) {
    FlipperFormat* format = flipper_format_file_alloc(storage);
    bool success = false;
    do {
        if(!flipper_format_file_open_always(format, path)) break;
        if(!flipper_format_write_header_cstr(
               format, TUMOCARD_SETTINGS_FILETYPE, TUMOCARD_SETTINGS_VERSION))
            break;
        if(!flipper_format_write_bool(format, "Enabled", &enabled, 1U)) break;
        success = true;
    } while(false);
    flipper_format_free(format);
    return success;
}

static bool tumocard_create_reference(
    Storage* storage,
    const char* id,
    const char* name,
    uint8_t aid_suffix,
    const uint8_t* state,
    size_t state_size) {
    char directory[TUMOCARD_PATH_MAX];
    if(!tumocard_path(directory, TUMOCARD_APPLETS_DIR, id) ||
       !tumocard_make_dir(storage, directory))
        return false;

    const uint8_t aid[] = {0xF0, 0x54, 0x43, 0x41, 0x52, 0x44, aid_suffix};
    char settings_path[TUMOCARD_PATH_MAX];
    if(!tumocard_path(settings_path, directory, "settings.tcs")) return false;
    return tumocard_write_manifest(
               storage, directory, name, aid, sizeof(aid), state, state_size) &&
           tumocard_write_program(storage, directory) &&
           tumocard_write_settings(storage, settings_path, true);
}

static bool tumocard_has_applet_directory(Storage* storage) {
    File* directory = storage_file_alloc(storage);
    bool found = false;
    if(storage_dir_open(directory, TUMOCARD_APPLETS_DIR)) {
        FileInfo info;
        char name[TUMOCARD_APPLET_ID_MAX];
        while(storage_dir_read(directory, &info, name, sizeof(name))) {
            if(file_info_is_dir(&info)) {
                found = true;
                break;
            }
        }
        storage_dir_close(directory);
    }
    storage_file_free(directory);
    return found;
}

static bool tumocard_create_references(Storage* storage) {
    static const uint8_t counter_state[] = {'C', 'O', 'U', 'N', 'T', 0x00, 0x00, 0x00};
    static const uint8_t notes_state[] = {
        'N', 'O', 'T', 'E', ':', 'R', 'E', 'A', 'D', 'Y', 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    return tumocard_create_reference(
               storage, "counter", "Counter Token", 0x01, counter_state, sizeof(counter_state)) &&
           tumocard_create_reference(
               storage, "notes", "Notes Token", 0x02, notes_state, sizeof(notes_state));
}

static bool tumocard_read_header(
    FlipperFormat* format,
    FuriString* header,
    const char* expected_type,
    uint32_t expected_version) {
    uint32_t version = 0U;
    return flipper_format_read_header(format, header, &version) && version == expected_version &&
           furi_string_cmp_str(header, expected_type) == 0;
}

static bool tumocard_recover_atomic(
    Storage* storage,
    const char* directory,
    const char* current_name,
    const char* temporary_name,
    const char* backup_name) {
    char current[TUMOCARD_PATH_MAX];
    char temporary[TUMOCARD_PATH_MAX];
    char backup[TUMOCARD_PATH_MAX];
    if(!tumocard_path(current, directory, current_name) ||
       !tumocard_path(temporary, directory, temporary_name) ||
       !tumocard_path(backup, directory, backup_name))
        return false;

    if(storage_file_exists(storage, current)) {
        storage_common_remove(storage, temporary);
        storage_common_remove(storage, backup);
        return true;
    }
    if(storage_file_exists(storage, temporary) &&
       storage_common_rename(storage, temporary, current) == FSE_OK) {
        storage_common_remove(storage, backup);
        return true;
    }
    storage_common_remove(storage, temporary);
    if(storage_file_exists(storage, backup))
        return storage_common_rename(storage, backup, current) == FSE_OK;
    return true;
}

static bool tumocard_read_applet(
    Storage* storage,
    const char* directory,
    TumoVmProgram* program,
    uint8_t initial_state[TUMOVM_STATE_MAX],
    bool* enabled) {
    char manifest_path[TUMOCARD_PATH_MAX];
    char program_path[TUMOCARD_PATH_MAX];
    char settings_path[TUMOCARD_PATH_MAX];
    if(!tumocard_path(manifest_path, directory, "manifest.tca") ||
       !tumocard_path(program_path, directory, "program.tvm") ||
       !tumocard_path(settings_path, directory, "settings.tcs"))
        return false;

    memset(program, 0, sizeof(*program));
    memset(initial_state, 0, TUMOVM_STATE_MAX);
    *enabled = true;

    if(!tumocard_recover_atomic(storage, directory, "settings.tcs", "settings.tmp", "settings.bak"))
        return false;

    FlipperFormat* format = flipper_format_file_alloc(storage);
    FuriString* header = furi_string_alloc();
    FuriString* name = furi_string_alloc();
    bool success = false;
    do {
        if(!flipper_format_file_open_existing(format, manifest_path)) break;
        if(!tumocard_read_header(
               format, header, TUMOCARD_MANIFEST_FILETYPE, TUMOCARD_MANIFEST_VERSION))
            break;
        uint32_t aid_size = 0U;
        uint32_t state_size = 0U;
        uint32_t crypto_mask = 0U;
        if(!flipper_format_read_string(format, "Name", name)) break;
        if(furi_string_size(name) == 0U || furi_string_size(name) >= TUMOVM_NAME_MAX) break;
        if(!flipper_format_read_uint32(format, "AID size", &aid_size, 1U) || aid_size == 0U ||
           aid_size > TUMOVM_AID_MAX)
            break;
        if(!flipper_format_read_hex(format, "AID", program->aid, aid_size)) break;
        if(!flipper_format_read_uint32(format, "State size", &state_size, 1U) ||
           state_size == 0U || state_size > TUMOVM_STATE_MAX)
            break;
        if(!flipper_format_read_hex(format, "Initial state", initial_state, state_size)) break;
        if(!flipper_format_read_bool(
               format, "Capability NFC Type4", &program->capability_nfc_type4, 1U))
            break;
        if(!flipper_format_read_bool(
               format, "Capability USB CCID", &program->capability_usb_ccid, 1U))
            break;
        if(!flipper_format_read_uint32(format, "Crypto mask", &crypto_mask, 1U) ||
           crypto_mask != 0U)
            break;
        strlcpy(program->name, furi_string_get_cstr(name), sizeof(program->name));
        program->aid_size = aid_size;
        program->state_size = state_size;
        flipper_format_file_close(format);

        furi_string_reset(header);
        if(!flipper_format_file_open_existing(format, program_path)) break;
        if(!tumocard_read_header(
               format, header, TUMOCARD_PROGRAM_FILETYPE, TUMOCARD_PROGRAM_VERSION))
            break;
        uint32_t route_count = 0U;
        uint32_t bytecode_size = 0U;
        uint8_t routes[TUMOVM_ROUTES_MAX * 3U] = {0};
        if(!flipper_format_read_uint32(format, "Route count", &route_count, 1U) ||
           route_count == 0U || route_count > TUMOVM_ROUTES_MAX)
            break;
        if(!flipper_format_read_hex(format, "Routes", routes, route_count * 3U)) break;
        if(!flipper_format_read_uint32(format, "Bytecode size", &bytecode_size, 1U) ||
           bytecode_size == 0U || bytecode_size > TUMOVM_BYTECODE_MAX)
            break;
        if(!flipper_format_read_hex(format, "Bytecode", program->bytecode, bytecode_size)) break;
        program->route_count = route_count;
        program->bytecode_size = bytecode_size;
        for(size_t i = 0; i < route_count; i++) {
            program->routes[i].cla = routes[i * 3U];
            program->routes[i].ins = routes[i * 3U + 1U];
            program->routes[i].entry = routes[i * 3U + 2U];
        }
        flipper_format_file_close(format);

        if(storage_file_exists(storage, settings_path)) {
            furi_string_reset(header);
            if(!flipper_format_file_open_existing(format, settings_path)) break;
            if(!tumocard_read_header(
                   format, header, TUMOCARD_SETTINGS_FILETYPE, TUMOCARD_SETTINGS_VERSION))
                break;
            if(!flipper_format_read_bool(format, "Enabled", enabled, 1U)) break;
        }
        success = tumovm_program_validate(program);
    } while(false);

    furi_string_free(name);
    furi_string_free(header);
    flipper_format_free(format);
    return success;
}

static bool tumocard_read_state(
    Storage* storage,
    const char* directory,
    const TumoVmProgram* program,
    uint8_t state[TUMOVM_STATE_MAX]) {
    char path[TUMOCARD_PATH_MAX];
    if(!tumocard_path(path, directory, "state.tvs")) return false;

    FlipperFormat* format = flipper_format_file_alloc(storage);
    FuriString* header = furi_string_alloc();
    bool success = false;
    do {
        if(!flipper_format_file_open_existing(format, path)) break;
        if(!tumocard_read_header(format, header, TUMOCARD_STATE_FILETYPE, TUMOCARD_STATE_VERSION))
            break;
        uint32_t state_size = 0U;
        if(!flipper_format_read_uint32(format, "State size", &state_size, 1U) ||
           state_size != program->state_size)
            break;
        if(!flipper_format_read_hex(format, "Data", state, state_size)) break;
        success = true;
    } while(false);
    furi_string_free(header);
    flipper_format_free(format);
    return success;
}

static bool tumocard_atomic_replace(
    Storage* storage,
    const char* current,
    const char* temporary,
    const char* backup) {
    storage_common_remove(storage, backup);
    const bool had_current = storage_file_exists(storage, current);
    if(had_current && storage_common_rename(storage, current, backup) != FSE_OK) return false;
    if(storage_common_rename(storage, temporary, current) != FSE_OK) {
        if(had_current) storage_common_rename(storage, backup, current);
        return false;
    }
    storage_common_remove(storage, backup);
    return true;
}

bool tumocard_package_save_state(
    Storage* storage,
    const TumoCardPackageStore* store,
    size_t index,
    const TumoCardApplet* applet) {
    if(!storage || !store || !applet || index >= TUMOCARD_APPLET_MAX) return false;
    char path[TUMOCARD_PATH_MAX];
    char temporary[TUMOCARD_PATH_MAX];
    char backup[TUMOCARD_PATH_MAX];
    if(!tumocard_path(path, store->directories[index], "state.tvs") ||
       !tumocard_path(temporary, store->directories[index], "state.tmp") ||
       !tumocard_path(backup, store->directories[index], "state.bak"))
        return false;

    storage_common_remove(storage, temporary);
    FlipperFormat* format = flipper_format_file_alloc(storage);
    bool success = false;
    do {
        if(!flipper_format_file_open_always(format, temporary)) break;
        if(!flipper_format_write_header_cstr(
               format, TUMOCARD_STATE_FILETYPE, TUMOCARD_STATE_VERSION))
            break;
        const uint32_t state_size = applet->vm.program.state_size;
        if(!flipper_format_write_uint32(format, "State size", &state_size, 1U)) break;
        if(!flipper_format_write_hex(format, "Data", applet->vm.state, state_size)) break;
        success = true;
    } while(false);
    flipper_format_free(format);
    if(!success) {
        storage_common_remove(storage, temporary);
        return false;
    }
    if(!tumocard_atomic_replace(storage, path, temporary, backup)) {
        storage_common_remove(storage, temporary);
        return false;
    }
    return true;
}

bool tumocard_package_save_enabled(
    Storage* storage,
    const TumoCardPackageStore* store,
    size_t index,
    bool enabled) {
    if(!storage || !store || index >= TUMOCARD_APPLET_MAX) return false;
    char path[TUMOCARD_PATH_MAX];
    char temporary[TUMOCARD_PATH_MAX];
    char backup[TUMOCARD_PATH_MAX];
    if(!tumocard_path(path, store->directories[index], "settings.tcs") ||
       !tumocard_path(temporary, store->directories[index], "settings.tmp") ||
       !tumocard_path(backup, store->directories[index], "settings.bak"))
        return false;
    storage_common_remove(storage, temporary);
    if(!tumocard_write_settings(storage, temporary, enabled)) {
        storage_common_remove(storage, temporary);
        return false;
    }
    if(!tumocard_atomic_replace(storage, path, temporary, backup)) {
        storage_common_remove(storage, temporary);
        return false;
    }
    return true;
}

static void tumocard_sort(TumoCardRegistry* registry, TumoCardPackageStore* store) {
    for(size_t i = 1U; i < registry->applet_count; i++) {
        size_t current = i;
        while(current > 0U &&
              strcmp(registry->applets[current - 1U].id, registry->applets[current].id) > 0) {
            TumoCardApplet applet = registry->applets[current - 1U];
            registry->applets[current - 1U] = registry->applets[current];
            registry->applets[current] = applet;
            char directory[TUMOCARD_PATH_MAX];
            memcpy(directory, store->directories[current - 1U], sizeof(directory));
            memcpy(
                store->directories[current - 1U], store->directories[current], sizeof(directory));
            memcpy(store->directories[current], directory, sizeof(directory));
            current--;
        }
    }
}

TumoCardPackageResult tumocard_package_load(
    Storage* storage,
    TumoCardRegistry* registry,
    TumoCardPackageStore* store) {
    if(!storage || !registry || !store) return TumoCardPackageResultIo;
    tumocard_registry_init(registry);
    memset(store, 0, sizeof(*store));
    if(!tumocard_make_dir(storage, TUMOCARD_DATA_DIR) ||
       !tumocard_make_dir(storage, TUMOCARD_APPLETS_DIR))
        return TumoCardPackageResultIo;

    bool created = false;
    if(!tumocard_has_applet_directory(storage)) {
        if(!tumocard_create_references(storage)) return TumoCardPackageResultIo;
        created = true;
    }

    File* directory = storage_file_alloc(storage);
    if(!storage_dir_open(directory, TUMOCARD_APPLETS_DIR)) {
        storage_file_free(directory);
        return TumoCardPackageResultIo;
    }

    FileInfo info;
    char id[TUMOCARD_APPLET_ID_MAX];
    while(storage_dir_read(directory, &info, id, sizeof(id))) {
        if(!file_info_is_dir(&info)) continue;
        if(registry->applet_count >= TUMOCARD_APPLET_MAX) {
            tumocard_mark_invalid(registry);
            continue;
        }

        char applet_directory[TUMOCARD_PATH_MAX];
        if(!tumocard_path(applet_directory, TUMOCARD_APPLETS_DIR, id)) {
            tumocard_mark_invalid(registry);
            continue;
        }

        if(!tumocard_recover_atomic(
               storage, applet_directory, "state.tvs", "state.tmp", "state.bak")) {
            tumocard_mark_invalid(registry);
            continue;
        }

        TumoVmProgram program;
        uint8_t initial_state[TUMOVM_STATE_MAX];
        uint8_t state[TUMOVM_STATE_MAX];
        bool enabled = true;
        if(!tumocard_read_applet(storage, applet_directory, &program, initial_state, &enabled)) {
            tumocard_mark_invalid(registry);
            continue;
        }

        char state_path[TUMOCARD_PATH_MAX];
        if(!tumocard_path(state_path, applet_directory, "state.tvs")) {
            tumocard_mark_invalid(registry);
            continue;
        }
        const bool had_state = storage_file_exists(storage, state_path);
        if(had_state) {
            memset(state, 0, sizeof(state));
            if(!tumocard_read_state(storage, applet_directory, &program, state)) {
                tumocard_mark_invalid(registry);
                continue;
            }
        } else {
            memcpy(state, initial_state, sizeof(state));
        }

        const TumoCardAddResult add_result =
            tumocard_registry_add(registry, id, enabled, &program, state);
        if(add_result != TumoCardAddResultOk) {
            tumocard_mark_invalid(registry);
            continue;
        }

        const size_t index = registry->applet_count - 1U;
        strlcpy(store->directories[index], applet_directory, sizeof(store->directories[index]));
        if(!had_state &&
           !tumocard_package_save_state(storage, store, index, &registry->applets[index])) {
            registry->applet_count--;
            tumocard_mark_invalid(registry);
            continue;
        }
        if(!had_state) created = true;
    }
    storage_dir_close(directory);
    storage_file_free(directory);

    tumocard_sort(registry, store);
    if(registry->applet_count == 0U) return TumoCardPackageResultNoValidApplet;
    if(registry->invalid_count > 0U) return TumoCardPackageResultPartial;
    return created ? TumoCardPackageResultCreated : TumoCardPackageResultOk;
}

const char* tumocard_package_result_name(TumoCardPackageResult result) {
    switch(result) {
    case TumoCardPackageResultOk:
        return "Applets loaded";
    case TumoCardPackageResultCreated:
        return "Defaults created";
    case TumoCardPackageResultPartial:
        return "Invalid skipped";
    case TumoCardPackageResultNoValidApplet:
        return "No valid applet";
    default:
        return "Storage error";
    }
}
