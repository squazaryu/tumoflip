#include <furi.h>
#include <gui/gui.h>
#include <gui/modules/submenu.h>
#include <gui/view_dispatcher.h>
#include <loader/loader.h>
#include <storage/storage.h>
#include <stdio.h>
#include <stdlib.h>

#define ARF_MODULES_PATH EXT_PATH("apps_data/arf_subghz_full/modules/")

typedef struct {
    Gui* gui;
    Loader* loader;
    ViewDispatcher* view_dispatcher;
    Submenu* submenu;
} ArfSubGhzHub;

typedef struct {
    const char* label;
    const char* target;
    const char* args;
} ArfSubGhzHubItem;

static const ArfSubGhzHubItem arf_subghz_hub_items[] = {
    {.label = "Standard Sub-GHz", .target = "Sub-GHz"},
    {.label = "Frequency Analyzer", .target = "Sub-GHz", .args = "frequency_analyzer"},
    {.label = "ARF KeeLoq", .target = ARF_MODULES_PATH "arf_keeloq.fap"},
    {.label = "ARF Counter BF", .target = ARF_MODULES_PATH "arf_counter_bf.fap"},
    {.label = "ARF Car Emulate", .target = ARF_MODULES_PATH "arf_car_emulate.fap"},
    {.label = "ARF PSA Decrypt", .target = ARF_MODULES_PATH "arf_psa_decrypt.fap"},
    {.label = "ProtoPirate", .target = ARF_MODULES_PATH "proto_pirate.fap"},
    {.label = "RollJam", .target = ARF_MODULES_PATH "rolljam.fap"},
    {.label = "Sub-GHz Bruteforcer", .target = ARF_MODULES_PATH "subghz_bruteforcer.fap"},
    {.label = "ARF Status", .target = ARF_MODULES_PATH "arf_status.fap"},
};

static bool arf_subghz_hub_back_callback(void* context) {
    ArfSubGhzHub* app = context;
    view_dispatcher_stop(app->view_dispatcher);
    return true;
}

static void arf_subghz_hub_launch_callback(void* context, uint32_t index) {
    ArfSubGhzHub* app = context;
    furi_check(index < COUNT_OF(arf_subghz_hub_items));

    loader_clear_launch_queue(app->loader);
    loader_enqueue_launch(
        app->loader,
        arf_subghz_hub_items[index].target,
        arf_subghz_hub_items[index].args,
        LoaderDeferredLaunchFlagGui);

    FuriString* self_path = furi_string_alloc();
    if(loader_get_application_launch_path(app->loader, self_path)) {
        char selected_item_arg[8];
        snprintf(selected_item_arg, sizeof(selected_item_arg), "%lu", (unsigned long)index);
        loader_enqueue_launch(
            app->loader,
            furi_string_get_cstr(self_path),
            selected_item_arg,
            LoaderDeferredLaunchFlagGui);
    }
    furi_string_free(self_path);

    view_dispatcher_stop(app->view_dispatcher);
}

static ArfSubGhzHub* arf_subghz_hub_alloc(uint32_t selected_item) {
    ArfSubGhzHub* app = malloc(sizeof(ArfSubGhzHub));
    app->gui = furi_record_open(RECORD_GUI);
    app->loader = furi_record_open(RECORD_LOADER);
    app->view_dispatcher = view_dispatcher_alloc();
    app->submenu = submenu_alloc();

    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, arf_subghz_hub_back_callback);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);

    for(size_t i = 0; i < COUNT_OF(arf_subghz_hub_items); i++) {
        submenu_add_item(
            app->submenu,
            arf_subghz_hub_items[i].label,
            i,
            arf_subghz_hub_launch_callback,
            app);
    }
    submenu_set_selected_item(app->submenu, selected_item);

    view_dispatcher_add_view(app->view_dispatcher, 0, submenu_get_view(app->submenu));
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_switch_to_view(app->view_dispatcher, 0);
    return app;
}

static void arf_subghz_hub_free(ArfSubGhzHub* app) {
    view_dispatcher_remove_view(app->view_dispatcher, 0);
    submenu_free(app->submenu);
    view_dispatcher_free(app->view_dispatcher);
    furi_record_close(RECORD_LOADER);
    furi_record_close(RECORD_GUI);
    free(app);
}

int32_t arf_subghz_hub_app(void* context) {
    uint32_t selected_item = 0;
    if(context) {
        selected_item = strtoul(context, NULL, 10);
        if(selected_item >= COUNT_OF(arf_subghz_hub_items)) selected_item = 0;
    }

    ArfSubGhzHub* app = arf_subghz_hub_alloc(selected_item);
    view_dispatcher_run(app->view_dispatcher);
    arf_subghz_hub_free(app);
    return 0;
}
