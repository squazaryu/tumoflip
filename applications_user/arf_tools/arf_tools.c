#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_box.h>
#include <storage/storage.h>
#include <toolbox/path.h>

#define TAG "ArfStatus"

#define ARF_TOOLS_ASSETS_FOLDER       EXT_PATH("apps_assets/proto_pirate")
#define ARF_TOOLS_SETTING_USER_PATH   EXT_PATH("apps_assets/proto_pirate/setting_user")
#define ARF_TOOLS_PROTO_PIRATE_PATH   EXT_PATH("apps/ARF Tools/ProtoPirate.fap")
#define ARF_TOOLS_ARF_SUBGHZ_PATH     EXT_PATH("apps/ARF Tools/ARF Sub-GHz.fap")
#define ARF_TOOLS_ARF_SUBGHZ_FULL_PATH EXT_PATH("apps/ARF Tools/ARF Sub-GHz Full.fap")
#define ARF_TOOLS_ROLLJAM_PATH        EXT_PATH("apps/ARF Tools/RollJam.fap")
#define ARF_TOOLS_SUBBRUTE_PATH       EXT_PATH("apps/ARF Tools/Sub-GHz Bruteforcer.fap")
#define ARF_STATUS_APP_PATH           EXT_PATH("apps/ARF Tools/ARF Status.fap")

typedef enum {
    ArfToolsViewMain,
    ArfToolsViewText,
} ArfToolsView;

typedef enum {
    ArfToolsMenuAssets,
    ArfToolsMenuAbout,
} ArfToolsMenu;

typedef enum {
    ArfToolsEventAssets,
    ArfToolsEventAbout,
} ArfToolsEvent;

typedef struct {
    Gui* gui;
    Storage* storage;
    ViewDispatcher* view_dispatcher;
    Submenu* main_menu;
    TextBox* text_box;
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

static void arf_tools_show_assets(ArfToolsApp* app) {
    furi_string_set_str(app->text, "ARF / ProtoPirate status\n\n");
    arf_tools_append_exists(app, "ARF Status app", ARF_STATUS_APP_PATH);
    arf_tools_append_exists(app, "ProtoPirate app", ARF_TOOLS_PROTO_PIRATE_PATH);
    arf_tools_append_exists(app, "ARF Sub-GHz app", ARF_TOOLS_ARF_SUBGHZ_PATH);
    arf_tools_append_exists(app, "ARF Sub-GHz Full", ARF_TOOLS_ARF_SUBGHZ_FULL_PATH);
    arf_tools_append_exists(app, "RollJam app", ARF_TOOLS_ROLLJAM_PATH);
    arf_tools_append_exists(app, "SubBrute app", ARF_TOOLS_SUBBRUTE_PATH);
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
        "ARF Status 0.1\n\n"
        "Diagnostic helper for tumoflip ARF deployment.\n\n"
        "Functional tools remain separate apps in /ext/apps/ARF Tools.");
}

static void arf_tools_menu_callback(void* context, uint32_t index) {
    ArfToolsApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

static bool arf_tools_custom_event_callback(void* context, uint32_t event) {
    ArfToolsApp* app = context;

    switch(event) {
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
    app->storage = furi_record_open(RECORD_STORAGE);
    app->text = furi_string_alloc();

    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher, arf_tools_custom_event_callback);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    app->main_menu = submenu_alloc();
    submenu_set_header(app->main_menu, "ARF Status");
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
    furi_record_close(RECORD_STORAGE);
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
