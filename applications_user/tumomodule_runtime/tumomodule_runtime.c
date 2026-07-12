#include <furi.h>
#include <furi_hal_info.h>
#include <gui/elements.h>
#include <gui/gui.h>
#include <gui/modules/submenu.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>

#include "tumomodule_drivers.h"
#include "tumomodule_manifest_io.h"

#define TAG "TumoModule"

typedef enum {
    TumoModuleViewMenu,
    TumoModuleViewDetail,
} TumoModuleView;

typedef enum {
    TumoModuleCommandProbe,
    TumoModuleCommandStop,
} TumoModuleCommandType;

typedef struct {
    TumoModuleCommandType type;
    uint8_t index;
} TumoModuleCommand;

typedef struct {
    char name[TumoModuleNameSize];
    char adapter[TumoModuleAdapterNameSize];
    char bus[20];
    char detail[48];
    TumoModuleManifestStatus manifest_status;
    TumoModuleProbeStatus probe_status;
    uint32_t resources;
    bool about;
    bool probe_allowed;
} TumoModuleViewModel;

typedef struct {
    Gui* gui;
    Storage* storage;
    Expansion* expansion;
    ViewDispatcher* view_dispatcher;
    Submenu* submenu;
    View* detail_view;
    FuriThread* worker;
    FuriMessageQueue* commands;
    FuriMutex* mutex;
    TumoModulePackage packages[TumoModuleMaximumPackages];
    TumoModuleProbeResult results[TumoModuleMaximumPackages];
    TumoModuleResourceRegistry registry;
    size_t package_count;
    size_t selected;
} TumoModuleApp;

static void tumomodule_refresh_detail(TumoModuleApp* app) {
    TumoModuleViewModel snapshot = {0};
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    if(app->selected < app->package_count) {
        const TumoModulePackage* package = &app->packages[app->selected];
        const TumoModuleProbeResult* result = &app->results[app->selected];
        snapshot.manifest_status = package->status;
        snapshot.probe_status = result->status;
        snapshot.probe_allowed = package->status == TumoModuleManifestOk &&
                                 result->status != TumoModuleProbeRunning;
        if(package->status == TumoModuleManifestOk) {
            strlcpy(snapshot.name, package->manifest.name, sizeof(snapshot.name));
            strlcpy(
                snapshot.adapter,
                tumomodule_adapter_name(package->manifest.adapter),
                sizeof(snapshot.adapter));
            strlcpy(
                snapshot.bus, tumomodule_bus_name(package->manifest.bus), sizeof(snapshot.bus));
            snapshot.resources = package->manifest.resources;
            strlcpy(snapshot.detail, result->detail, sizeof(snapshot.detail));
        } else {
            strlcpy(snapshot.name, package->filename, sizeof(snapshot.name));
            strlcpy(snapshot.adapter, "Blocked package", sizeof(snapshot.adapter));
            strlcpy(snapshot.bus, "No hardware access", sizeof(snapshot.bus));
            strlcpy(
                snapshot.detail,
                tumomodule_manifest_status_name(package->status),
                sizeof(snapshot.detail));
        }
    } else {
        strlcpy(snapshot.name, "No module packages", sizeof(snapshot.name));
        strlcpy(snapshot.adapter, "Install FW Packages", sizeof(snapshot.adapter));
        strlcpy(snapshot.bus, TUMOMODULE_PACKAGE_DIR, sizeof(snapshot.bus));
        snapshot.manifest_status = TumoModuleManifestInvalidArgument;
    }
    furi_mutex_release(app->mutex);

    with_view_model(
        app->detail_view,
        TumoModuleViewModel * model,
        {
            const bool about = model->about;
            *model = snapshot;
            model->about = about;
        },
        true);
}

static void tumomodule_draw_detail(Canvas* canvas, void* context) {
    const TumoModuleViewModel* model = context;
    char line[48];
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 1, AlignCenter, AlignTop, "TumoModule Runtime");
    canvas_draw_rframe(canvas, 2, 13, 124, 13, 3);
    canvas_set_font(canvas, FontSecondary);
    const char* status = model->manifest_status == TumoModuleManifestOk ?
                             tumomodule_probe_status_name(model->probe_status) :
                             tumomodule_manifest_status_name(model->manifest_status);
    canvas_draw_str_aligned(canvas, 64, 16, AlignCenter, AlignTop, status);

    if(model->about) {
        canvas_draw_str(canvas, 3, 32, "Trusted adapters only");
        canvas_draw_str(canvas, 3, 41, "No native FAL loading");
        canvas_draw_str(canvas, 3, 49, "v0.1.0  github:#65");
    } else {
        canvas_draw_str(canvas, 3, 32, model->name);
        snprintf(line, sizeof(line), "%s / %s", model->adapter, model->bus);
        canvas_draw_str(canvas, 3, 41, line);
        if(model->detail[0] != '\0') {
            canvas_draw_str(canvas, 3, 49, model->detail);
        } else {
            snprintf(line, sizeof(line), "Resources %02lX", (unsigned long)model->resources);
            canvas_draw_str(canvas, 3, 49, line);
        }
    }

    elements_button_left(canvas, "Back");
    if(!model->about && model->probe_allowed) elements_button_center(canvas, "Probe");
    elements_button_right(canvas, model->about ? "Module" : "Info");
}

static uint32_t tumomodule_detail_previous(void* context) {
    UNUSED(context);
    return TumoModuleViewMenu;
}

static uint32_t tumomodule_menu_previous(void* context) {
    UNUSED(context);
    return VIEW_NONE;
}

static bool tumomodule_detail_input(InputEvent* event, void* context) {
    TumoModuleApp* app = context;
    if(event->type != InputTypeShort) return false;
    if(event->key == InputKeyLeft) {
        view_dispatcher_switch_to_view(app->view_dispatcher, TumoModuleViewMenu);
        return true;
    }
    if(event->key == InputKeyRight) {
        with_view_model(
            app->detail_view, TumoModuleViewModel * model, { model->about = !model->about; }, true);
        return true;
    }
    if(event->key != InputKeyOk) return false;

    bool allowed = false;
    size_t selected = 0;
    with_view_model(
        app->detail_view,
        TumoModuleViewModel * model,
        {
            allowed = !model->about && model->probe_allowed;
            if(allowed) {
                model->probe_status = TumoModuleProbeRunning;
                model->probe_allowed = false;
                strlcpy(model->detail, "Claiming resources...", sizeof(model->detail));
            }
        },
        true);
    if(!allowed) return false;

    furi_mutex_acquire(app->mutex, FuriWaitForever);
    selected = app->selected;
    furi_mutex_release(app->mutex);
    const TumoModuleCommand command = {
        .type = TumoModuleCommandProbe,
        .index = (uint8_t)selected,
    };
    if(furi_message_queue_put(app->commands, &command, 0) != FuriStatusOk) {
        tumomodule_refresh_detail(app);
        return false;
    }
    return true;
}

static void tumomodule_menu_callback(void* context, uint32_t index) {
    TumoModuleApp* app = context;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->selected = index;
    furi_mutex_release(app->mutex);
    with_view_model(
        app->detail_view, TumoModuleViewModel * model, { model->about = false; }, false);
    tumomodule_refresh_detail(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, TumoModuleViewDetail);
}

static int32_t tumomodule_worker(void* context) {
    TumoModuleApp* app = context;
    TumoModuleDriverContext driver_context = {.expansion = app->expansion};
    while(true) {
        TumoModuleCommand command;
        if(furi_message_queue_get(app->commands, &command, FuriWaitForever) != FuriStatusOk) {
            continue;
        }
        if(command.type == TumoModuleCommandStop) break;
        if(command.type != TumoModuleCommandProbe || command.index >= app->package_count) continue;

        const TumoModulePackage* package = &app->packages[command.index];
        TumoModuleProbeResult result = {
            .status = TumoModuleProbeError,
            .detail = "Resource conflict",
        };
        const uint32_t owner = (uint32_t)command.index + 1U;
        if(package->status == TumoModuleManifestOk &&
           tumomodule_resource_claim(&app->registry, owner, package->manifest.resources)) {
            tumomodule_driver_probe(&package->manifest, &driver_context, &result);
            if(!tumomodule_resource_release(&app->registry, owner)) {
                result.status = TumoModuleProbeError;
                strlcpy(result.detail, "Resource release failed", sizeof(result.detail));
            }
        }

        bool refresh = false;
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        app->results[command.index] = result;
        refresh = app->selected == command.index;
        furi_mutex_release(app->mutex);
        if(refresh) tumomodule_refresh_detail(app);
    }
    return 0;
}

static TumoModuleApp* tumomodule_app_alloc(void) {
    TumoModuleApp* app = malloc(sizeof(*app));
    memset(app, 0, sizeof(*app));
    app->selected = TumoModuleMaximumPackages;
    tumomodule_resource_registry_init(&app->registry);
    app->gui = furi_record_open(RECORD_GUI);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->expansion = furi_record_open(RECORD_EXPANSION);
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->commands = furi_message_queue_alloc(2, sizeof(TumoModuleCommand));

    uint16_t api_major = 0;
    uint16_t api_minor = 0;
    furi_hal_info_get_api_version(&api_major, &api_minor);
    UNUSED(api_minor);
    app->package_count =
        tumomodule_packages_load(app->storage, api_major, app->packages, COUNT_OF(app->packages));
    for(size_t index = 0; index < app->package_count; index++) {
        app->results[index].status = TumoModuleProbeIdle;
    }

    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    app->submenu = submenu_alloc();
    submenu_set_header(app->submenu, "TumoModule Runtime");
    if(app->package_count == 0) {
        submenu_add_item(
            app->submenu,
            "No module packages",
            TumoModuleMaximumPackages,
            tumomodule_menu_callback,
            app);
    } else {
        for(size_t index = 0; index < app->package_count; index++) {
            const TumoModulePackage* package = &app->packages[index];
            const char* label = package->status == TumoModuleManifestOk ? package->manifest.name :
                                                                          package->filename;
            submenu_add_item(app->submenu, label, index, tumomodule_menu_callback, app);
        }
    }
    view_set_previous_callback(submenu_get_view(app->submenu), tumomodule_menu_previous);
    view_dispatcher_add_view(
        app->view_dispatcher, TumoModuleViewMenu, submenu_get_view(app->submenu));

    app->detail_view = view_alloc();
    view_allocate_model(app->detail_view, ViewModelTypeLocking, sizeof(TumoModuleViewModel));
    view_set_context(app->detail_view, app);
    view_set_draw_callback(app->detail_view, tumomodule_draw_detail);
    view_set_input_callback(app->detail_view, tumomodule_detail_input);
    view_set_previous_callback(app->detail_view, tumomodule_detail_previous);
    view_dispatcher_add_view(app->view_dispatcher, TumoModuleViewDetail, app->detail_view);

    app->worker = furi_thread_alloc_ex(TAG "Worker", 5 * 1024, tumomodule_worker, app);
    furi_thread_start(app->worker);
    return app;
}

static void tumomodule_app_free(TumoModuleApp* app) {
    const TumoModuleCommand stop = {.type = TumoModuleCommandStop};
    furi_message_queue_put(app->commands, &stop, FuriWaitForever);
    furi_thread_join(app->worker);
    furi_thread_free(app->worker);
    view_dispatcher_remove_view(app->view_dispatcher, TumoModuleViewDetail);
    view_dispatcher_remove_view(app->view_dispatcher, TumoModuleViewMenu);
    view_free(app->detail_view);
    submenu_free(app->submenu);
    view_dispatcher_free(app->view_dispatcher);
    furi_message_queue_free(app->commands);
    furi_mutex_free(app->mutex);
    furi_record_close(RECORD_EXPANSION);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_GUI);
    free(app);
}

int32_t tumomodule_runtime_app(void* context) {
    UNUSED(context);
    TumoModuleApp* app = tumomodule_app_alloc();
    view_dispatcher_switch_to_view(app->view_dispatcher, TumoModuleViewMenu);
    view_dispatcher_run(app->view_dispatcher);
    tumomodule_app_free(app);
    return 0;
}
