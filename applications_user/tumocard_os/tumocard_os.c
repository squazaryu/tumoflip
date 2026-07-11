#include "tumocard_package.h"
#include "tumocard_router.h"

#include <furi.h>
#include <furi_hal.h>
#include <gui/elements.h>
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

#define TAG "TumoCardOS"

#define TUMOCARD_VIEW_MAIN      0U
#define TUMOCARD_EVENT_REFRESH  1U
#define TUMOCARD_EVENT_PREVIOUS 2U
#define TUMOCARD_EVENT_NEXT     3U
#define TUMOCARD_EVENT_TOGGLE   4U
#define TUMOCARD_REFRESH_MS     250U

// Prototype identity recognized by the macOS in-box CCID driver.
#define TUMOCARD_CCID_VID 0x076BU
#define TUMOCARD_CCID_PID 0x3A21U

typedef struct {
    char name[TUMOVM_NAME_MAX];
    char package_status[24];
    uint8_t aid[TUMOVM_AID_MAX];
    uint8_t aid_size;
    uint8_t state[4];
    uint8_t applet_count;
    uint8_t invalid_count;
    uint8_t selected_index;
    uint16_t last_status;
    bool enabled;
    bool nfc_active;
    bool usb_active;
    bool persist_error;
    bool package_ready;
} TumoCardViewModel;

typedef struct {
    Gui* gui;
    Storage* storage;
    ViewDispatcher* view_dispatcher;
    View* view;
    FuriTimer* refresh_timer;
    FuriMutex* registry_mutex;

    TumoCardRegistry registry;
    TumoCardPackageStore package_store;
    TumoCardPackageResult package_result;
    TumoCardSession nfc_session;
    TumoCardSession usb_session;
    uint8_t selected_index;
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
} TumoCardOsApp;

static uint32_t tumocard_previous_callback(void* context) {
    UNUSED(context);
    return VIEW_NONE;
}

static void tumocard_draw(Canvas* canvas, void* model) {
    const TumoCardViewModel* view_model = model;
    canvas_clear(canvas);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "TumoCard OS");
    canvas_set_font(canvas, FontSecondary);

    if(!view_model->package_ready) {
        canvas_draw_str(canvas, 2, 24, view_model->package_status);
        canvas_draw_str(canvas, 2, 38, "NFC/USB disabled");
        canvas_draw_str(canvas, 2, 52, "Fix applets on SD");
        return;
    }

    char line[40];
    snprintf(
        line,
        sizeof(line),
        "%u/%u %s %s",
        view_model->selected_index + 1U,
        view_model->applet_count,
        view_model->enabled ? "ON" : "OFF",
        view_model->name);
    canvas_draw_str(canvas, 2, 20, line);

    if(view_model->aid_size > 0U) {
        snprintf(
            line,
            sizeof(line),
            "AID:%02X%02X%02X%02X..%02X",
            view_model->aid[0],
            view_model->aid_size > 1U ? view_model->aid[1] : 0U,
            view_model->aid_size > 2U ? view_model->aid[2] : 0U,
            view_model->aid_size > 3U ? view_model->aid[3] : 0U,
            view_model->aid[view_model->aid_size - 1U]);
        canvas_draw_str(canvas, 2, 30, line);
    }

    snprintf(
        line,
        sizeof(line),
        "State:%02X%02X%02X%02X",
        view_model->state[0],
        view_model->state[1],
        view_model->state[2],
        view_model->state[3]);
    canvas_draw_str(canvas, 2, 40, line);

    snprintf(
        line,
        sizeof(line),
        "N:%s U:%s SW:%04X%s",
        view_model->nfc_active ? "ON" : "--",
        view_model->usb_active ? "ON" : "--",
        view_model->last_status,
        view_model->persist_error ? " SAVE!" : "");
    canvas_draw_str(canvas, 2, 50, line);

    if(view_model->invalid_count > 0U) {
        snprintf(line, sizeof(line), "Bad:%u", view_model->invalid_count);
        canvas_draw_str_aligned(canvas, 126, 10, AlignRight, AlignBottom, line);
    }

    elements_button_left(canvas, "Prev");
    elements_button_center(canvas, view_model->enabled ? "Disable" : "Enable");
    elements_button_right(canvas, "Next");
}

static void tumocard_persist_dirty(TumoCardOsApp* app) {
    bool attempted = false;
    bool failed = false;
    for(size_t i = 0; i < app->registry.applet_count; i++) {
        TumoCardApplet snapshot;
        bool dirty = false;
        furi_check(furi_mutex_acquire(app->registry_mutex, FuriWaitForever) == FuriStatusOk);
        if(app->registry.applets[i].vm.dirty) {
            snapshot = app->registry.applets[i];
            app->registry.applets[i].vm.dirty = false;
            dirty = true;
        }
        furi_mutex_release(app->registry_mutex);

        if(!dirty) continue;
        attempted = true;
        if(!tumocard_package_save_state(app->storage, &app->package_store, i, &snapshot)) {
            failed = true;
            furi_check(furi_mutex_acquire(app->registry_mutex, FuriWaitForever) == FuriStatusOk);
            app->registry.applets[i].vm.dirty = true;
            furi_mutex_release(app->registry_mutex);
        }
    }
    if(attempted) app->persist_error = failed;
}

static void tumocard_refresh_model(TumoCardOsApp* app) {
    if(app->package_ready) tumocard_persist_dirty(app);

    with_view_model(
        app->view,
        TumoCardViewModel * model,
        {
            memset(model, 0, sizeof(*model));
            strlcpy(
                model->package_status,
                tumocard_package_result_name(app->package_result),
                sizeof(model->package_status));
            model->package_ready = app->package_ready;
            model->nfc_active = app->nfc_active;
            model->usb_active = app->usb_active;
            model->persist_error = app->persist_error;
            if(app->package_ready) {
                furi_check(
                    furi_mutex_acquire(app->registry_mutex, FuriWaitForever) == FuriStatusOk);
                if(app->selected_index >= app->registry.applet_count) app->selected_index = 0U;
                const TumoCardApplet* applet = &app->registry.applets[app->selected_index];
                strlcpy(model->name, applet->vm.program.name, sizeof(model->name));
                model->applet_count = app->registry.applet_count;
                model->invalid_count = app->registry.invalid_count;
                model->selected_index = app->selected_index;
                model->enabled = applet->enabled;
                model->aid_size = applet->vm.program.aid_size;
                memcpy(model->aid, applet->vm.program.aid, model->aid_size);
                memcpy(
                    model->state,
                    applet->vm.state,
                    MIN(sizeof(model->state), applet->vm.program.state_size));
                model->last_status = applet->vm.last_status;
                furi_mutex_release(app->registry_mutex);
            }
        },
        true);
}

static void tumocard_select_relative(TumoCardOsApp* app, int32_t delta) {
    if(!app->package_ready || app->registry.applet_count == 0U) return;
    const int32_t count = app->registry.applet_count;
    app->selected_index = (uint8_t)((app->selected_index + count + delta) % count);
}

static void tumocard_toggle_selected(TumoCardOsApp* app) {
    if(!app->package_ready || app->selected_index >= app->registry.applet_count) return;

    furi_check(furi_mutex_acquire(app->registry_mutex, FuriWaitForever) == FuriStatusOk);
    const bool previous = app->registry.applets[app->selected_index].enabled;
    app->registry.applets[app->selected_index].enabled = !previous;
    tumocard_session_reset(&app->nfc_session);
    tumocard_session_reset(&app->usb_session);
    furi_mutex_release(app->registry_mutex);

    if(!tumocard_package_save_enabled(
           app->storage, &app->package_store, app->selected_index, !previous)) {
        furi_check(furi_mutex_acquire(app->registry_mutex, FuriWaitForever) == FuriStatusOk);
        app->registry.applets[app->selected_index].enabled = previous;
        tumocard_session_reset(&app->nfc_session);
        tumocard_session_reset(&app->usb_session);
        furi_mutex_release(app->registry_mutex);
        app->persist_error = true;
    } else {
        app->persist_error = false;
    }
}

static bool tumocard_custom_event_callback(void* context, uint32_t event) {
    TumoCardOsApp* app = context;
    switch(event) {
    case TUMOCARD_EVENT_PREVIOUS:
        tumocard_select_relative(app, -1);
        break;
    case TUMOCARD_EVENT_NEXT:
        tumocard_select_relative(app, 1);
        break;
    case TUMOCARD_EVENT_TOGGLE:
        tumocard_toggle_selected(app);
        break;
    case TUMOCARD_EVENT_REFRESH:
        break;
    default:
        return false;
    }
    tumocard_refresh_model(app);
    return true;
}

static void tumocard_refresh_timer_callback(void* context) {
    TumoCardOsApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, TUMOCARD_EVENT_REFRESH);
}

static bool tumocard_input_callback(InputEvent* event, void* context) {
    TumoCardOsApp* app = context;
    if(event->type != InputTypeShort) return false;
    uint32_t custom_event = 0U;
    if(event->key == InputKeyLeft) {
        custom_event = TUMOCARD_EVENT_PREVIOUS;
    } else if(event->key == InputKeyRight) {
        custom_event = TUMOCARD_EVENT_NEXT;
    } else if(event->key == InputKeyOk) {
        custom_event = TUMOCARD_EVENT_TOGGLE;
    }
    if(custom_event == 0U) return false;
    view_dispatcher_send_custom_event(app->view_dispatcher, custom_event);
    return true;
}

static size_t tumocard_process_locked(
    TumoCardOsApp* app,
    TumoCardSession* session,
    TumoVmTransport transport,
    const uint8_t* input,
    size_t input_size,
    uint8_t* output,
    size_t output_capacity) {
    furi_check(furi_mutex_acquire(app->registry_mutex, FuriWaitForever) == FuriStatusOk);
    const size_t output_size = tumocard_process_apdu(
        &app->registry, session, transport, input, input_size, output, output_capacity);
    furi_mutex_release(app->registry_mutex);
    view_dispatcher_send_custom_event(app->view_dispatcher, TUMOCARD_EVENT_REFRESH);
    return output_size;
}

static NfcCommand tumocard_nfc_callback(NfcGenericEvent event, void* context) {
    TumoCardOsApp* app = context;
    furi_check(event.protocol == NfcProtocolIso14443_4a);
    Iso14443_4aListenerEvent* listener_event = event.event_data;

    if(listener_event->type == Iso14443_4aListenerEventTypeFieldOff ||
       listener_event->type == Iso14443_4aListenerEventTypeHalted) {
        furi_check(furi_mutex_acquire(app->registry_mutex, FuriWaitForever) == FuriStatusOk);
        tumocard_session_reset(&app->nfc_session);
        furi_mutex_release(app->registry_mutex);
        return listener_event->type == Iso14443_4aListenerEventTypeFieldOff ? NfcCommandSleep :
                                                                              NfcCommandContinue;
    }

    if(listener_event->type == Iso14443_4aListenerEventTypeReceivedData) {
        uint8_t response[TUMOVM_RESPONSE_MAX];
        const BitBuffer* input = listener_event->data->buffer;
        const size_t response_size = tumocard_process_locked(
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

static void tumocard_ccid_power_on(uint8_t* atr, uint32_t* atr_size, void* context) {
    TumoCardOsApp* app = context;
    static const uint8_t tumocard_atr[] = {0x3B, 0x00};
    memcpy(atr, tumocard_atr, sizeof(tumocard_atr));
    *atr_size = sizeof(tumocard_atr);
    furi_check(furi_mutex_acquire(app->registry_mutex, FuriWaitForever) == FuriStatusOk);
    tumocard_session_reset(&app->usb_session);
    furi_mutex_release(app->registry_mutex);
}

static void tumocard_ccid_transfer(
    const uint8_t* input,
    uint32_t input_size,
    uint8_t* output,
    uint32_t* output_size,
    void* context) {
    TumoCardOsApp* app = context;
    *output_size = tumocard_process_locked(
        app,
        &app->usb_session,
        TumoVmTransportUsb,
        input,
        input_size,
        output,
        CCID_SHORT_APDU_SIZE);
}

static bool tumocard_start_nfc(TumoCardOsApp* app) {
    static const uint8_t uid[] = {0x04, 0x54, 0x43, 0x41, 0x52, 0x44, 0x01};
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
    nfc_listener_start(app->nfc_listener, tumocard_nfc_callback, app);
    app->nfc_active = true;
    return true;
}

static bool tumocard_start_usb(TumoCardOsApp* app) {
    memset(&app->ccid_config, 0, sizeof(app->ccid_config));
    app->ccid_config.vid = TUMOCARD_CCID_VID;
    app->ccid_config.pid = TUMOCARD_CCID_PID;
    strlcpy(app->ccid_config.manuf, "Tumowuh", sizeof(app->ccid_config.manuf));
    strlcpy(app->ccid_config.product, "TumoCard OS", sizeof(app->ccid_config.product));
    app->ccid_callbacks.icc_power_on_callback = tumocard_ccid_power_on;
    app->ccid_callbacks.xfr_datablock_callback = tumocard_ccid_transfer;

    app->previous_usb = furi_hal_usb_get_config();
    furi_hal_usb_unlock();
    if(!furi_hal_usb_set_config(&ccid_usb_interface, &app->ccid_config)) {
        furi_hal_usb_set_config(app->previous_usb, NULL);
        return false;
    }
    ccid_usb_set_callbacks(&app->ccid_callbacks, app);
    ccid_usb_insert_smartcard();
    app->usb_active = true;
    return true;
}

static void tumocard_stop_transports(TumoCardOsApp* app) {
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
        if(!furi_hal_usb_set_config(app->previous_usb, NULL))
            FURI_LOG_E(TAG, "Failed to restore USB mode");
        app->usb_active = false;
    }
}

static TumoCardOsApp* tumocard_app_alloc(void) {
    TumoCardOsApp* app = malloc(sizeof(*app));
    memset(app, 0, sizeof(*app));
    app->storage = furi_record_open(RECORD_STORAGE);
    app->gui = furi_record_open(RECORD_GUI);
    app->registry_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    tumocard_session_reset(&app->nfc_session);
    tumocard_session_reset(&app->usb_session);

    app->package_result = tumocard_package_load(app->storage, &app->registry, &app->package_store);
    app->package_ready = app->registry.applet_count > 0U;

    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher, tumocard_custom_event_callback);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    app->view = view_alloc();
    view_allocate_model(app->view, ViewModelTypeLocking, sizeof(TumoCardViewModel));
    view_set_context(app->view, app);
    view_set_draw_callback(app->view, tumocard_draw);
    view_set_input_callback(app->view, tumocard_input_callback);
    view_set_previous_callback(app->view, tumocard_previous_callback);
    view_dispatcher_add_view(app->view_dispatcher, TUMOCARD_VIEW_MAIN, app->view);

    app->refresh_timer =
        furi_timer_alloc(tumocard_refresh_timer_callback, FuriTimerTypePeriodic, app);
    if(app->package_ready) {
        if(!tumocard_start_nfc(app)) FURI_LOG_E(TAG, "NFC start failed");
        if(!tumocard_start_usb(app)) FURI_LOG_E(TAG, "USB start failed");
    }
    tumocard_refresh_model(app);
    return app;
}

static void tumocard_app_free(TumoCardOsApp* app) {
    furi_timer_stop(app->refresh_timer);
    tumocard_stop_transports(app);
    tumocard_persist_dirty(app);
    furi_timer_free(app->refresh_timer);

    view_dispatcher_remove_view(app->view_dispatcher, TUMOCARD_VIEW_MAIN);
    view_free(app->view);
    view_dispatcher_free(app->view_dispatcher);
    furi_mutex_free(app->registry_mutex);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_STORAGE);
    free(app);
}

int32_t tumocard_os_app(void* context) {
    UNUSED(context);
    TumoCardOsApp* app = tumocard_app_alloc();
    furi_timer_start(app->refresh_timer, furi_ms_to_ticks(TUMOCARD_REFRESH_MS));
    view_dispatcher_switch_to_view(app->view_dispatcher, TUMOCARD_VIEW_MAIN);
    view_dispatcher_run(app->view_dispatcher);
    tumocard_app_free(app);
    return 0;
}
