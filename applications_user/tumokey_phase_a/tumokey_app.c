#include <furi.h>
#include <furi_hal_usb.h>
#include <furi_hal_usb_hid_u2f.h>
#include <gui/elements.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>

#include "tumokey_core.h"

#define TAG "TumoKeyA"

typedef enum {
    TumoKeyUsbStarting,
    TumoKeyUsbWaiting,
    TumoKeyUsbConnected,
    TumoKeyUsbLocked,
    TumoKeyUsbError,
} TumoKeyUsbState;

typedef enum {
    TumoKeyWorkerStop = 1U << 0,
    TumoKeyWorkerConnect = 1U << 1,
    TumoKeyWorkerDisconnect = 1U << 2,
    TumoKeyWorkerRequest = 1U << 3,
    TumoKeyWorkerTogglePage = 1U << 4,
} TumoKeyWorkerFlag;

#define TUMOKEY_WORKER_FLAGS                                                                     \
    (TumoKeyWorkerStop | TumoKeyWorkerConnect | TumoKeyWorkerDisconnect | TumoKeyWorkerRequest | \
     TumoKeyWorkerTogglePage)

typedef struct {
    TumoKeyUsbState state;
    uint32_t requests;
    uint32_t errors;
    uint8_t last_command;
    uint8_t page;
} TumoKeyViewModel;

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    View* view;
    FuriThread* worker;
    FuriThreadId worker_id;
    FuriHalUsbInterface* previous_usb;
    TumoKeyCore core;
    TumoKeyViewModel device;
    bool usb_owned;
} TumoKeyApp;

static const char* tumokey_usb_state_name(TumoKeyUsbState state) {
    switch(state) {
    case TumoKeyUsbStarting:
        return "Starting USB";
    case TumoKeyUsbWaiting:
        return "Waiting for host";
    case TumoKeyUsbConnected:
        return "Host connected";
    case TumoKeyUsbLocked:
        return "Close RPC first";
    case TumoKeyUsbError:
        return "USB restore error";
    default:
        return "Unknown";
    }
}

static const char* tumokey_command_name(uint8_t command) {
    switch(command) {
    case TumoKeyCommandInit:
        return "INIT";
    case TumoKeyCommandPing:
        return "PING";
    case TumoKeyCommandCbor:
        return "CBOR/GetInfo";
    default:
        return "None";
    }
}

static void tumokey_refresh_view(TumoKeyApp* app) {
    app->device.requests = app->core.request_count;
    app->device.errors = app->core.error_count;
    app->device.last_command = app->core.last_command;
    with_view_model(app->view, TumoKeyViewModel * model, { *model = app->device; }, true);
}

static void tumokey_draw_callback(Canvas* canvas, void* context) {
    const TumoKeyViewModel* model = context;
    char line[32];

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 1, AlignCenter, AlignTop, "TumoKey Phase A");
    canvas_draw_rframe(canvas, 2, 13, 124, 13, 3);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 16, AlignCenter, AlignTop, "EXPERIMENTAL");

    if(model->page == 0) {
        canvas_draw_str(canvas, 3, 34, tumokey_usb_state_name(model->state));
        snprintf(
            line,
            sizeof(line),
            "Requests %lu  Errors %lu",
            (unsigned long)model->requests,
            (unsigned long)model->errors);
        canvas_draw_str(canvas, 3, 43, line);
        snprintf(line, sizeof(line), "Last: %s", tumokey_command_name(model->last_command));
        canvas_draw_str(canvas, 3, 52, line);
    } else {
        canvas_draw_str(canvas, 3, 34, "GetInfo only; no keys");
        canvas_draw_str(canvas, 3, 43, "No PIN, UV or storage");
        canvas_draw_str(canvas, 3, 52, "Debug access is unsafe");
    }

    elements_button_left(canvas, "Back");
    elements_button_right(canvas, model->page == 0 ? "About" : "Status");
}

static uint32_t tumokey_previous_callback(void* context) {
    UNUSED(context);
    return VIEW_NONE;
}

static bool tumokey_input_callback(InputEvent* event, void* context) {
    TumoKeyApp* app = context;
    if(event->type != InputTypeShort) return false;
    if(event->key == InputKeyLeft) {
        view_dispatcher_stop(app->view_dispatcher);
        return true;
    }
    if(event->key == InputKeyRight) {
        furi_thread_flags_set(app->worker_id, TumoKeyWorkerTogglePage);
        return true;
    }
    return false;
}

static void tumokey_usb_callback(HidU2fEvent event, void* context) {
    TumoKeyApp* app = context;
    uint32_t flag = 0;
    if(event == HidU2fConnected) {
        flag = TumoKeyWorkerConnect;
    } else if(event == HidU2fDisconnected) {
        flag = TumoKeyWorkerDisconnect;
    } else if(event == HidU2fRequest) {
        flag = TumoKeyWorkerRequest;
    }
    if(flag != 0) furi_thread_flags_set(app->worker_id, flag);
}

static void tumokey_send_response(const TumoKeyResponse* response) {
    uint8_t report[TumoKeyHidReportSize];
    const size_t count = tumokey_response_report_count(response);
    for(size_t index = 0; index < count; index++) {
        if(!tumokey_response_encode_report(response, index, report)) return;
        furi_hal_hid_u2f_send_response(report, sizeof(report));
    }
}

static void tumokey_process_requests(TumoKeyApp* app) {
    uint8_t report[TumoKeyHidReportSize];
    uint32_t report_size = 0;
    while((report_size = furi_hal_hid_u2f_get_request(report)) > 0) {
        TumoKeyResponse response;
        if(tumokey_core_feed(&app->core, report, report_size, furi_get_tick(), &response)) {
            tumokey_send_response(&response);
        }
    }
    tumokey_refresh_view(app);
}

static bool tumokey_start_usb(TumoKeyApp* app) {
    if(furi_hal_usb_is_locked()) {
        app->device.state = TumoKeyUsbLocked;
        tumokey_refresh_view(app);
        return false;
    }
    app->previous_usb = furi_hal_usb_get_config();
    if(!furi_hal_usb_set_config(&usb_hid_u2f, NULL)) {
        app->device.state = TumoKeyUsbError;
        tumokey_refresh_view(app);
        return false;
    }
    app->usb_owned = true;
    furi_hal_hid_u2f_set_callback(tumokey_usb_callback, app);
    app->device.state = furi_hal_hid_u2f_is_connected() ? TumoKeyUsbConnected : TumoKeyUsbWaiting;
    tumokey_refresh_view(app);
    return true;
}

static void tumokey_stop_usb(TumoKeyApp* app) {
    if(!app->usb_owned) return;
    furi_hal_hid_u2f_set_callback(NULL, NULL);
    if(!furi_hal_usb_set_config(app->previous_usb, NULL)) {
        FURI_LOG_E(TAG, "USB restore failed");
    }
    app->usb_owned = false;
}

static int32_t tumokey_worker(void* context) {
    TumoKeyApp* app = context;
    app->worker_id = furi_thread_get_current_id();
    tumokey_core_init(&app->core);
    tumokey_start_usb(app);

    bool running = true;
    while(running) {
        uint32_t flags =
            furi_thread_flags_wait(TUMOKEY_WORKER_FLAGS, FuriFlagWaitAny, furi_ms_to_ticks(100));
        if(flags == (uint32_t)FuriFlagErrorTimeout) flags = 0;
        if(flags & FuriFlagError) {
            app->device.state = TumoKeyUsbError;
            tumokey_refresh_view(app);
            break;
        }
        if(flags & TumoKeyWorkerStop) running = false;
        if(app->usb_owned && (flags & TumoKeyWorkerConnect)) {
            app->device.state = TumoKeyUsbConnected;
            tumokey_refresh_view(app);
        }
        if(app->usb_owned && (flags & TumoKeyWorkerDisconnect)) {
            tumokey_core_disconnect(&app->core);
            app->device.state = TumoKeyUsbWaiting;
            tumokey_refresh_view(app);
        }
        if(app->usb_owned && (flags & TumoKeyWorkerRequest)) tumokey_process_requests(app);
        if(flags & TumoKeyWorkerTogglePage) {
            app->device.page ^= 1;
            tumokey_refresh_view(app);
        }

        TumoKeyResponse timeout_response;
        if(app->usb_owned && tumokey_core_poll(&app->core, furi_get_tick(), &timeout_response)) {
            tumokey_send_response(&timeout_response);
            tumokey_refresh_view(app);
        }
    }

    tumokey_stop_usb(app);
    return 0;
}

static TumoKeyApp* tumokey_app_alloc(void) {
    TumoKeyApp* app = malloc(sizeof(*app));
    memset(app, 0, sizeof(*app));
    app->gui = furi_record_open(RECORD_GUI);
    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    app->view = view_alloc();
    view_allocate_model(app->view, ViewModelTypeLocking, sizeof(TumoKeyViewModel));
    view_set_context(app->view, app);
    view_set_draw_callback(app->view, tumokey_draw_callback);
    view_set_input_callback(app->view, tumokey_input_callback);
    view_set_previous_callback(app->view, tumokey_previous_callback);
    view_dispatcher_add_view(app->view_dispatcher, 0, app->view);

    app->device.state = TumoKeyUsbStarting;
    tumokey_refresh_view(app);
    app->worker = furi_thread_alloc_ex(TAG "Worker", 4 * 1024, tumokey_worker, app);
    furi_thread_start(app->worker);
    app->worker_id = furi_thread_get_id(app->worker);
    return app;
}

static void tumokey_app_free(TumoKeyApp* app) {
    if(app->worker != NULL) {
        furi_thread_flags_set(app->worker_id, TumoKeyWorkerStop);
        furi_thread_join(app->worker);
        furi_thread_free(app->worker);
    }
    view_dispatcher_remove_view(app->view_dispatcher, 0);
    view_free(app->view);
    view_dispatcher_free(app->view_dispatcher);
    furi_record_close(RECORD_GUI);
    free(app);
}

int32_t tumokey_phase_a_app(void* context) {
    UNUSED(context);
    TumoKeyApp* app = tumokey_app_alloc();
    view_dispatcher_switch_to_view(app->view_dispatcher, 0);
    view_dispatcher_run(app->view_dispatcher);
    tumokey_app_free(app);
    return 0;
}
