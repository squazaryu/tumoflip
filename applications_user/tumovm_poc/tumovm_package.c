#include "tumovm_package.h"

#include <flipper_format/flipper_format.h>
#include <toolbox/path.h>

#define TUMOVM_PROGRAM_FILETYPE "TumoVM Program"
#define TUMOVM_PROGRAM_VERSION  1U
#define TUMOVM_STATE_FILETYPE   "TumoVM State"
#define TUMOVM_STATE_VERSION    1U

#define TUMOVM_STATE_TMP_PATH TUMOVM_PACKAGE_DIR "/state.tmp"
#define TUMOVM_STATE_BAK_PATH TUMOVM_PACKAGE_DIR "/state.bak"

static const uint8_t tumovm_default_aid[] = {0xF0, 0x54, 0x56, 0x4D, 0x01};
static const uint8_t tumovm_default_routes[] = {
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
static const uint8_t tumovm_default_bytecode[] = {
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
static const uint8_t tumovm_default_state[] = {
    'T',
    'U',
    'M',
    'O',
    'V',
    'M',
    ' ',
    'P',
    'O',
    'C',
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x01,
};

static bool tumovm_package_make_dirs(Storage* storage) {
    if(storage_simply_mkdir(storage, TUMOVM_DATA_DIR) != FSE_OK &&
       !storage_dir_exists(storage, TUMOVM_DATA_DIR))
        return false;
    const char* packages_dir = TUMOVM_DATA_DIR "/packages";
    if(storage_simply_mkdir(storage, packages_dir) != FSE_OK &&
       !storage_dir_exists(storage, packages_dir))
        return false;
    if(storage_simply_mkdir(storage, TUMOVM_PACKAGE_DIR) != FSE_OK &&
       !storage_dir_exists(storage, TUMOVM_PACKAGE_DIR))
        return false;
    return true;
}

static bool tumovm_package_write_default_program(Storage* storage) {
    FlipperFormat* format = flipper_format_file_alloc(storage);
    bool success = false;

    do {
        if(!flipper_format_file_open_always(format, TUMOVM_PROGRAM_PATH)) break;
        if(!flipper_format_write_header_cstr(
               format, TUMOVM_PROGRAM_FILETYPE, TUMOVM_PROGRAM_VERSION))
            break;
        if(!flipper_format_write_string_cstr(format, "Name", "Shared Object")) break;

        const uint32_t state_size = sizeof(tumovm_default_state);
        const uint32_t aid_size = sizeof(tumovm_default_aid);
        const uint32_t route_count = sizeof(tumovm_default_routes) / 3U;
        const uint32_t bytecode_size = sizeof(tumovm_default_bytecode);
        const bool enabled = true;

        if(!flipper_format_write_uint32(format, "State size", &state_size, 1U)) break;
        if(!flipper_format_write_uint32(format, "AID size", &aid_size, 1U)) break;
        if(!flipper_format_write_hex(format, "AID", tumovm_default_aid, sizeof(tumovm_default_aid)))
            break;
        if(!flipper_format_write_uint32(format, "Route count", &route_count, 1U)) break;
        if(!flipper_format_write_hex(
               format, "Routes", tumovm_default_routes, sizeof(tumovm_default_routes)))
            break;
        if(!flipper_format_write_uint32(format, "Bytecode size", &bytecode_size, 1U)) break;
        if(!flipper_format_write_hex(
               format, "Bytecode", tumovm_default_bytecode, sizeof(tumovm_default_bytecode)))
            break;
        if(!flipper_format_write_bool(format, "Capability NFC Type4", &enabled, 1U)) break;
        if(!flipper_format_write_bool(format, "Capability USB CCID", &enabled, 1U)) break;
        success = true;
    } while(false);

    flipper_format_free(format);
    return success;
}

static bool tumovm_package_read_program(Storage* storage, TumoVmProgram* program) {
    FlipperFormat* format = flipper_format_file_alloc(storage);
    FuriString* header = furi_string_alloc();
    FuriString* name = furi_string_alloc();
    uint32_t version = 0;
    uint32_t state_size = 0;
    uint32_t aid_size = 0;
    uint32_t route_count = 0;
    uint32_t bytecode_size = 0;
    uint8_t routes[TUMOVM_ROUTES_MAX * 3U] = {0};
    bool success = false;

    memset(program, 0, sizeof(*program));

    do {
        if(!flipper_format_file_open_existing(format, TUMOVM_PROGRAM_PATH)) break;
        if(!flipper_format_read_header(format, header, &version)) break;
        if(version != TUMOVM_PROGRAM_VERSION ||
           furi_string_cmp_str(header, TUMOVM_PROGRAM_FILETYPE) != 0)
            break;
        if(!flipper_format_read_string(format, "Name", name)) break;
        if(furi_string_size(name) == 0U || furi_string_size(name) >= TUMOVM_NAME_MAX) break;
        if(!flipper_format_read_uint32(format, "State size", &state_size, 1U)) break;
        if(!flipper_format_read_uint32(format, "AID size", &aid_size, 1U)) break;
        if(state_size == 0U || state_size > TUMOVM_STATE_MAX || aid_size == 0U ||
           aid_size > TUMOVM_AID_MAX)
            break;
        if(!flipper_format_read_hex(format, "AID", program->aid, aid_size)) break;
        if(!flipper_format_read_uint32(format, "Route count", &route_count, 1U)) break;
        if(route_count == 0U || route_count > TUMOVM_ROUTES_MAX) break;
        if(!flipper_format_read_hex(format, "Routes", routes, route_count * 3U)) break;
        if(!flipper_format_read_uint32(format, "Bytecode size", &bytecode_size, 1U)) break;
        if(bytecode_size == 0U || bytecode_size > TUMOVM_BYTECODE_MAX) break;
        if(!flipper_format_read_hex(format, "Bytecode", program->bytecode, bytecode_size)) break;
        if(!flipper_format_read_bool(
               format, "Capability NFC Type4", &program->capability_nfc_type4, 1U))
            break;
        if(!flipper_format_read_bool(
               format, "Capability USB CCID", &program->capability_usb_ccid, 1U))
            break;

        strlcpy(program->name, furi_string_get_cstr(name), sizeof(program->name));
        program->state_size = state_size;
        program->aid_size = aid_size;
        program->route_count = route_count;
        program->bytecode_size = bytecode_size;
        for(size_t i = 0; i < route_count; i++) {
            program->routes[i].cla = routes[i * 3U];
            program->routes[i].ins = routes[i * 3U + 1U];
            program->routes[i].entry = routes[i * 3U + 2U];
        }

        success = tumovm_program_validate(program);
    } while(false);

    furi_string_free(name);
    furi_string_free(header);
    flipper_format_free(format);
    return success;
}

static bool tumovm_package_read_state(
    Storage* storage,
    const TumoVmProgram* program,
    uint8_t state[TUMOVM_STATE_MAX]) {
    FlipperFormat* format = flipper_format_file_alloc(storage);
    FuriString* header = furi_string_alloc();
    uint32_t version = 0;
    uint32_t state_size = 0;
    bool success = false;

    memset(state, 0, TUMOVM_STATE_MAX);

    do {
        if(!flipper_format_file_open_existing(format, TUMOVM_STATE_PATH)) break;
        if(!flipper_format_read_header(format, header, &version)) break;
        if(version != TUMOVM_STATE_VERSION ||
           furi_string_cmp_str(header, TUMOVM_STATE_FILETYPE) != 0)
            break;
        if(!flipper_format_read_uint32(format, "State size", &state_size, 1U)) break;
        if(state_size != program->state_size) break;
        if(!flipper_format_read_hex(format, "Data", state, state_size)) break;
        success = true;
    } while(false);

    furi_string_free(header);
    flipper_format_free(format);
    return success;
}

bool tumovm_package_save_state(
    Storage* storage,
    const TumoVmProgram* program,
    const uint8_t state[TUMOVM_STATE_MAX]) {
    FlipperFormat* format = flipper_format_file_alloc(storage);
    bool success = false;

    storage_common_remove(storage, TUMOVM_STATE_TMP_PATH);
    do {
        if(!flipper_format_file_open_always(format, TUMOVM_STATE_TMP_PATH)) break;
        if(!flipper_format_write_header_cstr(format, TUMOVM_STATE_FILETYPE, TUMOVM_STATE_VERSION))
            break;
        const uint32_t state_size = program->state_size;
        if(!flipper_format_write_uint32(format, "State size", &state_size, 1U)) break;
        if(!flipper_format_write_hex(format, "Data", state, state_size)) break;
        success = true;
    } while(false);

    flipper_format_free(format);
    if(!success) {
        storage_common_remove(storage, TUMOVM_STATE_TMP_PATH);
        return false;
    }

    storage_common_remove(storage, TUMOVM_STATE_BAK_PATH);
    const bool had_state = storage_file_exists(storage, TUMOVM_STATE_PATH);
    if(had_state &&
       storage_common_rename(storage, TUMOVM_STATE_PATH, TUMOVM_STATE_BAK_PATH) != FSE_OK) {
        storage_common_remove(storage, TUMOVM_STATE_TMP_PATH);
        return false;
    }
    if(storage_common_rename(storage, TUMOVM_STATE_TMP_PATH, TUMOVM_STATE_PATH) != FSE_OK) {
        storage_common_remove(storage, TUMOVM_STATE_TMP_PATH);
        if(had_state) {
            storage_common_rename(storage, TUMOVM_STATE_BAK_PATH, TUMOVM_STATE_PATH);
        }
        return false;
    }
    storage_common_remove(storage, TUMOVM_STATE_BAK_PATH);
    return true;
}

TumoVmPackageResult
    tumovm_package_load(Storage* storage, TumoVmProgram* program, uint8_t state[TUMOVM_STATE_MAX]) {
    if(!tumovm_package_make_dirs(storage)) return TumoVmPackageResultIo;

    bool created = false;
    if(!storage_file_exists(storage, TUMOVM_PROGRAM_PATH)) {
        if(!tumovm_package_write_default_program(storage)) return TumoVmPackageResultIo;
        created = true;
    }

    if(!tumovm_package_read_program(storage, program)) return TumoVmPackageResultInvalidProgram;

    if(storage_file_exists(storage, TUMOVM_STATE_PATH)) {
        if(!tumovm_package_read_state(storage, program, state))
            return TumoVmPackageResultInvalidState;
    } else {
        memset(state, 0, TUMOVM_STATE_MAX);
        const size_t copy_size = program->state_size < sizeof(tumovm_default_state) ?
                                     program->state_size :
                                     sizeof(tumovm_default_state);
        memcpy(state, tumovm_default_state, copy_size);
        if(!tumovm_package_save_state(storage, program, state)) return TumoVmPackageResultIo;
        created = true;
    }

    return created ? TumoVmPackageResultCreated : TumoVmPackageResultOk;
}

const char* tumovm_package_result_name(TumoVmPackageResult result) {
    switch(result) {
    case TumoVmPackageResultOk:
        return "Package loaded";
    case TumoVmPackageResultCreated:
        return "Package created";
    case TumoVmPackageResultInvalidProgram:
        return "Invalid program";
    case TumoVmPackageResultInvalidState:
        return "Invalid state";
    default:
        return "Storage error";
    }
}
