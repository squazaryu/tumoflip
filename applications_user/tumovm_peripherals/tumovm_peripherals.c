#include "tumovm_peripheral_manifest.h"

#include "../tumovm_poc/tumovm_core.h"

#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_info.h>
#include <furi_hal_usb_hid.h>
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

#define TAG "TumoVmPeripheral"

#define TUMOVM_PERIPHERAL_VIEW_MAIN     0U
#define TUMOVM_PERIPHERAL_EVENT_REFRESH 1U
#define TUMOVM_PERIPHERAL_REFRESH_MS    250U

typedef struct {
    char package_name[TumoVmPeripheralNameSize];
    char status[32];
    char detail[40];
    char activity[40];
    size_t package_count;
    size_t selected_index;
    TumoVmPeripheralAdapter adapter;
    bool package_valid;
    bool active;
    bool info;
} TumoVmPeripheralViewModel;

typedef struct {
    Gui* gui;
    Storage* storage;
    ViewDispatcher* view_dispatcher;
    View* view;
    FuriTimer* refresh_timer;

    TumoVmPeripheralPackage packages[TumoVmPeripheralMaximumPackages];
    size_t package_count;
    size_t selected_index;
    int8_t active_index;
    TumoVmPeripheralResourceRegistry resources;
    bool info;
    char runtime_status[32];
    uint32_t action_count;

    FuriHalUsbInterface* previous_usb;
    FuriHalUsbHidConfig hid_config;
    volatile bool usb_connected;

    FuriMutex* vm_mutex;
    TumoVm vm;
    TumoVmSession nfc_session;
    Nfc* nfc;
    NfcListener* nfc_listener;
    Iso14443_4aData* nfc_data;
    BitBuffer* nfc_tx;
    bool nfc_active;
    bool persist_error;
} TumoVmPeripheralApp;

static const uint8_t tumovm_peripheral_routes[] = {
    0x00,
    0xA4,
    0x00,
    0x00,
    0xB0,
    0x07,
    0x00,
    0xD6,
    0x0C,
};

static const uint8_t tumovm_peripheral_bytecode[] = {
    TumoVmOpcodeCheckP1,
    0x04,
    TumoVmOpcodeCheckDataAid,
    TumoVmOpcodeSetSelected,
    TumoVmOpcodeReturnStatus,
    0x90,
    0x00,
    TumoVmOpcodeRequireSelected,
    TumoVmOpcodeReadState,
    TumoVmOpcodeReturnStatus,
    0x90,
    0x00,
    TumoVmOpcodeRequireSelected,
    TumoVmOpcodeWriteState,
    TumoVmOpcodeReturnStatus,
    0x90,
    0x00,
};

static const TumoVmPeripheralPackage* tumovm_peripheral_selected(const TumoVmPeripheralApp* app) {
    if(app->package_count == 0U || app->selected_index >= app->package_count) return NULL;
    return &app->packages[app->selected_index];
}

static const TumoVmPeripheralPackage* tumovm_peripheral_active(const TumoVmPeripheralApp* app) {
    if(app->active_index < 0 || (size_t)app->active_index >= app->package_count) return NULL;
    return &app->packages[app->active_index];
}

static uint32_t tumovm_peripheral_previous_callback(void* context) {
    UNUSED(context);
    return VIEW_NONE;
}

static void tumovm_peripheral_draw(Canvas* canvas, void* model) {
    const TumoVmPeripheralViewModel* view_model = model;
    canvas_clear(canvas);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "TumoVM Peripherals");
    canvas_draw_frame(canvas, 2, 13, 124, 14);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 23, AlignCenter, AlignBottom, view_model->status);

    if(view_model->info) {
        canvas_draw_str(canvas, 3, 39, view_model->detail);
        canvas_draw_str(canvas, 3, 50, view_model->activity);
        elements_button_left(canvas, "Stop");
        elements_button_right(canvas, "Close");
        return;
    }

    canvas_draw_str(canvas, 3, 39, view_model->package_name);
    canvas_draw_str(canvas, 3, 50, view_model->activity);

    if(view_model->active) {
        elements_button_left(canvas, "Stop");
        elements_button_center(
            canvas,
            view_model->adapter == TumoVmPeripheralAdapterUsbHidConsumer ? "Send" : "Reset");
        elements_button_right(canvas, "Info");
    } else if(view_model->package_count > 0U) {
        elements_button_left(canvas, "Prev");
        elements_button_center(canvas, view_model->package_valid ? "Start" : "Blocked");
        elements_button_right(canvas, "Next");
    } else {
        elements_button_center(canvas, "Empty");
    }
}

static void tumovm_peripheral_persist(TumoVmPeripheralApp* app) {
    const TumoVmPeripheralPackage* package = tumovm_peripheral_active(app);
    if(package == NULL || package->manifest.adapter != TumoVmPeripheralAdapterNfcType4) return;

    uint8_t state[TumoVmPeripheralStateMaximum] = {0};
    bool dirty = false;
    furi_check(furi_mutex_acquire(app->vm_mutex, FuriWaitForever) == FuriStatusOk);
    if(app->vm.dirty) {
        memcpy(state, app->vm.state, app->vm.program.state_size);
        app->vm.dirty = false;
        dirty = true;
    }
    furi_mutex_release(app->vm_mutex);

    if(dirty && !tumovm_peripheral_state_save(app->storage, &package->manifest, state)) {
        app->persist_error = true;
        furi_check(furi_mutex_acquire(app->vm_mutex, FuriWaitForever) == FuriStatusOk);
        app->vm.dirty = true;
        furi_mutex_release(app->vm_mutex);
    } else if(dirty) {
        app->persist_error = false;
    }
}

static void tumovm_peripheral_refresh_model(TumoVmPeripheralApp* app) {
    tumovm_peripheral_persist(app);
    const TumoVmPeripheralPackage* selected = tumovm_peripheral_selected(app);
    const TumoVmPeripheralPackage* active = tumovm_peripheral_active(app);

    with_view_model(
        app->view,
        TumoVmPeripheralViewModel * model,
        {
            memset(model, 0, sizeof(*model));
            model->package_count = app->package_count;
            model->selected_index = app->selected_index;
            model->active = active != NULL;
            model->info = app->info;
            if(selected == NULL) {
                strlcpy(model->status, "No SD packages", sizeof(model->status));
                strlcpy(
                    model->package_name,
                    TUMOVM_PERIPHERAL_PACKAGE_DIR,
                    sizeof(model->package_name));
                strlcpy(model->activity, "Install .tper packages", sizeof(model->activity));
            } else {
                model->package_valid = selected->status == TumoVmPeripheralManifestOk;
                model->adapter = selected->manifest.adapter;
                strlcpy(
                    model->package_name,
                    model->package_valid ? selected->manifest.name : selected->filename,
                    sizeof(model->package_name));
                if(active == NULL) {
                    snprintf(
                        model->status,
                        sizeof(model->status),
                        "%u/%u %s",
                        (unsigned)(app->selected_index + 1U),
                        (unsigned)app->package_count,
                        tumovm_peripheral_manifest_status_name(selected->status));
                    snprintf(
                        model->activity,
                        sizeof(model->activity),
                        "%s / %s",
                        tumovm_peripheral_adapter_name(selected->manifest.adapter),
                        tumovm_peripheral_action_name(selected->manifest.action));
                } else {
                    model->adapter = active->manifest.adapter;
                    strlcpy(model->status, app->runtime_status, sizeof(model->status));
                    if(active->manifest.adapter == TumoVmPeripheralAdapterUsbHidConsumer) {
                        snprintf(
                            model->activity,
                            sizeof(model->activity),
                            "%s / Sent:%lu",
                            app->usb_connected ? "Host connected" : "Waiting for host",
                            (unsigned long)app->action_count);
                    } else {
                        furi_check(
                            furi_mutex_acquire(app->vm_mutex, FuriWaitForever) == FuriStatusOk);
                        snprintf(
                            model->activity,
                            sizeof(model->activity),
                            "APDU:%lu SW:%04X%s",
                            (unsigned long)app->vm.apdu_count,
                            app->vm.last_status,
                            app->persist_error ? " SAVE!" : "");
                        furi_mutex_release(app->vm_mutex);
                    }
                    snprintf(
                        model->detail,
                        sizeof(model->detail),
                        "%s / %s",
                        tumovm_peripheral_adapter_name(active->manifest.adapter),
                        tumovm_peripheral_action_name(active->manifest.action));
                    if(app->info) {
                        strlcpy(
                            model->activity, "v0.1 / GitHub issue #67", sizeof(model->activity));
                    }
                }
            }
        },
        true);
}

static bool tumovm_peripheral_custom_event_callback(void* context, uint32_t event) {
    TumoVmPeripheralApp* app = context;
    if(event != TUMOVM_PERIPHERAL_EVENT_REFRESH) return false;
    tumovm_peripheral_refresh_model(app);
    return true;
}

static void tumovm_peripheral_refresh_timer_callback(void* context) {
    TumoVmPeripheralApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, TUMOVM_PERIPHERAL_EVENT_REFRESH);
}

static void tumovm_peripheral_usb_state_callback(bool connected, void* context) {
    TumoVmPeripheralApp* app = context;
    app->usb_connected = connected;
}

static bool tumovm_peripheral_start_usb(
    TumoVmPeripheralApp* app,
    const TumoVmPeripheralManifest* manifest) {
    app->previous_usb = furi_hal_usb_get_config();
    memset(&app->hid_config, 0, sizeof(app->hid_config));
    app->hid_config.vid = HID_VID_DEFAULT;
    app->hid_config.pid = HID_PID_DEFAULT;
    strlcpy(app->hid_config.manuf, "Tumowuh", sizeof(app->hid_config.manuf));
    strlcpy(app->hid_config.product, manifest->name, sizeof(app->hid_config.product));
    app->usb_connected = false;

    furi_hal_hid_set_state_callback(tumovm_peripheral_usb_state_callback, app);
    furi_hal_usb_unlock();
    if(!furi_hal_usb_set_config(&usb_hid, &app->hid_config)) {
        furi_hal_hid_set_state_callback(NULL, NULL);
        if(app->previous_usb != NULL) {
            furi_hal_usb_set_config(app->previous_usb, NULL);
            app->previous_usb = NULL;
        }
        return false;
    }
    strlcpy(app->runtime_status, "USB HID active", sizeof(app->runtime_status));
    return true;
}

static void tumovm_peripheral_stop_usb(TumoVmPeripheralApp* app) {
    furi_hal_hid_consumer_key_release_all();
    furi_hal_hid_kb_release_all();
    furi_hal_hid_set_state_callback(NULL, NULL);
    if(app->previous_usb != NULL && !furi_hal_usb_set_config(app->previous_usb, NULL)) {
        FURI_LOG_E(TAG, "Failed to restore USB mode");
    }
    app->usb_connected = false;
    app->previous_usb = NULL;
}

static uint16_t tumovm_peripheral_consumer_key(TumoVmPeripheralAction action) {
    switch(action) {
    case TumoVmPeripheralActionPlayPause:
        return HID_CONSUMER_PLAY_PAUSE;
    case TumoVmPeripheralActionVolumeUp:
        return HID_CONSUMER_VOLUME_INCREMENT;
    case TumoVmPeripheralActionVolumeDown:
        return HID_CONSUMER_VOLUME_DECREMENT;
    default:
        return 0U;
    }
}

static void tumovm_peripheral_send_usb_action(TumoVmPeripheralApp* app) {
    const TumoVmPeripheralPackage* package = tumovm_peripheral_active(app);
    if(package == NULL || package->manifest.adapter != TumoVmPeripheralAdapterUsbHidConsumer)
        return;
    if(!app->usb_connected) {
        strlcpy(app->runtime_status, "USB host missing", sizeof(app->runtime_status));
        return;
    }
    const uint16_t key = tumovm_peripheral_consumer_key(package->manifest.action);
    const bool pressed = key != 0U && furi_hal_hid_consumer_key_press(key);
    furi_delay_ms(20U);
    const bool released = pressed && furi_hal_hid_consumer_key_release(key);
    if(pressed && released) {
        app->action_count++;
        strlcpy(app->runtime_status, "Action sent", sizeof(app->runtime_status));
    } else {
        furi_hal_hid_consumer_key_release_all();
        strlcpy(app->runtime_status, "HID send failed", sizeof(app->runtime_status));
    }
}

static bool tumovm_peripheral_build_program(
    const TumoVmPeripheralManifest* manifest,
    TumoVmProgram* program) {
    memset(program, 0, sizeof(*program));
    strlcpy(program->name, manifest->name, sizeof(program->name));
    program->state_size = manifest->state_size;
    program->aid_size = manifest->aid_size;
    memcpy(program->aid, manifest->aid, manifest->aid_size);
    program->route_count = sizeof(tumovm_peripheral_routes) / 3U;
    for(size_t index = 0U; index < program->route_count; index++) {
        program->routes[index].cla = tumovm_peripheral_routes[index * 3U];
        program->routes[index].ins = tumovm_peripheral_routes[index * 3U + 1U];
        program->routes[index].entry = tumovm_peripheral_routes[index * 3U + 2U];
    }
    program->bytecode_size = sizeof(tumovm_peripheral_bytecode);
    memcpy(program->bytecode, tumovm_peripheral_bytecode, sizeof(tumovm_peripheral_bytecode));
    program->capability_nfc_type4 = true;
    program->capability_usb_ccid = false;
    return tumovm_program_validate(program);
}

static size_t tumovm_peripheral_process_nfc(
    TumoVmPeripheralApp* app,
    const uint8_t* input,
    size_t input_size,
    uint8_t* output,
    size_t output_capacity) {
    furi_check(furi_mutex_acquire(app->vm_mutex, FuriWaitForever) == FuriStatusOk);
    const size_t output_size = tumovm_process_apdu(
        &app->vm,
        &app->nfc_session,
        TumoVmTransportNfc,
        input,
        input_size,
        output,
        output_capacity);
    furi_mutex_release(app->vm_mutex);
    return output_size;
}

static NfcCommand tumovm_peripheral_nfc_callback(NfcGenericEvent event, void* context) {
    TumoVmPeripheralApp* app = context;
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
    if(listener_event->type != Iso14443_4aListenerEventTypeReceivedData) {
        return NfcCommandContinue;
    }

    uint8_t response[TUMOVM_RESPONSE_MAX];
    const BitBuffer* input = listener_event->data->buffer;
    const size_t response_size = tumovm_peripheral_process_nfc(
        app,
        bit_buffer_get_data(input),
        bit_buffer_get_size_bytes(input),
        response,
        sizeof(response));
    if(response_size == 0U) return NfcCommandReset;

    bit_buffer_reset(app->nfc_tx);
    bit_buffer_append_bytes(app->nfc_tx, response, response_size);
    return iso14443_4a_listener_send_block(event.instance, app->nfc_tx) == Iso14443_4aErrorNone ?
               NfcCommandContinue :
               NfcCommandReset;
}

static bool tumovm_peripheral_start_nfc(
    TumoVmPeripheralApp* app,
    const TumoVmPeripheralManifest* manifest) {
    TumoVmProgram program;
    uint8_t state[TumoVmPeripheralStateMaximum] = {0};
    if(!tumovm_peripheral_build_program(manifest, &program) ||
       !tumovm_peripheral_state_load(app->storage, manifest, state)) {
        return false;
    }
    tumovm_init(&app->vm, &program, state);
    tumovm_session_reset(&app->nfc_session);

    static const uint8_t uid[] = {0x04, 0x54, 0x56, 0x4D, 0x01, 0x00, 0x67};
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
    nfc_listener_start(app->nfc_listener, tumovm_peripheral_nfc_callback, app);
    app->nfc_active = true;
    strlcpy(app->runtime_status, "NFC listening", sizeof(app->runtime_status));
    return true;
}

static void tumovm_peripheral_stop_nfc(TumoVmPeripheralApp* app) {
    if(app->nfc_listener != NULL) {
        nfc_listener_stop(app->nfc_listener);
        nfc_listener_free(app->nfc_listener);
        app->nfc_listener = NULL;
    }
    tumovm_peripheral_persist(app);
    if(app->nfc != NULL) {
        nfc_free(app->nfc);
        app->nfc = NULL;
    }
    if(app->nfc_tx != NULL) {
        bit_buffer_free(app->nfc_tx);
        app->nfc_tx = NULL;
    }
    if(app->nfc_data != NULL) {
        iso14443_4a_free(app->nfc_data);
        app->nfc_data = NULL;
    }
    app->nfc_active = false;
}

static void tumovm_peripheral_stop(TumoVmPeripheralApp* app) {
    const TumoVmPeripheralPackage* package = tumovm_peripheral_active(app);
    if(package == NULL) return;
    if(package->manifest.adapter == TumoVmPeripheralAdapterUsbHidConsumer) {
        tumovm_peripheral_stop_usb(app);
    } else if(package->manifest.adapter == TumoVmPeripheralAdapterNfcType4) {
        tumovm_peripheral_stop_nfc(app);
    }
    tumovm_peripheral_resource_release(&app->resources, package->manifest.resources);
    app->active_index = -1;
    app->info = false;
    strlcpy(app->runtime_status, "Stopped", sizeof(app->runtime_status));
}

static void tumovm_peripheral_start_selected(TumoVmPeripheralApp* app) {
    const TumoVmPeripheralPackage* package = tumovm_peripheral_selected(app);
    if(package == NULL || package->status != TumoVmPeripheralManifestOk) return;
    if(!tumovm_peripheral_resource_claim(&app->resources, package->manifest.resources)) {
        strlcpy(app->runtime_status, "Resource busy", sizeof(app->runtime_status));
        return;
    }

    app->active_index = (int8_t)app->selected_index;
    app->action_count = 0U;
    app->persist_error = false;
    const bool started = package->manifest.adapter == TumoVmPeripheralAdapterUsbHidConsumer ?
                             tumovm_peripheral_start_usb(app, &package->manifest) :
                             tumovm_peripheral_start_nfc(app, &package->manifest);
    if(!started) {
        if(package->manifest.adapter == TumoVmPeripheralAdapterNfcType4) {
            tumovm_peripheral_stop_nfc(app);
        }
        tumovm_peripheral_resource_release(&app->resources, package->manifest.resources);
        app->active_index = -1;
        strlcpy(app->runtime_status, "Start failed", sizeof(app->runtime_status));
    }
}

static void tumovm_peripheral_reset_nfc_session(TumoVmPeripheralApp* app) {
    furi_check(furi_mutex_acquire(app->vm_mutex, FuriWaitForever) == FuriStatusOk);
    tumovm_session_reset(&app->nfc_session);
    furi_mutex_release(app->vm_mutex);
    strlcpy(app->runtime_status, "NFC session reset", sizeof(app->runtime_status));
}

static bool tumovm_peripheral_input_callback(InputEvent* event, void* context) {
    TumoVmPeripheralApp* app = context;
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return false;
    if(event->key == InputKeyBack) return false;

    const bool active = tumovm_peripheral_active(app) != NULL;
    if(active && event->type != InputTypeShort) return false;
    if(event->key == InputKeyLeft) {
        if(active) {
            tumovm_peripheral_stop(app);
        } else if(app->package_count > 0U) {
            app->selected_index =
                (app->selected_index + app->package_count - 1U) % app->package_count;
        }
    } else if(event->key == InputKeyRight) {
        if(active) {
            app->info = !app->info;
        } else if(app->package_count > 0U) {
            app->selected_index = (app->selected_index + 1U) % app->package_count;
        }
    } else if(event->key == InputKeyOk && event->type == InputTypeShort) {
        const TumoVmPeripheralPackage* package = tumovm_peripheral_active(app);
        if(package == NULL) {
            tumovm_peripheral_start_selected(app);
        } else if(package->manifest.adapter == TumoVmPeripheralAdapterUsbHidConsumer) {
            tumovm_peripheral_send_usb_action(app);
        } else {
            tumovm_peripheral_reset_nfc_session(app);
        }
    } else {
        return false;
    }
    tumovm_peripheral_refresh_model(app);
    return true;
}

static TumoVmPeripheralApp* tumovm_peripheral_app_alloc(void) {
    TumoVmPeripheralApp* app = malloc(sizeof(*app));
    memset(app, 0, sizeof(*app));
    app->active_index = -1;
    strlcpy(app->runtime_status, "Select package", sizeof(app->runtime_status));
    tumovm_peripheral_resources_init(&app->resources);

    app->storage = furi_record_open(RECORD_STORAGE);
    app->gui = furi_record_open(RECORD_GUI);
    app->vm_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    uint16_t api_major = 0U;
    uint16_t api_minor = 0U;
    furi_hal_info_get_api_version(&api_major, &api_minor);
    UNUSED(api_minor);
    app->package_count = tumovm_peripheral_packages_load(
        app->storage, api_major, app->packages, COUNT_OF(app->packages));

    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher, tumovm_peripheral_custom_event_callback);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    app->view = view_alloc();
    view_allocate_model(app->view, ViewModelTypeLocking, sizeof(TumoVmPeripheralViewModel));
    view_set_draw_callback(app->view, tumovm_peripheral_draw);
    view_set_input_callback(app->view, tumovm_peripheral_input_callback);
    view_set_context(app->view, app);
    view_set_previous_callback(app->view, tumovm_peripheral_previous_callback);
    view_dispatcher_add_view(app->view_dispatcher, TUMOVM_PERIPHERAL_VIEW_MAIN, app->view);
    app->refresh_timer =
        furi_timer_alloc(tumovm_peripheral_refresh_timer_callback, FuriTimerTypePeriodic, app);
    tumovm_peripheral_refresh_model(app);
    return app;
}

static void tumovm_peripheral_app_free(TumoVmPeripheralApp* app) {
    furi_timer_stop(app->refresh_timer);
    tumovm_peripheral_stop(app);
    furi_timer_free(app->refresh_timer);
    view_dispatcher_remove_view(app->view_dispatcher, TUMOVM_PERIPHERAL_VIEW_MAIN);
    view_free(app->view);
    view_dispatcher_free(app->view_dispatcher);
    furi_mutex_free(app->vm_mutex);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_STORAGE);
    free(app);
}

int32_t tumovm_peripherals_app(void* context) {
    UNUSED(context);
    TumoVmPeripheralApp* app = tumovm_peripheral_app_alloc();
    furi_timer_start(app->refresh_timer, furi_ms_to_ticks(TUMOVM_PERIPHERAL_REFRESH_MS));
    view_dispatcher_switch_to_view(app->view_dispatcher, TUMOVM_PERIPHERAL_VIEW_MAIN);
    view_dispatcher_run(app->view_dispatcher);
    tumovm_peripheral_app_free(app);
    return 0;
}
