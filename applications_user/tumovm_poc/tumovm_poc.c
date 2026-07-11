#include "tumovm_core.h"
#include "tumovm_package.h"

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <nfc/nfc.h>
#include <nfc/nfc_listener.h>
#include <nfc/protocols/iso14443_4a/iso14443_4a.h>
#include <nfc/protocols/iso14443_4a/iso14443_4a_listener.h>
#include <storage/storage.h>
#include <toolbox/bit_buffer.h>

#include "../../applications/debug/ccid_test/ccid_usb.h"

#define TAG "TumoVmPoc"

#define TUMOVM_VIEW_MAIN     0U
#define TUMOVM_EVENT_REFRESH 1U
#define TUMOVM_REFRESH_MS    250U

// Temporary PoC identity recognized by the macOS in-box CCID driver.
#define TUMOVM_CCID_VID 0x076BU
#define TUMOVM_CCID_PID 0x3A21U

typedef struct {
    char name[TUMOVM_NAME_MAX];
    char package_status[24];
    uint8_t state_preview[8];
    uint8_t state_size;
    uint32_t apdu_count;
    uint32_t write_count;
    TumoVmTransport last_transport;
    uint8_t last_ins;
    uint16_t last_status;
    bool nfc_active;
    bool usb_active;
    bool persist_error;
    bool package_ready;
} TumoVmViewModel;

typedef struct {
    Gui* gui;
    Storage* storage;
    ViewDispatcher* view_dispatcher;
    View* view;
    FuriTimer* refresh_timer;

    FuriMutex* vm_mutex;
    TumoVm vm;
    TumoVmSession nfc_session;
    TumoVmSession usb_session;
    TumoVmPackageResult package_result;
    bool package_ready;
    bool persist_error;

    Nfc* nfc;
    NfcListener* nfc_listener;
    Iso14443_4aData* nfc_data;
    BitBuffer* nfc_tx;
    bool nfc_active;

    FuriHalUsbInterface* previous_usb;
    FuriHalUsbCcidConfig ccid_config;
    CcidCallbacks ccid_callbacks;
    bool usb_active;
} TumoVmPocApp;

static uint32_t tumovm_previous_callback(void* context) {
    UNUSED(context);
    return VIEW_NONE;
}

static void tumovm_view_draw(Canvas* canvas, void* model) {
    const TumoVmViewModel* view_model = model;
    canvas_clear(canvas);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "TumoVM PoC");

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 20, view_model->name);

    if(!view_model->package_ready) {
        canvas_draw_str(canvas, 2, 33, view_model->package_status);
        canvas_draw_str(canvas, 2, 46, "Transports not started");
        canvas_draw_str(canvas, 2, 59, "Fix SD package, reopen");
        return;
    }

    char line[48];
    snprintf(
        line,
        sizeof(line),
        "NFC:%s USB:%s %s",
        view_model->nfc_active ? "ON" : "--",
        view_model->usb_active ? "ON" : "--",
        view_model->persist_error ? "SAVE!" : "");
    canvas_draw_str(canvas, 2, 30, line);

    snprintf(
        line,
        sizeof(line),
        "%s INS:%02X SW:%04X",
        tumovm_transport_name(view_model->last_transport),
        view_model->last_ins,
        view_model->last_status);
    canvas_draw_str(canvas, 2, 40, line);

    snprintf(
        line, sizeof(line), "APDU:%lu Write:%lu", view_model->apdu_count, view_model->write_count);
    canvas_draw_str(canvas, 2, 50, line);

    snprintf(
        line,
        sizeof(line),
        "%02X %02X %02X %02X %02X %02X",
        view_model->state_preview[0],
        view_model->state_preview[1],
        view_model->state_preview[2],
        view_model->state_preview[3],
        view_model->state_preview[4],
        view_model->state_preview[5]);
    canvas_draw_str(canvas, 2, 61, line);
}

static void tumovm_refresh_model(TumoVmPocApp* app) {
    uint8_t state[TUMOVM_STATE_MAX] = {0};
    bool should_save = false;

    furi_check(furi_mutex_acquire(app->vm_mutex, FuriWaitForever) == FuriStatusOk);
    if(app->package_ready) {
        memcpy(state, app->vm.state, app->vm.program.state_size);
        should_save = app->vm.dirty;
        app->vm.dirty = false;
    }
    furi_mutex_release(app->vm_mutex);

    if(should_save) {
        app->persist_error = !tumovm_package_save_state(app->storage, &app->vm.program, state);
        if(app->persist_error) {
            furi_check(furi_mutex_acquire(app->vm_mutex, FuriWaitForever) == FuriStatusOk);
            app->vm.dirty = true;
            furi_mutex_release(app->vm_mutex);
        }
    }

    with_view_model(
        app->view,
        TumoVmViewModel * model,
        {
            strlcpy(
                model->name,
                app->package_ready ? app->vm.program.name : "Transport disabled",
                sizeof(model->name));
            strlcpy(
                model->package_status,
                tumovm_package_result_name(app->package_result),
                sizeof(model->package_status));
            model->nfc_active = app->nfc_active;
            model->usb_active = app->usb_active;
            model->persist_error = app->persist_error;
            model->package_ready = app->package_ready;
            if(app->package_ready) {
                furi_check(furi_mutex_acquire(app->vm_mutex, FuriWaitForever) == FuriStatusOk);
                model->state_size = app->vm.program.state_size;
                memcpy(
                    model->state_preview,
                    app->vm.state,
                    MIN(sizeof(model->state_preview), app->vm.program.state_size));
                model->apdu_count = app->vm.apdu_count;
                model->write_count = app->vm.write_count;
                model->last_transport = app->vm.last_transport;
                model->last_ins = app->vm.last_ins;
                model->last_status = app->vm.last_status;
                furi_mutex_release(app->vm_mutex);
            }
        },
        true);
}

static bool tumovm_custom_event_callback(void* context, uint32_t event) {
    TumoVmPocApp* app = context;
    if(event == TUMOVM_EVENT_REFRESH) {
        tumovm_refresh_model(app);
        return true;
    }
    return false;
}

static void tumovm_refresh_timer_callback(void* context) {
    TumoVmPocApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, TUMOVM_EVENT_REFRESH);
}

static size_t tumovm_process_locked(
    TumoVmPocApp* app,
    TumoVmSession* session,
    TumoVmTransport transport,
    const uint8_t* input,
    size_t input_size,
    uint8_t* output,
    size_t output_capacity) {
    furi_check(furi_mutex_acquire(app->vm_mutex, FuriWaitForever) == FuriStatusOk);
    const size_t output_size = tumovm_process_apdu(
        &app->vm, session, transport, input, input_size, output, output_capacity);
    furi_mutex_release(app->vm_mutex);
    view_dispatcher_send_custom_event(app->view_dispatcher, TUMOVM_EVENT_REFRESH);
    return output_size;
}

static NfcCommand tumovm_nfc_callback(NfcGenericEvent event, void* context) {
    TumoVmPocApp* app = context;
    furi_check(event.protocol == NfcProtocolIso14443_4a);

    Iso14443_4aListenerEvent* listener_event = event.event_data;
    if(listener_event->type == Iso14443_4aListenerEventTypeFieldOff ||
       listener_event->type == Iso14443_4aListenerEventTypeHalted) {
        furi_check(furi_mutex_acquire(app->vm_mutex, FuriWaitForever) == FuriStatusOk);
        tumovm_session_reset(&app->nfc_session);
        furi_mutex_release(app->vm_mutex);
        return listener_event->type == Iso14443_4aListenerEventTypeFieldOff ? NfcCommandSleep :
                                                                              NfcCommandContinue;
    }

    if(listener_event->type == Iso14443_4aListenerEventTypeReceivedData) {
        uint8_t response[TUMOVM_RESPONSE_MAX];
        const BitBuffer* input = listener_event->data->buffer;
        const size_t response_size = tumovm_process_locked(
            app,
            &app->nfc_session,
            TumoVmTransportNfc,
            bit_buffer_get_data(input),
            bit_buffer_get_size_bytes(input),
            response,
            sizeof(response));
        if(response_size == 0U) return NfcCommandStop;

        bit_buffer_reset(app->nfc_tx);
        bit_buffer_append_bytes(app->nfc_tx, response, response_size);
        if(iso14443_4a_listener_send_block(event.instance, app->nfc_tx) != Iso14443_4aErrorNone)
            return NfcCommandReset;
    }

    return NfcCommandContinue;
}

static void tumovm_ccid_power_on(uint8_t* atr, uint32_t* atr_size, void* context) {
    TumoVmPocApp* app = context;
    static const uint8_t tumovm_atr[] = {0x3B, 0x00};
    memcpy(atr, tumovm_atr, sizeof(tumovm_atr));
    *atr_size = sizeof(tumovm_atr);

    furi_check(furi_mutex_acquire(app->vm_mutex, FuriWaitForever) == FuriStatusOk);
    tumovm_session_reset(&app->usb_session);
    furi_mutex_release(app->vm_mutex);
}

static void tumovm_ccid_transfer(
    const uint8_t* input,
    uint32_t input_size,
    uint8_t* output,
    uint32_t* output_size,
    void* context) {
    TumoVmPocApp* app = context;
    *output_size = tumovm_process_locked(
        app,
        &app->usb_session,
        TumoVmTransportUsb,
        input,
        input_size,
        output,
        CCID_SHORT_APDU_SIZE);
}

static bool tumovm_start_nfc(TumoVmPocApp* app) {
    static const uint8_t uid[] = {0x04, 0x54, 0x56, 0x4D, 0x01, 0x00, 0x01};
    static const uint8_t historical[] = {0x80};

    app->nfc_data = iso14443_4a_alloc();
    if(!iso14443_4a_set_uid(app->nfc_data, uid, sizeof(uid))) return false;

    Iso14443_3aData* iso3 = iso14443_4a_get_base_data(app->nfc_data);
    iso3->atqa[0] = 0x44;
    iso3->atqa[1] = 0x03;
    iso3->sak = 0x20;

    Iso14443_4aAtsData* ats = &app->nfc_data->ats_data;
    ats->tl = 6;
    ats->t0 = 0x75;
    ats->ta_1 = 0x77;
    ats->tb_1 = 0x81;
    ats->tc_1 = 0x02;
    simple_array_init(ats->t1_tk, sizeof(historical));
    memcpy(simple_array_get_data(ats->t1_tk), historical, sizeof(historical));

    app->nfc = nfc_alloc();
    app->nfc_listener = nfc_listener_alloc(app->nfc, NfcProtocolIso14443_4a, app->nfc_data);
    app->nfc_tx = bit_buffer_alloc(TUMOVM_RESPONSE_MAX);
    nfc_listener_start(app->nfc_listener, tumovm_nfc_callback, app);
    app->nfc_active = true;
    return true;
}

static bool tumovm_start_usb(TumoVmPocApp* app) {
    memset(&app->ccid_config, 0, sizeof(app->ccid_config));
    app->ccid_config.vid = TUMOVM_CCID_VID;
    app->ccid_config.pid = TUMOVM_CCID_PID;
    strlcpy(app->ccid_config.manuf, "Tumowuh", sizeof(app->ccid_config.manuf));
    strlcpy(app->ccid_config.product, "TumoVM CCID", sizeof(app->ccid_config.product));

    app->ccid_callbacks.icc_power_on_callback = tumovm_ccid_power_on;
    app->ccid_callbacks.xfr_datablock_callback = tumovm_ccid_transfer;

    app->previous_usb = furi_hal_usb_get_config();
    furi_hal_usb_unlock();
    if(!furi_hal_usb_set_config(&ccid_usb_interface, &app->ccid_config)) return false;
    ccid_usb_set_callbacks(&app->ccid_callbacks, app);
    ccid_usb_insert_smartcard();
    app->usb_active = true;
    return true;
}

static void tumovm_stop_transports(TumoVmPocApp* app) {
    if(app->nfc_listener) {
        nfc_listener_stop(app->nfc_listener);
        nfc_listener_free(app->nfc_listener);
        app->nfc_listener = NULL;
    }
    if(app->nfc) {
        nfc_free(app->nfc);
        app->nfc = NULL;
    }
    if(app->nfc_tx) {
        bit_buffer_free(app->nfc_tx);
        app->nfc_tx = NULL;
    }
    if(app->nfc_data) {
        iso14443_4a_free(app->nfc_data);
        app->nfc_data = NULL;
    }
    app->nfc_active = false;

    if(app->usb_active) {
        ccid_usb_set_callbacks(NULL, NULL);
        if(!furi_hal_usb_set_config(app->previous_usb, NULL)) {
            FURI_LOG_E(TAG, "Failed to restore USB mode");
        }
        app->usb_active = false;
    }
}

static TumoVmPocApp* tumovm_app_alloc(void) {
    TumoVmPocApp* app = malloc(sizeof(*app));
    memset(app, 0, sizeof(*app));

    app->storage = furi_record_open(RECORD_STORAGE);
    app->gui = furi_record_open(RECORD_GUI);
    app->vm_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    tumovm_session_reset(&app->nfc_session);
    tumovm_session_reset(&app->usb_session);

    TumoVmProgram program;
    uint8_t state[TUMOVM_STATE_MAX];
    app->package_result = tumovm_package_load(app->storage, &program, state);
    app->package_ready = app->package_result == TumoVmPackageResultOk ||
                         app->package_result == TumoVmPackageResultCreated;
    if(app->package_ready) tumovm_init(&app->vm, &program, state);

    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, tumovm_custom_event_callback);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    app->view = view_alloc();
    view_allocate_model(app->view, ViewModelTypeLocking, sizeof(TumoVmViewModel));
    view_set_draw_callback(app->view, tumovm_view_draw);
    view_set_previous_callback(app->view, tumovm_previous_callback);
    view_dispatcher_add_view(app->view_dispatcher, TUMOVM_VIEW_MAIN, app->view);

    app->refresh_timer =
        furi_timer_alloc(tumovm_refresh_timer_callback, FuriTimerTypePeriodic, app);

    if(app->package_ready) {
        if(!tumovm_start_nfc(app)) FURI_LOG_E(TAG, "NFC start failed");
        if(!tumovm_start_usb(app)) FURI_LOG_E(TAG, "USB start failed");
    }

    tumovm_refresh_model(app);
    return app;
}

static void tumovm_app_free(TumoVmPocApp* app) {
    furi_timer_stop(app->refresh_timer);
    tumovm_stop_transports(app);
    tumovm_refresh_model(app);

    furi_timer_free(app->refresh_timer);
    view_dispatcher_remove_view(app->view_dispatcher, TUMOVM_VIEW_MAIN);
    view_free(app->view);
    view_dispatcher_free(app->view_dispatcher);

    furi_mutex_free(app->vm_mutex);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_STORAGE);
    free(app);
}

int32_t tumovm_poc_app(void* context) {
    UNUSED(context);
    TumoVmPocApp* app = tumovm_app_alloc();
    furi_timer_start(app->refresh_timer, furi_ms_to_ticks(TUMOVM_REFRESH_MS));
    view_dispatcher_switch_to_view(app->view_dispatcher, TUMOVM_VIEW_MAIN);
    view_dispatcher_run(app->view_dispatcher);
    tumovm_app_free(app);
    return 0;
}
