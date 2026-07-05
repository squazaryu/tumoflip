#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_box.h>
#include <storage/storage.h>
#include <toolbox/path.h>

#define TAG "ArfStatus"

#define ARF_FULL_PATH          EXT_PATH("apps/ARF Tools/arf_subghz_full.fap")
#define ARF_ANALYZER_PATH      EXT_PATH("apps/ARF Tools/arf_frequency_analyzer.fap")
#define ARF_VISUALIZER_PATH    EXT_PATH("apps/ARF Tools/protocol_visualizer.fap")
#define ARF_MODULES_PATH       EXT_PATH("apps_data/arf_subghz_full/modules/")

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
    furi_string_set_str(app->text, "ARF Full status\n\n");
    arf_tools_append_exists(app, "Full launcher", ARF_FULL_PATH);
    arf_tools_append_exists(app, "Status", ARF_MODULES_PATH "arf_status.fap");
    arf_tools_append_exists(app, "ProtoPirate", ARF_MODULES_PATH "proto_pirate.fap");
    arf_tools_append_exists(app, "KeeLoq", ARF_MODULES_PATH "arf_keeloq.fap");
    arf_tools_append_exists(app, "Counter BF", ARF_MODULES_PATH "arf_counter_bf.fap");
    arf_tools_append_exists(app, "Car Emulate", ARF_MODULES_PATH "arf_car_emulate.fap");
    arf_tools_append_exists(app, "PSA Decrypt", ARF_MODULES_PATH "arf_psa_decrypt.fap");
    arf_tools_append_exists(app, "Analyzer", ARF_ANALYZER_PATH);
    arf_tools_append_exists(app, "Protocol Visualizer", ARF_VISUALIZER_PATH);
    arf_tools_append_exists(app, "RollJam", ARF_MODULES_PATH "rolljam.fap");
    arf_tools_append_exists(
        app, "SubBrute", ARF_MODULES_PATH "subghz_bruteforcer.fap");
    arf_tools_append_exists(app, "Protocol packs", EXT_PATH("apps_data/subghz/plugins"));
    arf_tools_set_text_string(app);
}

static void arf_tools_show_about(ArfToolsApp* app) {
    arf_tools_set_text(
        app,
        "ARF Status 0.2\n\n"
        "Checks the Full launcher, visible ARF apps, and isolated modules.\n\n"
        "Full, Frequency Analyzer, and Protocol Visualizer are exposed in /ext/apps/ARF Tools.");
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
