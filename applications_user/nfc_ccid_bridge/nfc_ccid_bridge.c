#include "nfc_ccid_bridge_core.h"

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <nfc/nfc.h>
#include <nfc/nfc_poller.h>
#include <nfc/protocols/iso14443_4a/iso14443_4a_poller.h>
#include <toolbox/bit_buffer.h>

#include "../../applications/debug/ccid_test/ccid_usb.h"

#define TAG "NfcCcidBridge"

#define NFC_CCID_VIEW_MAIN        0U
#define NFC_CCID_EVENT_REFRESH    1U
#define NFC_CCID_REFRESH_MS       200U
#define NFC_CCID_REQUEST_TIMEOUT  1500U
#define NFC_CCID_TRANSPORT_STATUS 0x6400U

// Temporary PoC identity recognized by the macOS in-box CCID driver.
#define NFC_CCID_VID 0x076BU
#define NFC_CCID_PID 0x3A21U

typedef enum {
    NfcCcidMailboxIdle,
    NfcCcidMailboxPending,
    NfcCcidMailboxProcessing,
    NfcCcidMailboxDone,
} NfcCcidMailboxState;

typedef enum {
    NfcCcidResultIdle,
    NfcCcidResultOk,
    NfcCcidResultBlocked,
    NfcCcidResultMalformed,
    NfcCcidResultNoCard,
    NfcCcidResultTimeout,
    NfcCcidResultRadio,
    NfcCcidResultBusy,
} NfcCcidResult;

typedef struct {
    bool usb_active;
    bool card_present;
    NfcCcidBridgePolicy policy;
    NfcCcidResult result;
    uint8_t cla;
    uint8_t ins;
    uint16_t request_size;
    uint16_t status;
    uint32_t request_count;
    uint32_t error_count;
} NfcCcidViewModel;

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    View* view;
    FuriTimer* refresh_timer;

    Nfc* nfc;
    NfcPoller* poller;
    BitBuffer* nfc_tx;
    BitBuffer* nfc_rx;

    FuriMutex* mutex;
    FuriSemaphore* request_done;
    NfcCcidMailboxState mailbox_state;
    bool waiter_active;
    uint8_t request[NFC_CCID_BRIDGE_APDU_MAX];
    size_t request_size;
    uint8_t response[NFC_CCID_BRIDGE_APDU_MAX];
    size_t response_size;

    bool card_present;
    bool usb_active;
    NfcCcidBridgePolicy policy;
    NfcCcidResult result;
    uint8_t last_cla;
    uint8_t last_ins;
    uint16_t last_request_size;
    uint16_t last_status;
    uint32_t request_count;
    uint32_t error_count;

    FuriHalUsbInterface* previous_usb;
    FuriHalUsbCcidConfig ccid_config;
    CcidCallbacks ccid_callbacks;
} NfcCcidBridgeApp;

static uint32_t nfc_ccid_previous_callback(void* context) {
    UNUSED(context);
    return VIEW_NONE;
}

static const char* nfc_ccid_result_name(NfcCcidResult result) {
    switch(result) {
    case NfcCcidResultOk:
        return "OK";
    case NfcCcidResultBlocked:
        return "BLOCK";
    case NfcCcidResultMalformed:
        return "LENGTH";
    case NfcCcidResultNoCard:
        return "NO CARD";
    case NfcCcidResultTimeout:
        return "TIMEOUT";
    case NfcCcidResultRadio:
        return "NFC ERR";
    case NfcCcidResultBusy:
        return "BUSY";
    default:
        return "READY";
    }
}

static void nfc_ccid_draw(Canvas* canvas, void* model) {
    const NfcCcidViewModel* view_model = model;
    canvas_clear(canvas);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "NFC CCID Bridge");

    canvas_set_font(canvas, FontSecondary);
    char line[40];
    snprintf(
        line,
        sizeof(line),
        "Card:%s USB:%s %s",
        view_model->card_present ? "ON" : "--",
        view_model->usb_active ? "ON" : "--",
        view_model->policy == NfcCcidBridgePolicyFull ? "FULL" : "RO");
    canvas_draw_str(canvas, 2, 23, line);

    snprintf(
        line,
        sizeof(line),
        "APDU %02X %02X Len:%u",
        view_model->cla,
        view_model->ins,
        view_model->request_size);
    canvas_draw_str(canvas, 2, 35, line);

    snprintf(
        line,
        sizeof(line),
        "%s SW:%04X",
        nfc_ccid_result_name(view_model->result),
        view_model->status);
    canvas_draw_str(canvas, 2, 47, line);

    snprintf(
        line, sizeof(line), "Req:%lu Err:%lu", view_model->request_count, view_model->error_count);
    canvas_draw_str(canvas, 2, 59, line);
}

static void nfc_ccid_refresh_model(NfcCcidBridgeApp* app) {
    with_view_model(
        app->view,
        NfcCcidViewModel * model,
        {
            furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
            model->usb_active = app->usb_active;
            model->card_present = app->card_present;
            model->policy = app->policy;
            model->result = app->result;
            model->cla = app->last_cla;
            model->ins = app->last_ins;
            model->request_size = app->last_request_size;
            model->status = app->last_status;
            model->request_count = app->request_count;
            model->error_count = app->error_count;
            furi_mutex_release(app->mutex);
        },
        true);
}

static bool nfc_ccid_custom_event_callback(void* context, uint32_t event) {
    NfcCcidBridgeApp* app = context;
    if(event == NFC_CCID_EVENT_REFRESH) {
        nfc_ccid_refresh_model(app);
        return true;
    }
    return false;
}

static void nfc_ccid_refresh_timer_callback(void* context) {
    NfcCcidBridgeApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, NFC_CCID_EVENT_REFRESH);
}

static bool nfc_ccid_input_callback(InputEvent* event, void* context) {
    NfcCcidBridgeApp* app = context;
    if(event->key != InputKeyOk) return false;

    bool changed = false;
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    if(event->type == InputTypeLong && app->policy == NfcCcidBridgePolicyReadOnly) {
        app->policy = NfcCcidBridgePolicyFull;
        changed = true;
    } else if(event->type == InputTypeShort && app->policy == NfcCcidBridgePolicyFull) {
        app->policy = NfcCcidBridgePolicyReadOnly;
        changed = true;
    }
    furi_mutex_release(app->mutex);
    if(changed) view_dispatcher_send_custom_event(app->view_dispatcher, NFC_CCID_EVENT_REFRESH);
    return changed;
}

static void nfc_ccid_write_status(uint8_t* output, uint32_t* output_size, uint16_t status) {
    output[0] = status >> 8U;
    output[1] = status;
    *output_size = 2U;
}

static void nfc_ccid_complete_request_locked(
    NfcCcidBridgeApp* app,
    NfcCcidResult result,
    const uint8_t* response,
    size_t response_size) {
    app->result = result;
    app->last_status = nfc_ccid_bridge_response_status(response, response_size);
    if(result != NfcCcidResultOk) app->error_count++;

    if(app->waiter_active) {
        app->response_size = MIN(response_size, sizeof(app->response));
        if(app->response_size) memcpy(app->response, response, app->response_size);
        app->mailbox_state = NfcCcidMailboxDone;
        furi_semaphore_release(app->request_done);
    } else {
        app->mailbox_state = NfcCcidMailboxIdle;
    }
}

static void nfc_ccid_fail_pending(NfcCcidBridgeApp* app, NfcCcidResult result) {
    static const uint8_t failure[] = {0x64, 0x00};

    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    if(app->mailbox_state == NfcCcidMailboxPending) {
        nfc_ccid_complete_request_locked(app, result, failure, sizeof(failure));
    }
    furi_mutex_release(app->mutex);
}

static void nfc_ccid_power_on(uint8_t* atr, uint32_t* atr_size, void* context) {
    UNUSED(context);
    static const uint8_t bridge_atr[] = {0x3B, 0x00};
    memcpy(atr, bridge_atr, sizeof(bridge_atr));
    *atr_size = sizeof(bridge_atr);
}

static void nfc_ccid_transfer(
    const uint8_t* input,
    uint32_t input_size,
    uint8_t* output,
    uint32_t* output_size,
    void* context) {
    NfcCcidBridgeApp* app = context;
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    const NfcCcidBridgePolicy policy = app->policy;
    furi_mutex_release(app->mutex);
    const NfcCcidBridgeApduDecision decision =
        nfc_ccid_bridge_check_apdu(input, input_size, policy);
    if(decision != NfcCcidBridgeApduAllowed) {
        const uint16_t status = decision == NfcCcidBridgeApduMalformed ? 0x6700U : 0x6985U;
        furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
        app->last_cla = input_size > 0U ? input[0] : 0U;
        app->last_ins = input_size > 1U ? input[1] : 0U;
        app->last_request_size = input_size;
        app->last_status = status;
        app->result = decision == NfcCcidBridgeApduMalformed ? NfcCcidResultMalformed :
                                                               NfcCcidResultBlocked;
        app->request_count++;
        app->error_count++;
        furi_mutex_release(app->mutex);
        nfc_ccid_write_status(output, output_size, status);
        return;
    }

    while(furi_semaphore_acquire(app->request_done, 0U) == FuriStatusOk) {
    }

    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    app->last_cla = input[0];
    app->last_ins = input[1];
    app->last_request_size = input_size;
    app->request_count++;

    if(!app->card_present) {
        app->result = NfcCcidResultNoCard;
        app->last_status = NFC_CCID_TRANSPORT_STATUS;
        app->error_count++;
        furi_mutex_release(app->mutex);
        nfc_ccid_write_status(output, output_size, NFC_CCID_TRANSPORT_STATUS);
        return;
    }
    if(app->mailbox_state != NfcCcidMailboxIdle) {
        app->result = NfcCcidResultBusy;
        app->last_status = NFC_CCID_TRANSPORT_STATUS;
        app->error_count++;
        furi_mutex_release(app->mutex);
        nfc_ccid_write_status(output, output_size, NFC_CCID_TRANSPORT_STATUS);
        return;
    }

    memcpy(app->request, input, input_size);
    app->request_size = input_size;
    app->response_size = 0U;
    app->waiter_active = true;
    app->mailbox_state = NfcCcidMailboxPending;
    furi_mutex_release(app->mutex);

    const FuriStatus wait_status =
        furi_semaphore_acquire(app->request_done, furi_ms_to_ticks(NFC_CCID_REQUEST_TIMEOUT));

    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    if(wait_status == FuriStatusOk && app->mailbox_state == NfcCcidMailboxDone) {
        *output_size = app->response_size;
        memcpy(output, app->response, app->response_size);
        app->waiter_active = false;
        app->mailbox_state = NfcCcidMailboxIdle;
        furi_mutex_release(app->mutex);
        return;
    }

    if(app->mailbox_state == NfcCcidMailboxDone) {
        furi_semaphore_acquire(app->request_done, 0U);
        *output_size = app->response_size;
        memcpy(output, app->response, app->response_size);
        app->waiter_active = false;
        app->mailbox_state = NfcCcidMailboxIdle;
        furi_mutex_release(app->mutex);
        return;
    }

    app->waiter_active = false;
    if(app->mailbox_state == NfcCcidMailboxPending) app->mailbox_state = NfcCcidMailboxIdle;
    app->result = NfcCcidResultTimeout;
    app->last_status = NFC_CCID_TRANSPORT_STATUS;
    app->error_count++;
    furi_mutex_release(app->mutex);
    nfc_ccid_write_status(output, output_size, NFC_CCID_TRANSPORT_STATUS);
}

static NfcCommand nfc_ccid_poller_callback(NfcGenericEvent event, void* context) {
    NfcCcidBridgeApp* app = context;
    furi_check(event.protocol == NfcProtocolIso14443_4a);

    Iso14443_4aPollerEvent* poller_event = event.event_data;
    if(poller_event->type == Iso14443_4aPollerEventTypeError) {
        bool removed = false;
        furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
        if(app->card_present) {
            app->card_present = false;
            removed = true;
        }
        furi_mutex_release(app->mutex);
        nfc_ccid_fail_pending(app, NfcCcidResultRadio);
        if(removed && app->usb_active) ccid_usb_remove_smartcard();
        return NfcCommandContinue;
    }

    bool inserted = false;
    uint8_t request[NFC_CCID_BRIDGE_APDU_MAX];
    size_t request_size = 0U;
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    if(!app->card_present) {
        app->card_present = true;
        inserted = true;
    }
    if(app->mailbox_state == NfcCcidMailboxPending) {
        request_size = app->request_size;
        memcpy(request, app->request, request_size);
        app->mailbox_state = NfcCcidMailboxProcessing;
    }
    furi_mutex_release(app->mutex);
    if(inserted && app->usb_active) ccid_usb_insert_smartcard();
    if(request_size == 0U) return NfcCommandContinue;

    bit_buffer_copy_bytes(app->nfc_tx, request, request_size);
    bit_buffer_reset(app->nfc_rx);
    const Iso14443_4aError error =
        iso14443_4a_poller_send_block(event.instance, app->nfc_tx, app->nfc_rx);

    uint8_t response[NFC_CCID_BRIDGE_APDU_MAX];
    size_t response_size = 0U;
    NfcCcidResult result = NfcCcidResultRadio;
    if(error == Iso14443_4aErrorNone) {
        response_size = MIN(bit_buffer_get_size_bytes(app->nfc_rx), sizeof(response));
        memcpy(response, bit_buffer_get_data(app->nfc_rx), response_size);
        result = NfcCcidResultOk;
    } else {
        response[0] = NFC_CCID_TRANSPORT_STATUS >> 8U;
        response[1] = (uint8_t)NFC_CCID_TRANSPORT_STATUS;
        response_size = 2U;
    }

    bool removed = false;
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    if(app->mailbox_state == NfcCcidMailboxProcessing)
        nfc_ccid_complete_request_locked(app, result, response, response_size);
    if(error != Iso14443_4aErrorNone && app->card_present) {
        app->card_present = false;
        removed = true;
    }
    furi_mutex_release(app->mutex);
    if(removed && app->usb_active) ccid_usb_remove_smartcard();
    return error == Iso14443_4aErrorNone ? NfcCommandContinue : NfcCommandReset;
}

static bool nfc_ccid_start_usb(NfcCcidBridgeApp* app) {
    memset(&app->ccid_config, 0, sizeof(app->ccid_config));
    app->ccid_config.vid = NFC_CCID_VID;
    app->ccid_config.pid = NFC_CCID_PID;
    strlcpy(app->ccid_config.manuf, "Tumowuh", sizeof(app->ccid_config.manuf));
    strlcpy(app->ccid_config.product, "NFC CCID Bridge", sizeof(app->ccid_config.product));
    app->ccid_callbacks.icc_power_on_callback = nfc_ccid_power_on;
    app->ccid_callbacks.xfr_datablock_callback = nfc_ccid_transfer;

    app->previous_usb = furi_hal_usb_get_config();
    furi_hal_usb_unlock();
    if(!furi_hal_usb_set_config(&ccid_usb_interface, &app->ccid_config)) return false;
    ccid_usb_set_callbacks(&app->ccid_callbacks, app);
    app->usb_active = true;
    return true;
}

static void nfc_ccid_start_nfc(NfcCcidBridgeApp* app) {
    app->nfc = nfc_alloc();
    app->poller = nfc_poller_alloc(app->nfc, NfcProtocolIso14443_4a);
    app->nfc_tx = bit_buffer_alloc(NFC_CCID_BRIDGE_APDU_MAX);
    app->nfc_rx = bit_buffer_alloc(NFC_CCID_BRIDGE_APDU_MAX);
    nfc_poller_start(app->poller, nfc_ccid_poller_callback, app);
}

static void nfc_ccid_stop_transports(NfcCcidBridgeApp* app) {
    nfc_ccid_fail_pending(app, NfcCcidResultTimeout);
    if(app->poller) {
        nfc_poller_stop(app->poller);
        nfc_poller_free(app->poller);
        app->poller = NULL;
    }
    if(app->nfc) {
        nfc_free(app->nfc);
        app->nfc = NULL;
    }
    if(app->nfc_tx) {
        bit_buffer_free(app->nfc_tx);
        app->nfc_tx = NULL;
    }
    if(app->nfc_rx) {
        bit_buffer_free(app->nfc_rx);
        app->nfc_rx = NULL;
    }

    if(app->usb_active) {
        ccid_usb_set_callbacks(NULL, NULL);
        if(!furi_hal_usb_set_config(app->previous_usb, NULL))
            FURI_LOG_E(TAG, "Failed to restore USB mode");
        app->usb_active = false;
    }
}

static NfcCcidBridgeApp* nfc_ccid_bridge_alloc(void) {
    NfcCcidBridgeApp* app = malloc(sizeof(*app));
    memset(app, 0, sizeof(*app));
    app->policy = NfcCcidBridgePolicyReadOnly;
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->request_done = furi_semaphore_alloc(1U, 0U);
    app->gui = furi_record_open(RECORD_GUI);

    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher, nfc_ccid_custom_event_callback);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    app->view = view_alloc();
    view_allocate_model(app->view, ViewModelTypeLocking, sizeof(NfcCcidViewModel));
    view_set_context(app->view, app);
    view_set_draw_callback(app->view, nfc_ccid_draw);
    view_set_input_callback(app->view, nfc_ccid_input_callback);
    view_set_previous_callback(app->view, nfc_ccid_previous_callback);
    view_dispatcher_add_view(app->view_dispatcher, NFC_CCID_VIEW_MAIN, app->view);

    app->refresh_timer =
        furi_timer_alloc(nfc_ccid_refresh_timer_callback, FuriTimerTypePeriodic, app);
    if(!nfc_ccid_start_usb(app)) FURI_LOG_E(TAG, "USB start failed");
    nfc_ccid_start_nfc(app);
    nfc_ccid_refresh_model(app);
    return app;
}

static void nfc_ccid_bridge_free(NfcCcidBridgeApp* app) {
    furi_timer_stop(app->refresh_timer);
    nfc_ccid_stop_transports(app);
    furi_timer_free(app->refresh_timer);

    view_dispatcher_remove_view(app->view_dispatcher, NFC_CCID_VIEW_MAIN);
    view_free(app->view);
    view_dispatcher_free(app->view_dispatcher);
    furi_record_close(RECORD_GUI);
    furi_semaphore_free(app->request_done);
    furi_mutex_free(app->mutex);
    free(app);
}

int32_t nfc_ccid_bridge_app(void* context) {
    UNUSED(context);
    NfcCcidBridgeApp* app = nfc_ccid_bridge_alloc();
    furi_timer_start(app->refresh_timer, furi_ms_to_ticks(NFC_CCID_REFRESH_MS));
    view_dispatcher_switch_to_view(app->view_dispatcher, NFC_CCID_VIEW_MAIN);
    view_dispatcher_run(app->view_dispatcher);
    nfc_ccid_bridge_free(app);
    return 0;
}
