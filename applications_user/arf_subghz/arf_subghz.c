#include "arf_subghz_keeloq_keys.h"

#include <dialogs/dialogs.h>
#include <flipper_format/flipper_format.h>
#include <furi.h>
#include <gui/gui.h>
#include <gui/modules/byte_input.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_box.h>
#include <gui/modules/text_input.h>
#include <gui/view_dispatcher.h>
#include <stdio.h>
#include <storage/storage.h>
#include <string.h>
#include <toolbox/path.h>

#define TAG "ArfSubGhz"

#define ARF_SUBGHZ_FOLDER              EXT_PATH("subghz")
#define ARF_SUBGHZ_SUB_EXTENSION       ".sub"
#define ARF_SUBGHZ_KEELOQ_NAME_SIZE    32
#define ARF_SUBGHZ_KEELOQ_KEY_SIZE     8
#define ARF_SUBGHZ_KEELOQ_TYPE_COUNT   8
#define ARF_SUBGHZ_EVENT_KEY_BASE      1000
#define ARF_SUBGHZ_EVENT_KEY_TYPE_BASE 2000

typedef enum {
    ArfSubGhzViewMain,
    ArfSubGhzViewText,
    ArfSubGhzViewByteInput,
    ArfSubGhzViewTextInput,
    ArfSubGhzViewKeeloqList,
    ArfSubGhzViewKeeloqOptions,
    ArfSubGhzViewKeeloqType,
} ArfSubGhzView;

typedef enum {
    ArfSubGhzEventOpenSub = 1,
    ArfSubGhzEventKeeloqKeys,
    ArfSubGhzEventAbout,
    ArfSubGhzEventKeeloqAdd,
    ArfSubGhzEventKeeloqInfo,
    ArfSubGhzEventKeeloqEdit,
    ArfSubGhzEventKeeloqDelete,
    ArfSubGhzEventKeeloqByteDone,
    ArfSubGhzEventKeeloqNameDone,
} ArfSubGhzEvent;

typedef struct {
    Gui* gui;
    DialogsApp* dialogs;
    Storage* storage;
    ViewDispatcher* view_dispatcher;
    Submenu* main_menu;
    Submenu* keeloq_list_menu;
    Submenu* keeloq_options_menu;
    Submenu* keeloq_type_menu;
    TextBox* text_box;
    ByteInput* byte_input;
    TextInput* text_input;
    FuriString* file_path;
    FuriString* text;
    ArfSubGhzKeeloqKeys* keeloq_keys;
    size_t selected_keeloq_index;
    bool edit_keeloq_is_new;
    uint8_t edit_keeloq_key[ARF_SUBGHZ_KEELOQ_KEY_SIZE];
    char edit_keeloq_name[ARF_SUBGHZ_KEELOQ_NAME_SIZE];
    uint16_t edit_keeloq_type;
} ArfSubGhzApp;

static const char* const arf_subghz_keeloq_type_names[ARF_SUBGHZ_KEELOQ_TYPE_COUNT] = {
    "1-Simple",
    "2-Normal",
    "3-Secure",
    "4-Magic XOR",
    "5-FAAC SLH",
    "6-Magic Ser1",
    "7-Magic Ser2",
    "8-Magic Ser3",
};

static void arf_subghz_menu_callback(void* context, uint32_t index);
static void arf_subghz_byte_input_callback(void* context);
static void arf_subghz_text_input_callback(void* context);

static const char* arf_subghz_keeloq_type_name(uint16_t type) {
    if((type >= 1) && (type <= ARF_SUBGHZ_KEELOQ_TYPE_COUNT)) {
        return arf_subghz_keeloq_type_names[type - 1];
    }

    return "Unknown";
}

static void arf_subghz_show_text(ArfSubGhzApp* app) {
    text_box_set_text(app->text_box, furi_string_get_cstr(app->text));
    view_dispatcher_switch_to_view(app->view_dispatcher, ArfSubGhzViewText);
}

static bool arf_subghz_read_string_field(FlipperFormat* format, const char* key, FuriString* out) {
    furi_string_reset(out);
    return flipper_format_read_string(format, key, out);
}

static void arf_subghz_append_string_field(
    FlipperFormat* format,
    FuriString* scratch,
    FuriString* out,
    const char* key) {
    if(arf_subghz_read_string_field(format, key, scratch)) {
        furi_string_cat_printf(out, "%s: %s\n", key, furi_string_get_cstr(scratch));
    }
}

static void arf_subghz_append_uint32_field(
    FlipperFormat* format,
    FuriString* out,
    const char* key,
    bool hex) {
    uint32_t value = 0;
    if(flipper_format_read_uint32(format, key, &value, 1)) {
        if(hex) {
            furi_string_cat_printf(out, "%s: 0x%08lX\n", key, value);
        } else {
            furi_string_cat_printf(out, "%s: %lu\n", key, value);
        }
    }
}

static void arf_subghz_show_sub_file(ArfSubGhzApp* app, const char* path) {
    FlipperFormat* format = flipper_format_file_alloc(app->storage);
    FuriString* scratch = furi_string_alloc();
    uint32_t version = 0;

    furi_string_printf(app->text, "Saved Sub-GHz\n%s\n\n", path);

    do {
        if(!flipper_format_file_open_existing(format, path)) {
            furi_string_cat_str(app->text, "Cannot open file.\n");
            break;
        }

        if(flipper_format_read_header(format, scratch, &version)) {
            furi_string_cat_printf(
                app->text, "Header: %s v%lu\n", furi_string_get_cstr(scratch), version);
        }

        arf_subghz_append_uint32_field(format, app->text, "Frequency", false);
        arf_subghz_append_string_field(format, scratch, app->text, "Preset");
        arf_subghz_append_string_field(format, scratch, app->text, "Protocol");
        arf_subghz_append_string_field(format, scratch, app->text, "Manufacture");
        arf_subghz_append_uint32_field(format, app->text, "Bit", false);
        arf_subghz_append_uint32_field(format, app->text, "Key", true);
        arf_subghz_append_uint32_field(format, app->text, "Sn", true);
        arf_subghz_append_uint32_field(format, app->text, "Cnt", true);
        arf_subghz_append_uint32_field(format, app->text, "Btn", true);

        furi_string_cat_str(app->text, "\nRead-only ARF workspace.\n");
    } while(false);

    furi_string_free(scratch);
    flipper_format_free(format);
    arf_subghz_show_text(app);
}

static void arf_subghz_open_sub(ArfSubGhzApp* app) {
    storage_simply_mkdir(app->storage, ARF_SUBGHZ_FOLDER);
    furi_string_set_str(app->file_path, ARF_SUBGHZ_FOLDER);

    DialogsFileBrowserOptions browser;
    dialog_file_browser_set_basic_options(&browser, ARF_SUBGHZ_SUB_EXTENSION, NULL);
    browser.base_path = ARF_SUBGHZ_FOLDER;

    if(dialog_file_browser_show(app->dialogs, app->file_path, app->file_path, &browser)) {
        arf_subghz_show_sub_file(app, furi_string_get_cstr(app->file_path));
    } else {
        view_dispatcher_switch_to_view(app->view_dispatcher, ArfSubGhzViewMain);
    }
}

static void arf_subghz_keeloq_reload(ArfSubGhzApp* app) {
    if(app->keeloq_keys) {
        arf_subghz_keeloq_keys_free(app->keeloq_keys);
    }

    app->keeloq_keys = arf_subghz_keeloq_keys_alloc();
}

static void arf_subghz_keeloq_sanitize_name(char* name) {
    bool has_visible = false;

    for(size_t i = 0; name[i] != '\0'; i++) {
        if((name[i] == ':') || (name[i] == '\r') || (name[i] == '\n')) {
            name[i] = '_';
        }

        if(name[i] > ' ') {
            has_visible = true;
        }
    }

    if(!has_visible) {
        snprintf(name, ARF_SUBGHZ_KEELOQ_NAME_SIZE, "Custom");
    }
}

static uint64_t arf_subghz_keeloq_key_from_bytes(const uint8_t* bytes) {
    uint64_t key = 0;

    for(size_t i = 0; i < ARF_SUBGHZ_KEELOQ_KEY_SIZE; i++) {
        key = (key << 8) | bytes[i];
    }

    return key;
}

static void arf_subghz_keeloq_key_to_bytes(uint64_t key, uint8_t* bytes) {
    for(size_t i = 0; i < ARF_SUBGHZ_KEELOQ_KEY_SIZE; i++) {
        const size_t shift = (ARF_SUBGHZ_KEELOQ_KEY_SIZE - 1 - i) * 8;
        bytes[i] = (key >> shift) & 0xFF;
    }
}

static void arf_subghz_show_keeloq_list(ArfSubGhzApp* app) {
    if(!app->keeloq_keys) {
        arf_subghz_keeloq_reload(app);
    }

    submenu_reset(app->keeloq_list_menu);
    char header[32];
    snprintf(
        header,
        sizeof(header),
        "KL Keys U:%zu S:%zu",
        arf_subghz_keeloq_keys_user_count(app->keeloq_keys),
        arf_subghz_keeloq_keys_count(app->keeloq_keys) -
            arf_subghz_keeloq_keys_user_count(app->keeloq_keys));
    submenu_set_header(app->keeloq_list_menu, header);
    submenu_add_item(
        app->keeloq_list_menu,
        "+ Add Key",
        ArfSubGhzEventKeeloqAdd,
        arf_subghz_menu_callback,
        app);

    const size_t total = arf_subghz_keeloq_keys_count(app->keeloq_keys);
    const size_t user_count = arf_subghz_keeloq_keys_user_count(app->keeloq_keys);
    for(size_t index = 0; index < total; index++) {
        SubGhzKey* key = arf_subghz_keeloq_keys_get(app->keeloq_keys, index);
        char label[64];
        snprintf(
            label,
            sizeof(label),
            "%c %s",
            index < user_count ? 'U' : 'S',
            furi_string_get_cstr(key->name));
        submenu_add_item(
            app->keeloq_list_menu,
            label,
            ARF_SUBGHZ_EVENT_KEY_BASE + index,
            arf_subghz_menu_callback,
            app);
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, ArfSubGhzViewKeeloqList);
}

static void arf_subghz_show_keeloq_info(ArfSubGhzApp* app) {
    SubGhzKey* key = arf_subghz_keeloq_keys_get(app->keeloq_keys, app->selected_keeloq_index);
    const bool user_key =
        app->selected_keeloq_index < arf_subghz_keeloq_keys_user_count(app->keeloq_keys);

    furi_string_printf(
        app->text,
        "%s Keeloq Key\n\n"
        "Name: %s\n"
        "Type: %s\n"
        "Key: %016llX\n\n"
        "%s",
        user_key ? "User" : "System",
        furi_string_get_cstr(key->name),
        arf_subghz_keeloq_type_name(key->type),
        (unsigned long long)key->key,
        user_key ? "User entry is editable." : "System entry is read-only.");
    arf_subghz_show_text(app);
}

static void arf_subghz_show_keeloq_options(ArfSubGhzApp* app) {
    SubGhzKey* key = arf_subghz_keeloq_keys_get(app->keeloq_keys, app->selected_keeloq_index);

    submenu_reset(app->keeloq_options_menu);
    submenu_set_header(app->keeloq_options_menu, furi_string_get_cstr(key->name));
    submenu_add_item(
        app->keeloq_options_menu,
        "Info",
        ArfSubGhzEventKeeloqInfo,
        arf_subghz_menu_callback,
        app);
    submenu_add_item(
        app->keeloq_options_menu,
        "Edit",
        ArfSubGhzEventKeeloqEdit,
        arf_subghz_menu_callback,
        app);
    submenu_add_item(
        app->keeloq_options_menu,
        "Delete",
        ArfSubGhzEventKeeloqDelete,
        arf_subghz_menu_callback,
        app);

    view_dispatcher_switch_to_view(app->view_dispatcher, ArfSubGhzViewKeeloqOptions);
}

static void arf_subghz_show_keeloq_type_menu(ArfSubGhzApp* app) {
    submenu_reset(app->keeloq_type_menu);
    submenu_set_header(app->keeloq_type_menu, "Key Type");

    for(uint16_t type = 1; type <= ARF_SUBGHZ_KEELOQ_TYPE_COUNT; type++) {
        submenu_add_item(
            app->keeloq_type_menu,
            arf_subghz_keeloq_type_name(type),
            ARF_SUBGHZ_EVENT_KEY_TYPE_BASE + type,
            arf_subghz_menu_callback,
            app);
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, ArfSubGhzViewKeeloqType);
}

static void arf_subghz_show_keeloq_byte_input(ArfSubGhzApp* app) {
    byte_input_set_header_text(app->byte_input, "Keeloq key");
    byte_input_set_result_callback(
        app->byte_input,
        arf_subghz_byte_input_callback,
        NULL,
        app,
        app->edit_keeloq_key,
        sizeof(app->edit_keeloq_key));
    view_dispatcher_switch_to_view(app->view_dispatcher, ArfSubGhzViewByteInput);
}

static void arf_subghz_show_keeloq_name_input(ArfSubGhzApp* app) {
    text_input_reset(app->text_input);
    text_input_set_header_text(app->text_input, "Manufacturer");
    text_input_set_minimum_length(app->text_input, 1);
    text_input_set_result_callback(
        app->text_input,
        arf_subghz_text_input_callback,
        app,
        app->edit_keeloq_name,
        sizeof(app->edit_keeloq_name),
        false);
    view_dispatcher_switch_to_view(app->view_dispatcher, ArfSubGhzViewTextInput);
}

static void arf_subghz_start_keeloq_add(ArfSubGhzApp* app) {
    memset(app->edit_keeloq_key, 0, sizeof(app->edit_keeloq_key));
    snprintf(app->edit_keeloq_name, sizeof(app->edit_keeloq_name), "Custom");
    app->edit_keeloq_type = 1;
    app->edit_keeloq_is_new = true;
    app->selected_keeloq_index = SIZE_MAX;
    arf_subghz_show_keeloq_byte_input(app);
}

static void arf_subghz_start_keeloq_edit(ArfSubGhzApp* app) {
    SubGhzKey* key = arf_subghz_keeloq_keys_get(app->keeloq_keys, app->selected_keeloq_index);

    arf_subghz_keeloq_key_to_bytes(key->key, app->edit_keeloq_key);
    snprintf(app->edit_keeloq_name, sizeof(app->edit_keeloq_name), "%s", furi_string_get_cstr(key->name));
    app->edit_keeloq_type = key->type;
    app->edit_keeloq_is_new = false;
    arf_subghz_show_keeloq_byte_input(app);
}

static void arf_subghz_save_keeloq_edit(ArfSubGhzApp* app, uint16_t type) {
    app->edit_keeloq_type = type;
    arf_subghz_keeloq_sanitize_name(app->edit_keeloq_name);

    const uint64_t key = arf_subghz_keeloq_key_from_bytes(app->edit_keeloq_key);
    if(app->edit_keeloq_is_new) {
        arf_subghz_keeloq_keys_add(app->keeloq_keys, key, app->edit_keeloq_type, app->edit_keeloq_name);
    } else {
        arf_subghz_keeloq_keys_set(
            app->keeloq_keys,
            app->selected_keeloq_index,
            key,
            app->edit_keeloq_type,
            app->edit_keeloq_name);
    }

    if(!arf_subghz_keeloq_keys_save(app->keeloq_keys)) {
        furi_string_set_str(app->text, "Keeloq Keys\n\nCannot save user keystore.");
        arf_subghz_show_text(app);
        return;
    }

    arf_subghz_show_keeloq_list(app);
}

static void arf_subghz_delete_selected_keeloq(ArfSubGhzApp* app) {
    if(app->selected_keeloq_index >= arf_subghz_keeloq_keys_user_count(app->keeloq_keys)) {
        furi_string_set_str(app->text, "Keeloq Keys\n\nSystem key is read-only.");
        arf_subghz_show_text(app);
        return;
    }

    arf_subghz_keeloq_keys_delete(app->keeloq_keys, app->selected_keeloq_index);
    if(!arf_subghz_keeloq_keys_save(app->keeloq_keys)) {
        furi_string_set_str(app->text, "Keeloq Keys\n\nCannot save user keystore.");
        arf_subghz_show_text(app);
        return;
    }

    app->selected_keeloq_index = SIZE_MAX;
    arf_subghz_show_keeloq_list(app);
}

static void arf_subghz_show_about(ArfSubGhzApp* app) {
    furi_string_set_str(
        app->text,
        "ARF Sub-GHz 0.2\n\n"
        "Isolated ARF workspace for saved Sub-GHz files and Keeloq keys.\n\n"
        "Current build:\n"
        "- open .sub files\n"
        "- inspect common fields\n"
        "- manage user Keeloq keys\n\n"
        "Normal Sub-GHz is not modified.");
    arf_subghz_show_text(app);
}

static void arf_subghz_menu_callback(void* context, uint32_t index) {
    ArfSubGhzApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

static void arf_subghz_byte_input_callback(void* context) {
    ArfSubGhzApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, ArfSubGhzEventKeeloqByteDone);
}

static void arf_subghz_text_input_callback(void* context) {
    ArfSubGhzApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, ArfSubGhzEventKeeloqNameDone);
}

static bool arf_subghz_custom_event_callback(void* context, uint32_t event) {
    ArfSubGhzApp* app = context;

    if((event >= ARF_SUBGHZ_EVENT_KEY_BASE) &&
       (event < ARF_SUBGHZ_EVENT_KEY_BASE + arf_subghz_keeloq_keys_count(app->keeloq_keys))) {
        app->selected_keeloq_index = event - ARF_SUBGHZ_EVENT_KEY_BASE;
        if(app->selected_keeloq_index < arf_subghz_keeloq_keys_user_count(app->keeloq_keys)) {
            arf_subghz_show_keeloq_options(app);
        } else {
            arf_subghz_show_keeloq_info(app);
        }
        return true;
    }

    if((event > ARF_SUBGHZ_EVENT_KEY_TYPE_BASE) &&
       (event <= ARF_SUBGHZ_EVENT_KEY_TYPE_BASE + ARF_SUBGHZ_KEELOQ_TYPE_COUNT)) {
        arf_subghz_save_keeloq_edit(app, event - ARF_SUBGHZ_EVENT_KEY_TYPE_BASE);
        return true;
    }

    switch(event) {
    case ArfSubGhzEventOpenSub:
        arf_subghz_open_sub(app);
        return true;
    case ArfSubGhzEventKeeloqKeys:
        arf_subghz_keeloq_reload(app);
        arf_subghz_show_keeloq_list(app);
        return true;
    case ArfSubGhzEventAbout:
        arf_subghz_show_about(app);
        return true;
    case ArfSubGhzEventKeeloqAdd:
        arf_subghz_start_keeloq_add(app);
        return true;
    case ArfSubGhzEventKeeloqInfo:
        arf_subghz_show_keeloq_info(app);
        return true;
    case ArfSubGhzEventKeeloqEdit:
        arf_subghz_start_keeloq_edit(app);
        return true;
    case ArfSubGhzEventKeeloqDelete:
        arf_subghz_delete_selected_keeloq(app);
        return true;
    case ArfSubGhzEventKeeloqByteDone:
        arf_subghz_show_keeloq_name_input(app);
        return true;
    case ArfSubGhzEventKeeloqNameDone:
        arf_subghz_show_keeloq_type_menu(app);
        return true;
    default:
        return false;
    }
}

static uint32_t arf_subghz_nav_exit(void* context) {
    UNUSED(context);
    return VIEW_NONE;
}

static uint32_t arf_subghz_nav_to_main(void* context) {
    UNUSED(context);
    return ArfSubGhzViewMain;
}

static uint32_t arf_subghz_nav_to_keeloq_list(void* context) {
    UNUSED(context);
    return ArfSubGhzViewKeeloqList;
}

static ArfSubGhzApp* arf_subghz_app_alloc(void) {
    ArfSubGhzApp* app = malloc(sizeof(ArfSubGhzApp));

    app->gui = furi_record_open(RECORD_GUI);
    app->dialogs = furi_record_open(RECORD_DIALOGS);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->file_path = furi_string_alloc();
    app->text = furi_string_alloc();
    app->keeloq_keys = NULL;
    app->selected_keeloq_index = SIZE_MAX;

    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher, arf_subghz_custom_event_callback);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    app->main_menu = submenu_alloc();
    submenu_set_header(app->main_menu, "ARF Sub-GHz");
    submenu_add_item(
        app->main_menu, "Open .sub", ArfSubGhzEventOpenSub, arf_subghz_menu_callback, app);
    submenu_add_item(
        app->main_menu,
        "Keeloq Keys",
        ArfSubGhzEventKeeloqKeys,
        arf_subghz_menu_callback,
        app);
    submenu_add_item(app->main_menu, "About", ArfSubGhzEventAbout, arf_subghz_menu_callback, app);
    view_set_previous_callback(submenu_get_view(app->main_menu), arf_subghz_nav_exit);
    view_dispatcher_add_view(
        app->view_dispatcher, ArfSubGhzViewMain, submenu_get_view(app->main_menu));

    app->keeloq_list_menu = submenu_alloc();
    view_set_previous_callback(
        submenu_get_view(app->keeloq_list_menu), arf_subghz_nav_to_main);
    view_dispatcher_add_view(
        app->view_dispatcher,
        ArfSubGhzViewKeeloqList,
        submenu_get_view(app->keeloq_list_menu));

    app->keeloq_options_menu = submenu_alloc();
    view_set_previous_callback(
        submenu_get_view(app->keeloq_options_menu), arf_subghz_nav_to_keeloq_list);
    view_dispatcher_add_view(
        app->view_dispatcher,
        ArfSubGhzViewKeeloqOptions,
        submenu_get_view(app->keeloq_options_menu));

    app->keeloq_type_menu = submenu_alloc();
    view_set_previous_callback(
        submenu_get_view(app->keeloq_type_menu), arf_subghz_nav_to_keeloq_list);
    view_dispatcher_add_view(
        app->view_dispatcher,
        ArfSubGhzViewKeeloqType,
        submenu_get_view(app->keeloq_type_menu));

    app->text_box = text_box_alloc();
    text_box_set_font(app->text_box, TextBoxFontText);
    view_set_previous_callback(text_box_get_view(app->text_box), arf_subghz_nav_to_main);
    view_dispatcher_add_view(
        app->view_dispatcher, ArfSubGhzViewText, text_box_get_view(app->text_box));

    app->byte_input = byte_input_alloc();
    view_set_previous_callback(byte_input_get_view(app->byte_input), arf_subghz_nav_to_keeloq_list);
    view_dispatcher_add_view(
        app->view_dispatcher, ArfSubGhzViewByteInput, byte_input_get_view(app->byte_input));

    app->text_input = text_input_alloc();
    view_set_previous_callback(text_input_get_view(app->text_input), arf_subghz_nav_to_keeloq_list);
    view_dispatcher_add_view(
        app->view_dispatcher, ArfSubGhzViewTextInput, text_input_get_view(app->text_input));

    view_dispatcher_switch_to_view(app->view_dispatcher, ArfSubGhzViewMain);
    return app;
}

static void arf_subghz_app_free(ArfSubGhzApp* app) {
    furi_assert(app);

    byte_input_set_result_callback(app->byte_input, NULL, NULL, NULL, NULL, 0);
    byte_input_set_header_text(app->byte_input, "");
    text_input_reset(app->text_input);

    view_dispatcher_remove_view(app->view_dispatcher, ArfSubGhzViewTextInput);
    view_dispatcher_remove_view(app->view_dispatcher, ArfSubGhzViewByteInput);
    view_dispatcher_remove_view(app->view_dispatcher, ArfSubGhzViewText);
    view_dispatcher_remove_view(app->view_dispatcher, ArfSubGhzViewKeeloqType);
    view_dispatcher_remove_view(app->view_dispatcher, ArfSubGhzViewKeeloqOptions);
    view_dispatcher_remove_view(app->view_dispatcher, ArfSubGhzViewKeeloqList);
    view_dispatcher_remove_view(app->view_dispatcher, ArfSubGhzViewMain);

    text_input_free(app->text_input);
    byte_input_free(app->byte_input);
    text_box_free(app->text_box);
    submenu_free(app->keeloq_type_menu);
    submenu_free(app->keeloq_options_menu);
    submenu_free(app->keeloq_list_menu);
    submenu_free(app->main_menu);
    view_dispatcher_free(app->view_dispatcher);

    if(app->keeloq_keys) {
        arf_subghz_keeloq_keys_free(app->keeloq_keys);
    }

    furi_string_free(app->text);
    furi_string_free(app->file_path);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_GUI);
    free(app);
}

int32_t arf_subghz_main(void* p) {
    UNUSED(p);
    ArfSubGhzApp* app = arf_subghz_app_alloc();
    view_dispatcher_run(app->view_dispatcher);
    arf_subghz_app_free(app);
    return 0;
}
