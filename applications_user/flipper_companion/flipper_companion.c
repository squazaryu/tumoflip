// Flipper Companion: receives BLE App Bridge commands from the UnleashedCompanion
// iOS app and acts on them on-device. Pairs with the iOS app's Bridge / Files tabs.
//
//   subghz / tx       payload = /ext/.../x.sub   -> transmit
//   nfc    / emulate  payload = /ext/.../x.nfc    -> emulate (listener)
//   rfid   / emulate  payload = /ext/.../x.rfid   -> emulate (125 kHz)
//   pager  / notify   payload = "Title|Body"      -> page + buzz
//
// On launch it announces "companion/ready" so the phone knows it can send;
// after each command it replies "companion/ack" with "ok:<id>" or "err:<msg>".

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <input/input.h>
#include <storage/storage.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>
#include <bt/bt_service/bt.h>

#include "companion_subghz.h"
#include "companion_emulate.h"

#define TAG "FlipperCompanion"
#define SUBGHZ_TX_DURATION_MS  500
#define EMULATE_DURATION_MS    10000

typedef enum {
    CompanionEvtInput,
    CompanionEvtBridge,
} CompanionEvtType;

typedef struct {
    CompanionEvtType type;
    InputEvent input;
    BtAppBridgeEvent bridge;
} CompanionEvent;

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    FuriMessageQueue* queue;
    FuriMutex* mutex;
    Bt* bt;
    FuriPubSub* bridge_pubsub;
    FuriPubSubSubscription* bridge_sub;
    Storage* storage;
    NotificationApp* notifications;

    FuriString* line1;
    FuriString* line2;
    uint32_t events_seen;
    bool busy;
} Companion;

static void companion_draw_callback(Canvas* canvas, void* ctx) {
    Companion* app = ctx;
    furi_mutex_acquire(app->mutex, FuriWaitForever);

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 11, "Companion");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 23, app->busy ? "Working..." : "Waiting for iPhone");
    canvas_draw_line(canvas, 0, 27, 128, 27);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 40, furi_string_get_cstr(app->line1));
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 52, furi_string_get_cstr(app->line2));

    char foot[32];
    snprintf(foot, sizeof(foot), "events: %lu", (unsigned long)app->events_seen);
    canvas_draw_str(canvas, 2, 63, foot);

    furi_mutex_release(app->mutex);
}

static void companion_input_callback(InputEvent* input_event, void* ctx) {
    Companion* app = ctx;
    CompanionEvent event = {.type = CompanionEvtInput, .input = *input_event};
    furi_message_queue_put(app->queue, &event, FuriWaitForever);
}

// Runs in the BT service thread — keep it light: copy + enqueue only.
static void companion_bridge_callback(const void* message, void* ctx) {
    Companion* app = ctx;
    const BtAppBridgeEvent* in = message;
    CompanionEvent event = {.type = CompanionEvtBridge};
    event.bridge = *in;
    furi_message_queue_put(app->queue, &event, 0);
}

static void companion_set_lines(Companion* app, const char* l1, const char* l2) {
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    furi_string_set_str(app->line1, l1);
    furi_string_set_str(app->line2, l2 ? l2 : "");
    furi_mutex_release(app->mutex);
    view_port_update(app->view_port);
}

static void companion_set_busy(Companion* app, bool busy) {
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->busy = busy;
    furi_mutex_release(app->mutex);
    view_port_update(app->view_port);
}

// Send a status frame back to the phone (companion/<command>).
static void companion_reply(Companion* app, const char* command, const char* payload) {
    bt_app_bridge_send_text(app->bt, "companion", command, payload ? payload : "");
}

// Run a file-based action that returns ok/err; report via UI + notification + ack.
static void companion_run_file_action(
    Companion* app,
    const char* kind,        // "subghz" | "nfc" | "rfid"
    const char* busy_label,
    bool ok,
    const FuriString* err) {
    companion_set_busy(app, false);
    if(ok) {
        notification_message(app->notifications, &sequence_blink_green_100);
        companion_set_lines(app, busy_label, "done");
        char ack[48];
        snprintf(ack, sizeof(ack), "ok:%s", kind);
        companion_reply(app, "ack", ack);
    } else {
        notification_message(app->notifications, &sequence_blink_red_100);
        companion_set_lines(app, busy_label, furi_string_get_cstr(err));
        char ack[64];
        snprintf(ack, sizeof(ack), "err:%s", furi_string_get_cstr(err));
        companion_reply(app, "ack", ack);
    }
}

static void companion_handle_bridge(Companion* app, const BtAppBridgeEvent* ev) {
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->events_seen++;
    furi_mutex_release(app->mutex);

    char payload[BT_APP_BRIDGE_PAYLOAD_LEN_MAX + 1];
    uint16_t n = ev->payload_len;
    if(n > BT_APP_BRIDGE_PAYLOAD_LEN_MAX) n = BT_APP_BRIDGE_PAYLOAD_LEN_MAX;
    memcpy(payload, ev->payload, n);
    payload[n] = '\0';

    FuriString* err = furi_string_alloc();

    if(strcmp(ev->app_id, "subghz") == 0 && strcmp(ev->command, "tx") == 0) {
        companion_set_lines(app, "Sub-GHz TX", payload);
        companion_set_busy(app, true);
        bool ok = companion_subghz_tx_file(app->storage, payload, SUBGHZ_TX_DURATION_MS, err);
        companion_run_file_action(app, "subghz", "Sub-GHz TX", ok, err);

    } else if(strcmp(ev->app_id, "nfc") == 0 && strcmp(ev->command, "emulate") == 0) {
        companion_set_lines(app, "NFC emulate", payload);
        companion_set_busy(app, true);
        bool ok = companion_nfc_emulate(payload, EMULATE_DURATION_MS, err);
        companion_run_file_action(app, "nfc", "NFC emulate", ok, err);

    } else if(strcmp(ev->app_id, "rfid") == 0 && strcmp(ev->command, "emulate") == 0) {
        companion_set_lines(app, "RFID emulate", payload);
        companion_set_busy(app, true);
        bool ok = companion_rfid_emulate(app->storage, payload, EMULATE_DURATION_MS, err);
        companion_run_file_action(app, "rfid", "RFID emulate", ok, err);

    } else if(strcmp(ev->app_id, "pager") == 0 && strcmp(ev->command, "notify") == 0) {
        char* sep = strchr(payload, '|');
        const char* title = payload;
        const char* body = "";
        if(sep) {
            *sep = '\0';
            body = sep + 1;
        }
        companion_set_lines(app, title[0] ? title : "Page", body);
        notification_message(app->notifications, &sequence_single_vibro);
        notification_message(app->notifications, &sequence_success);
        companion_reply(app, "ack", "ok:pager");

    } else if(strcmp(ev->app_id, "companion") == 0 && strcmp(ev->command, "ping") == 0) {
        companion_reply(app, "ready", "");

    } else {
        char l2[BT_APP_BRIDGE_APP_ID_LEN_MAX + BT_APP_BRIDGE_COMMAND_LEN_MAX + 4];
        snprintf(l2, sizeof(l2), "%s/%s", ev->app_id, ev->command);
        companion_set_lines(app, "Unknown event", l2);
    }

    furi_string_free(err);
}

int32_t flipper_companion_app(void* p) {
    UNUSED(p);
    Companion* app = malloc(sizeof(Companion));
    memset(app, 0, sizeof(Companion));

    app->queue = furi_message_queue_alloc(16, sizeof(CompanionEvent));
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->line1 = furi_string_alloc_set("Ready");
    app->line2 = furi_string_alloc_set("Waiting for iPhone");

    app->storage = furi_record_open(RECORD_STORAGE);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->bt = furi_record_open(RECORD_BT);
    app->bridge_pubsub = bt_app_bridge_get_pubsub(app->bt);
    app->bridge_sub =
        furi_pubsub_subscribe(app->bridge_pubsub, companion_bridge_callback, app);

    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, companion_draw_callback, app);
    view_port_input_callback_set(app->view_port, companion_input_callback, app);
    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    // Announce readiness so the phone can stop waiting and send its command.
    companion_reply(app, "ready", "");

    bool running = true;
    CompanionEvent event;
    while(running) {
        if(furi_message_queue_get(app->queue, &event, FuriWaitForever) != FuriStatusOk) continue;
        if(event.type == CompanionEvtInput) {
            if(event.input.type == InputTypeShort && event.input.key == InputKeyBack) {
                running = false;
            }
        } else if(event.type == CompanionEvtBridge) {
            companion_handle_bridge(app, &event.bridge);
        }
    }

    furi_pubsub_unsubscribe(app->bridge_pubsub, app->bridge_sub);
    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);
    furi_message_queue_free(app->queue);
    furi_mutex_free(app->mutex);
    furi_string_free(app->line1);
    furi_string_free(app->line2);

    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_BT);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_STORAGE);
    free(app);
    return 0;
}
