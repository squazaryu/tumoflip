#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/menu.h>
#include <gui/modules/menu_style.h>
#include <gui/modules/submenu.h>
#include <assets_icons.h>
#include <applications.h>
#include <archive/helpers/archive_favorites.h>

#include "loader.h"
#include "loader_menu.h"

#define TAG "LoaderMenu"
#define MODULE_ONE_MENU_NAME "8/1"
#define MODULE_ONE_APPS_PATH EXT_PATH("apps/Module One")
#define ARF_TOOLS_MENU_NAME "ARF Tools"
#define ARF_TOOLS_APPS_PATH EXT_PATH("apps/ARF Tools")

struct LoaderMenu {
    FuriThread* thread;
    void (*closed_cb)(void*);
    void* context;
    bool start_in_settings;
    ViewDispatcher* view_dispatcher;
    FuriString* pending_launch_name;
    FuriString* pending_launch_args;
};

static int32_t loader_menu_thread(void* p);

static LoaderMenu* loader_menu_alloc_internal(
    void (*closed_cb)(void*),
    void* context,
    bool start_in_settings) {
    LoaderMenu* loader_menu = malloc(sizeof(LoaderMenu));
    loader_menu->closed_cb = closed_cb;
    loader_menu->context = context;
    loader_menu->start_in_settings = start_in_settings;
    loader_menu->view_dispatcher = NULL;
    loader_menu->pending_launch_name = furi_string_alloc();
    loader_menu->pending_launch_args = furi_string_alloc();
    loader_menu->thread = furi_thread_alloc_ex(TAG, 1024, loader_menu_thread, loader_menu);
    furi_thread_start(loader_menu->thread);
    return loader_menu;
}

LoaderMenu* loader_menu_alloc(void (*closed_cb)(void*), void* context) {
    return loader_menu_alloc_internal(closed_cb, context, false);
}

LoaderMenu* loader_menu_alloc_settings(void (*closed_cb)(void*), void* context) {
    return loader_menu_alloc_internal(closed_cb, context, true);
}

void loader_menu_free(LoaderMenu* loader_menu) {
    furi_assert(loader_menu);
    furi_thread_join(loader_menu->thread);
    furi_thread_free(loader_menu->thread);
    furi_string_free(loader_menu->pending_launch_name);
    furi_string_free(loader_menu->pending_launch_args);
    free(loader_menu);
}

bool loader_menu_has_pending_launch(LoaderMenu* loader_menu) {
    furi_assert(loader_menu);
    return !furi_string_empty(loader_menu->pending_launch_name);
}

const char* loader_menu_get_pending_launch_name(LoaderMenu* loader_menu) {
    furi_assert(loader_menu);
    return furi_string_get_cstr(loader_menu->pending_launch_name);
}

const char* loader_menu_get_pending_launch_args(LoaderMenu* loader_menu) {
    furi_assert(loader_menu);
    return furi_string_empty(loader_menu->pending_launch_args) ?
               NULL :
               furi_string_get_cstr(loader_menu->pending_launch_args);
}

typedef enum {
    LoaderMenuViewPrimary,
    LoaderMenuViewSettings,
} LoaderMenuView;

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    Menu* primary_menu;
    Submenu* settings_menu;
} LoaderMenuApp;

static void loader_menu_start_with_args(LoaderMenu* loader_menu, const char* name, const char* args) {
    furi_string_set(loader_menu->pending_launch_name, name);
    if(args) {
        furi_string_set(loader_menu->pending_launch_args, args);
    } else {
        furi_string_reset(loader_menu->pending_launch_args);
    }
    if(loader_menu->view_dispatcher) {
        view_dispatcher_stop(loader_menu->view_dispatcher);
    }
}

static void loader_menu_start(LoaderMenu* loader_menu, const char* name) {
    loader_menu_start_with_args(loader_menu, name, NULL);
}

static void loader_menu_apps_callback(void* context, uint32_t index) {
    LoaderMenu* menu = context;
    const char* name = FLIPPER_APPS[index].name;
    loader_menu_start(menu, name);
}

static void loader_menu_external_apps_callback(void* context, uint32_t index) {
    LoaderMenu* menu = context;
    const char* path = FLIPPER_EXTERNAL_APPS[index].name;
    loader_menu_start(menu, path);
}

static void loader_menu_applications_callback(void* context, uint32_t index) {
    UNUSED(index);
    LoaderMenu* menu = context;
    const char* name = LOADER_APPLICATIONS_NAME;
    loader_menu_start(menu, name);
}

static void loader_menu_module_one_callback(void* context, uint32_t index) {
    UNUSED(index);
    LoaderMenu* menu = context;
    loader_menu_start_with_args(menu, LOADER_APPLICATIONS_NAME, MODULE_ONE_APPS_PATH);
}

static void loader_menu_arf_tools_callback(void* context, uint32_t index) {
    UNUSED(index);
    LoaderMenu* menu = context;
    loader_menu_start_with_args(menu, LOADER_APPLICATIONS_NAME, ARF_TOOLS_APPS_PATH);
}

static void
    loader_menu_settings_menu_callback(void* context, InputType input_type, uint32_t index) {
    LoaderMenu* loader_menu = context;
    if(input_type == InputTypeShort) {
        loader_menu_start(loader_menu, (const char*)index);
    } else if(input_type == InputTypeLong) {
        archive_favorites_handle_setting_pin_unpin((const char*)index, NULL);
    }
}

static void loader_menu_switch_to_settings(void* context, uint32_t index) {
    UNUSED(index);
    LoaderMenuApp* app = context;
    view_dispatcher_switch_to_view(app->view_dispatcher, LoaderMenuViewSettings);
}

static uint32_t loader_menu_switch_to_primary(void* context) {
    UNUSED(context);
    return LoaderMenuViewPrimary;
}

static uint32_t loader_menu_exit(void* context) {
    UNUSED(context);
    return VIEW_NONE;
}

static void loader_menu_build_menu(LoaderMenuApp* app, LoaderMenu* menu) {
    size_t i = 0;

    menu_add_item(
        app->primary_menu,
        LOADER_APPLICATIONS_NAME,
        &A_Plugins_14,
        i++,
        loader_menu_applications_callback,
        (void*)menu);
    menu_add_item(
        app->primary_menu,
        MODULE_ONE_MENU_NAME,
        &A_ModuleOne_14,
        i++,
        loader_menu_module_one_callback,
        (void*)menu);

    for(i = 0; i < FLIPPER_APPS_COUNT; i++) {
        menu_add_item(
            app->primary_menu,
            FLIPPER_APPS[i].name,
            FLIPPER_APPS[i].icon,
            i,
            loader_menu_apps_callback,
            (void*)menu);
    }

    for(i = 0; i < FLIPPER_EXTERNAL_APPS_COUNT; i++) {
        if(strcmp(FLIPPER_EXTERNAL_APPS[i].name, "Clock") == 0) {
            continue;
        }

        if(strcmp(FLIPPER_EXTERNAL_APPS[i].name, "Sub-GHz Remote") == 0) {
            menu_add_item(
                app->primary_menu,
                ARF_TOOLS_MENU_NAME,
                &A_ARFTools_14,
                i,
                loader_menu_arf_tools_callback,
                (void*)menu);
            continue;
        }

        menu_add_item(
            app->primary_menu,
            FLIPPER_EXTERNAL_APPS[i].name,
            FLIPPER_EXTERNAL_APPS[i].icon,
            i,
            loader_menu_external_apps_callback,
            (void*)menu);
    }

    menu_add_item(
        app->primary_menu, "Settings", &A_Settings_14, i++, loader_menu_switch_to_settings, app);
}

static void loader_menu_build_submenu(LoaderMenuApp* app, LoaderMenu* loader_menu) {
    for(size_t i = 0; i < FLIPPER_EXTSETTINGS_APPS_COUNT; i++) {
        submenu_add_item_ex(
            app->settings_menu,
            FLIPPER_EXTSETTINGS_APPS[i].name,
            (uint32_t)FLIPPER_EXTSETTINGS_APPS[i].name,
            loader_menu_settings_menu_callback,
            loader_menu);
    }
    for(size_t i = 0; i < FLIPPER_SETTINGS_APPS_COUNT; i++) {
        submenu_add_item_ex(
            app->settings_menu,
            FLIPPER_SETTINGS_APPS[i].name,
            (uint32_t)FLIPPER_SETTINGS_APPS[i].name,
            loader_menu_settings_menu_callback,
            loader_menu);
    }
}

static LoaderMenuApp* loader_menu_app_alloc(LoaderMenu* loader_menu) {
    LoaderMenuApp* app = malloc(sizeof(LoaderMenuApp));
    app->gui = furi_record_open(RECORD_GUI);
    app->view_dispatcher = view_dispatcher_alloc();
    app->primary_menu = menu_alloc();
    app->settings_menu = submenu_alloc();
    loader_menu->view_dispatcher = app->view_dispatcher;

    loader_menu_build_menu(app, loader_menu);
    loader_menu_build_submenu(app, loader_menu);

    // Primary menu
    View* primary_view = menu_get_view(app->primary_menu);
    view_set_context(primary_view, app->primary_menu);
    view_set_previous_callback(primary_view, loader_menu_exit);
    view_dispatcher_add_view(app->view_dispatcher, LoaderMenuViewPrimary, primary_view);

    // Settings menu
    View* settings_view = submenu_get_view(app->settings_menu);
    if(menu_style_load() == MenuStyleVertical) {
        submenu_set_orientation(app->settings_menu, ViewOrientationVertical);
    }
    view_set_context(settings_view, app->settings_menu);
    view_set_previous_callback(
        settings_view,
        loader_menu->start_in_settings ? loader_menu_exit : loader_menu_switch_to_primary);
    view_dispatcher_add_view(app->view_dispatcher, LoaderMenuViewSettings, settings_view);
    view_dispatcher_switch_to_view(
        app->view_dispatcher,
        loader_menu->start_in_settings ? LoaderMenuViewSettings : LoaderMenuViewPrimary);

    return app;
}

static void loader_menu_app_free(LoaderMenuApp* app) {
    view_dispatcher_remove_view(app->view_dispatcher, LoaderMenuViewPrimary);
    view_dispatcher_remove_view(app->view_dispatcher, LoaderMenuViewSettings);
    view_dispatcher_free(app->view_dispatcher);

    menu_free(app->primary_menu);
    submenu_free(app->settings_menu);
    furi_record_close(RECORD_GUI);
    free(app);
}

static int32_t loader_menu_thread(void* p) {
    LoaderMenu* loader_menu = p;
    furi_assert(loader_menu);

    LoaderMenuApp* app = loader_menu_app_alloc(loader_menu);

    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_run(app->view_dispatcher);

    if(loader_menu->closed_cb) {
        loader_menu->closed_cb(loader_menu->context);
    }

    loader_menu_app_free(app);

    return 0;
}
