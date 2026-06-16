#include "arf_keeloq_keys.h"

#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_box.h>
#include <dialogs/dialogs.h>
#include <storage/storage.h>
#include <flipper_format/flipper_format.h>
#include <toolbox/path.h>

#define TAG "ArfTools"

#define ARF_TOOLS_SUBGHZ_FOLDER       EXT_PATH("subghz")
#define ARF_TOOLS_ASSETS_FOLDER       EXT_PATH("apps_assets/proto_pirate")
#define ARF_TOOLS_SETTING_USER_PATH   EXT_PATH("apps_assets/proto_pirate/setting_user")
#define ARF_TOOLS_PROTO_PIRATE_PATH   EXT_PATH("apps/ARF Tools/ProtoPirate.fap")
#define ARF_TOOLS_APP_PATH            EXT_PATH("apps/ARF Tools/ARF Tools.fap")
#define ARF_TOOLS_SUB_EXTENSION       ".sub"
#define ARF_TOOLS_MAX_KEY_LINES       10

typedef enum {
    ArfToolsViewMain,
    ArfToolsViewText,
} ArfToolsView;

typedef enum {
    ArfToolsMenuKeeloq,
    ArfToolsMenuOpenSub,
    ArfToolsMenuAssets,
    ArfToolsMenuAbout,
} ArfToolsMenu;

typedef enum {
    ArfToolsEventKeeloq,
    ArfToolsEventOpenSub,
    ArfToolsEventAssets,
    ArfToolsEventAbout,
} ArfToolsEvent;

typedef struct {
    Gui* gui;
    DialogsApp* dialogs;
    Storage* storage;
    ViewDispatcher* view_dispatcher;
    Submenu* main_menu;
    TextBox* text_box;
    FuriString* file_path;
    FuriString* text;
} ArfToolsApp;

static void arf_tools_set_text(ArfToolsApp* app, const char* text) {
    furi_string_set_str(app->text, text);
    text_box_set_text(app->text_box, furi_string_get_cstr(app->text));
    view_dispatcher_switch_to_view(app->view_dispatcher, ArfToolsViewText);
}

static void arf_tools_set_text_string(ArfToolsApp* app) {
    text_box_set_text(app->text_box, furi_string_get_cstr(app->text));
    view_dispatcher_switch_to_view(app->view_dispatcher, ArfToolsViewText);
}

static bool arf_tools_path_exists(ArfToolsApp* app, const char* path) {
    return storage_common_stat(app->storage, path, NULL) == FSE_OK;
}

static void arf_tools_append_exists(ArfToolsApp* app, const char* label, const char* path) {
    furi_string_cat_printf(
        app->text, "%s: %s\n", label, arf_tools_path_exists(app, path) ? "OK" : "missing");
}

static bool arf_tools_read_string_field(FlipperFormat* format, const char* key, FuriString* out) {
    furi_string_reset(out);
    return flipper_format_read_string(format, key, out);
}

static void arf_tools_append_string_field(
    FlipperFormat* format,
    FuriString* scratch,
    FuriString* out,
    const char* key) {
    if(arf_tools_read_string_field(format, key, scratch)) {
        furi_string_cat_printf(out, "%s: %s\n", key, furi_string_get_cstr(scratch));
    }
}

static void arf_tools_append_uint32_field(
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

static void arf_tools_show_keeloq(ArfToolsApp* app) {
    ArfKeeloqKeys* keys = arf_keeloq_keys_alloc();
    const size_t user_count = arf_keeloq_keys_user_count(keys);
    const size_t total_count = arf_keeloq_keys_count(keys);

    furi_string_printf(
        app->text,
        "Keeloq Keys\n\nUser: %zu\nSystem: %zu\nTotal: %zu\n\n",
        user_count,
        total_count - user_count,
        total_count);

    if(total_count == 0) {
        furi_string_cat_str(app->text, "No keys loaded.\n");
    } else {
        const size_t lines =
            total_count < ARF_TOOLS_MAX_KEY_LINES ? total_count : ARF_TOOLS_MAX_KEY_LINES;
        for(size_t i = 0; i < lines; i++) {
            SubGhzKey* key = arf_keeloq_keys_get(keys, i);
            furi_string_cat_printf(
                app->text,
                "%c %s\n  %016llX type:%u\n",
                i < user_count ? 'U' : 'S',
                furi_string_get_cstr(key->name),
                (unsigned long long)key->key,
                key->type);
        }
        if(total_count > lines) {
            furi_string_cat_printf(app->text, "\n...%zu more\n", total_count - lines);
        }
    }

    arf_keeloq_keys_free(keys);
    arf_tools_set_text_string(app);
}

static void arf_tools_show_assets(ArfToolsApp* app) {
    furi_string_set_str(app->text, "ARF / ProtoPirate status\n\n");
    arf_tools_append_exists(app, "ARF Tools app", ARF_TOOLS_APP_PATH);
    arf_tools_append_exists(app, "ProtoPirate app", ARF_TOOLS_PROTO_PIRATE_PATH);
    arf_tools_append_exists(app, "Assets folder", ARF_TOOLS_ASSETS_FOLDER);
    arf_tools_append_exists(app, "ProtoPirate settings", ARF_TOOLS_SETTING_USER_PATH);
    arf_tools_append_exists(
        app,
        "AM plugin",
        EXT_PATH("apps_assets/proto_pirate/plugins/protopirate_am_plugin.fal"));
    arf_tools_append_exists(
        app,
        "FM plugin",
        EXT_PATH("apps_assets/proto_pirate/plugins/protopirate_fm_plugin.fal"));
    arf_tools_append_exists(
        app,
        "Emulate plugin",
        EXT_PATH("apps_assets/proto_pirate/plugins/protopirate_emulate_plugin.fal"));
    arf_tools_append_exists(
        app,
        "PSA BF plugin",
        EXT_PATH("apps_assets/proto_pirate/plugins/protopirate_psa_bf_plugin.fal"));
    furi_string_cat_str(
        app->text,
        "\nFull ARF screens are intentionally isolated from normal Sub-GHz.\n");
    arf_tools_set_text_string(app);
}

static void arf_tools_show_about(ArfToolsApp* app) {
    arf_tools_set_text(
        app,
        "ARF Tools 0.1\n\n"
        "Separate tumoflip launcher for ARF-related Sub-GHz helpers.\n\n"
        "Current build:\n"
        "- Keeloq keystore viewer\n"
        "- saved .sub inspector\n"
        "- ProtoPirate asset check\n\n"
        "ProtoPirate remains the active ARF decoder/emulator app.");
}

static void arf_tools_show_sub_file(ArfToolsApp* app, const char* path) {
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

        arf_tools_append_uint32_field(format, app->text, "Frequency", false);
        arf_tools_append_string_field(format, scratch, app->text, "Preset");
        arf_tools_append_string_field(format, scratch, app->text, "Protocol");
        arf_tools_append_string_field(format, scratch, app->text, "Manufacture");
        arf_tools_append_uint32_field(format, app->text, "Bit", false);
        arf_tools_append_uint32_field(format, app->text, "Key", true);
        arf_tools_append_uint32_field(format, app->text, "Sn", true);
        arf_tools_append_uint32_field(format, app->text, "Cnt", true);
        arf_tools_append_uint32_field(format, app->text, "Btn", true);

        furi_string_cat_str(
            app->text,
            "\nARF actions for this file are kept outside normal Sub-GHz.\n");
    } while(false);

    furi_string_free(scratch);
    flipper_format_free(format);
    arf_tools_set_text_string(app);
}

static void arf_tools_open_sub(ArfToolsApp* app) {
    storage_simply_mkdir(app->storage, ARF_TOOLS_SUBGHZ_FOLDER);
    furi_string_set_str(app->file_path, ARF_TOOLS_SUBGHZ_FOLDER);

    DialogsFileBrowserOptions browser;
    dialog_file_browser_set_basic_options(&browser, ARF_TOOLS_SUB_EXTENSION, NULL);
    browser.base_path = ARF_TOOLS_SUBGHZ_FOLDER;

    if(dialog_file_browser_show(app->dialogs, app->file_path, app->file_path, &browser)) {
        arf_tools_show_sub_file(app, furi_string_get_cstr(app->file_path));
    } else {
        view_dispatcher_switch_to_view(app->view_dispatcher, ArfToolsViewMain);
    }
}

static void arf_tools_menu_callback(void* context, uint32_t index) {
    ArfToolsApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

static bool arf_tools_custom_event_callback(void* context, uint32_t event) {
    ArfToolsApp* app = context;

    switch(event) {
    case ArfToolsEventKeeloq:
        arf_tools_show_keeloq(app);
        return true;
    case ArfToolsEventOpenSub:
        arf_tools_open_sub(app);
        return true;
    case ArfToolsEventAssets:
        arf_tools_show_assets(app);
        return true;
    case ArfToolsEventAbout:
        arf_tools_show_about(app);
        return true;
    default:
        return false;
    }
}

static uint32_t arf_tools_nav_exit(void* context) {
    UNUSED(context);
    return VIEW_NONE;
}

static uint32_t arf_tools_nav_to_main(void* context) {
    UNUSED(context);
    return ArfToolsViewMain;
}

static ArfToolsApp* arf_tools_app_alloc(void) {
    ArfToolsApp* app = malloc(sizeof(ArfToolsApp));

    app->gui = furi_record_open(RECORD_GUI);
    app->dialogs = furi_record_open(RECORD_DIALOGS);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->file_path = furi_string_alloc();
    app->text = furi_string_alloc();

    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher, arf_tools_custom_event_callback);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    app->main_menu = submenu_alloc();
    submenu_set_header(app->main_menu, "ARF Tools");
    submenu_add_item(
        app->main_menu, "Keeloq Keys", ArfToolsMenuKeeloq, arf_tools_menu_callback, app);
    submenu_add_item(
        app->main_menu, "Open .sub", ArfToolsMenuOpenSub, arf_tools_menu_callback, app);
    submenu_add_item(
        app->main_menu, "Assets Status", ArfToolsMenuAssets, arf_tools_menu_callback, app);
    submenu_add_item(app->main_menu, "About", ArfToolsMenuAbout, arf_tools_menu_callback, app);
    view_set_previous_callback(submenu_get_view(app->main_menu), arf_tools_nav_exit);
    view_dispatcher_add_view(
        app->view_dispatcher, ArfToolsViewMain, submenu_get_view(app->main_menu));

    app->text_box = text_box_alloc();
    text_box_set_font(app->text_box, TextBoxFontText);
    view_set_previous_callback(text_box_get_view(app->text_box), arf_tools_nav_to_main);
    view_dispatcher_add_view(
        app->view_dispatcher, ArfToolsViewText, text_box_get_view(app->text_box));

    view_dispatcher_switch_to_view(app->view_dispatcher, ArfToolsViewMain);
    return app;
}

static void arf_tools_app_free(ArfToolsApp* app) {
    furi_assert(app);

    view_dispatcher_remove_view(app->view_dispatcher, ArfToolsViewText);
    view_dispatcher_remove_view(app->view_dispatcher, ArfToolsViewMain);
    text_box_free(app->text_box);
    submenu_free(app->main_menu);
    view_dispatcher_free(app->view_dispatcher);

    furi_string_free(app->text);
    furi_string_free(app->file_path);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_GUI);
    free(app);
}

int32_t arf_tools_main(void* p) {
    UNUSED(p);
    ArfToolsApp* app = arf_tools_app_alloc();
    view_dispatcher_run(app->view_dispatcher);
    arf_tools_app_free(app);
    return 0;
}
