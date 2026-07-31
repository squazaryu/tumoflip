// Flipper Companion: receives BLE App Bridge commands from TumoCompanion and
// exposes an on-device acceptance screen for opt-in iPhone GPS and HTTPS.
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

#define TAG                            "FlipperCompanion"
#define SUBGHZ_TX_DURATION_MS          500U
#define EMULATE_DURATION_MS            10000U
#define DEVICE_SERVICES_APP_ID         "device_services"
#define DEVICE_SERVICES_TIMEOUT_MS     12000U
#define DEVICE_SERVICES_RESPONSE_MAX   512U
#define DEVICE_SERVICES_HTTPS_TEST_URL "https://api.github.com/zen"

typedef enum {
    CompanionRequestNone,
    CompanionRequestGps,
    CompanionRequestHttps,
} CompanionRequest;

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
    uint32_t request_id_next;
    uint32_t pending_request_id;
    uint32_t request_started_at;
    CompanionRequest pending_request;
    uint8_t response_chunk_count;
    uint8_t response_next_chunk;
    size_t response_size;
    uint8_t response[DEVICE_SERVICES_RESPONSE_MAX + 1U];
    bool busy;
} Companion;

static void companion_draw_callback(Canvas* canvas, void* ctx) {
    Companion* app = ctx;
    furi_mutex_acquire(app->mutex, FuriWaitForever);

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 11, "Phone Services");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 23, app->busy ? "Requesting iPhone..." : "< GPS       HTTPS >");
    canvas_draw_line(canvas, 0, 27, 128, 27);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 40, furi_string_get_cstr(app->line1));
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 52, furi_string_get_cstr(app->line2));

    canvas_draw_str(canvas, 2, 63, "Back: Exit");

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

    const bool device_service_response = (in->protocol_version == 2U) &&
                                         ((in->flags & BtAppBridgeFlagResponse) != 0U) &&
                                         (strcmp(in->app_id, DEVICE_SERVICES_APP_ID) == 0);
    const bool legacy_command = (in->flags & BtAppBridgeFlagResponse) == 0U;
    if(!device_service_response && !legacy_command) return;

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

static uint32_t companion_next_request_id(Companion* app) {
    app->request_id_next++;
    if(app->request_id_next == 0U) app->request_id_next++;
    return app->request_id_next;
}

static void companion_finish_device_request(
    Companion* app,
    const char* line1,
    const char* line2,
    bool success) {
    companion_set_busy(app, false);
    app->pending_request = CompanionRequestNone;
    app->pending_request_id = 0U;
    app->response_chunk_count = 0U;
    app->response_next_chunk = 0U;
    app->response_size = 0U;
    companion_set_lines(app, line1, line2);
    notification_message(
        app->notifications, success ? &sequence_blink_green_100 : &sequence_blink_red_100);
}

static bool
    companion_copy_field(const char* payload, const char* key, char* output, size_t output_size) {
    if(!payload || !key || !output || (output_size == 0U)) return false;

    const size_t key_size = strlen(key);
    const char* cursor = payload;
    while(cursor && *cursor) {
        if((strncmp(cursor, key, key_size) == 0) && (cursor[key_size] == '=')) {
            const char* value = cursor + key_size + 1U;
            const char* end = strchr(value, ';');
            size_t value_size = end ? (size_t)(end - value) : strlen(value);
            if(value_size >= output_size) value_size = output_size - 1U;
            memcpy(output, value, value_size);
            output[value_size] = '\0';
            return value_size > 0U;
        }
        cursor = strchr(cursor, ';');
        if(cursor) cursor++;
    }
    return false;
}

static void companion_show_device_response(Companion* app) {
    app->response[app->response_size] = '\0';
    char line1[48];
    char line2[48];

    if(app->pending_request == CompanionRequestGps) {
        char latitude[20];
        char longitude[20];
        if(companion_copy_field((const char*)app->response, "lat", latitude, sizeof(latitude)) &&
           companion_copy_field((const char*)app->response, "lon", longitude, sizeof(longitude))) {
            snprintf(line1, sizeof(line1), "Lat %s", latitude);
            snprintf(line2, sizeof(line2), "Lon %s", longitude);
            companion_finish_device_request(app, line1, line2, true);
        } else {
            companion_finish_device_request(app, "GPS error", "Invalid response", false);
        }
        return;
    }

    const char* payload = (const char*)app->response;
    const char* body = strchr(payload, '\n');
    size_t header_size = body ? (size_t)(body - payload) : strlen(payload);
    if(header_size >= sizeof(line1)) header_size = sizeof(line1) - 1U;
    memcpy(line1, payload, header_size);
    line1[header_size] = '\0';
    if(body) body++;
    snprintf(line2, sizeof(line2), "%.38s", body ? body : "No body");
    companion_finish_device_request(app, line1, line2, true);
}

static void companion_handle_device_response(Companion* app, const BtAppBridgeEvent* event) {
    if((app->pending_request == CompanionRequestNone) ||
       (event->request_id != app->pending_request_id)) {
        return;
    }

    if((event->flags & BtAppBridgeFlagError) != 0U) {
        char error[48];
        size_t size = event->payload_len;
        if(size >= sizeof(error)) size = sizeof(error) - 1U;
        memcpy(error, event->payload, size);
        error[size] = '\0';
        companion_finish_device_request(
            app,
            app->pending_request == CompanionRequestGps ? "GPS unavailable" : "HTTPS unavailable",
            error[0] ? error : "Request failed",
            false);
        return;
    }

    if((event->chunk_count == 0U) ||
       ((app->response_chunk_count != 0U) && (app->response_chunk_count != event->chunk_count)) ||
       (event->chunk_index != app->response_next_chunk) ||
       ((app->response_size + event->payload_len) > DEVICE_SERVICES_RESPONSE_MAX)) {
        companion_finish_device_request(app, "Protocol error", "Invalid response chunks", false);
        return;
    }

    if(app->response_chunk_count == 0U) app->response_chunk_count = event->chunk_count;
    memcpy(&app->response[app->response_size], event->payload, event->payload_len);
    app->response_size += event->payload_len;
    app->response_next_chunk++;

    if(app->response_next_chunk == app->response_chunk_count) {
        companion_show_device_response(app);
    }
}

static void companion_start_device_request(Companion* app, CompanionRequest request) {
    if(app->busy || (request == CompanionRequestNone)) return;

    const uint32_t request_id = companion_next_request_id(app);
    const char* command = request == CompanionRequestGps ? "gps_once" : "https_get";
    const char* payload = request == CompanionRequestGps ? "schema=1" :
                                                           DEVICE_SERVICES_HTTPS_TEST_URL;

    app->pending_request = request;
    app->pending_request_id = request_id;
    app->request_started_at = furi_get_tick();
    app->response_chunk_count = 0U;
    app->response_next_chunk = 0U;
    app->response_size = 0U;
    companion_set_busy(app, true);
    companion_set_lines(
        app,
        request == CompanionRequestGps ? "Requesting GPS" : "Requesting HTTPS",
        request == CompanionRequestGps ? "Allow on iPhone" : "api.github.com/zen");

    const bool sent = bt_app_bridge_send_v2(
        app->bt,
        DEVICE_SERVICES_APP_ID,
        command,
        request_id,
        BtAppBridgeFlagAckRequested,
        0U,
        1U,
        (const uint8_t*)payload,
        strlen(payload));

    if(!sent) {
        companion_finish_device_request(app, "Bridge unavailable", "Enable App Bridge", false);
        return;
    }
}

static void companion_check_device_timeout(Companion* app) {
    if(app->pending_request == CompanionRequestNone) return;
    const uint32_t elapsed = furi_get_tick() - app->request_started_at;
    if(elapsed >= furi_ms_to_ticks(DEVICE_SERVICES_TIMEOUT_MS)) {
        companion_finish_device_request(app, "Request timed out", "Check TumoCompanion", false);
    }
}

// Send a status frame back to the phone (companion/<command>).
static void companion_reply(Companion* app, const char* command, const char* payload) {
    bt_app_bridge_send_text(app->bt, "companion", command, payload ? payload : "");
}

// Run a file-based action that returns ok/err; report via UI + notification + ack.
static void companion_run_file_action(
    Companion* app,
    const char* kind, // "subghz" | "nfc" | "rfid"
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

    // TumoCompanion validates its FAB3 session before answering a device-service
    // request. The runtime owns that nested hello exchange; it must not replace
    // this app's visible in-progress state with "Unknown event".
    if((app->pending_request != CompanionRequestNone) && (ev->protocol_version == 2U) &&
       (strcmp(ev->app_id, "runtime") == 0) && (strcmp(ev->command, "hello") == 0)) {
        return;
    }

    if((ev->protocol_version == 2U) && ((ev->flags & BtAppBridgeFlagResponse) != 0U) &&
       (strcmp(ev->app_id, DEVICE_SERVICES_APP_ID) == 0)) {
        companion_handle_device_response(app, ev);
        return;
    }

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
    app->line2 = furi_string_alloc_set("Left GPS / Right HTTPS");
    app->request_id_next = furi_get_tick();

    app->storage = furi_record_open(RECORD_STORAGE);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->bt = furi_record_open(RECORD_BT);
    app->bridge_pubsub = bt_app_bridge_get_pubsub(app->bt);
    app->bridge_sub = furi_pubsub_subscribe(app->bridge_pubsub, companion_bridge_callback, app);

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
        const FuriStatus status =
            furi_message_queue_get(app->queue, &event, furi_ms_to_ticks(250U));
        if(status == FuriStatusErrorTimeout) {
            companion_check_device_timeout(app);
            continue;
        }
        if(status != FuriStatusOk) continue;
        if(event.type == CompanionEvtInput) {
            if(event.input.type == InputTypeShort && event.input.key == InputKeyBack) {
                running = false;
            } else if(event.input.type == InputTypeShort && event.input.key == InputKeyLeft) {
                companion_start_device_request(app, CompanionRequestGps);
            } else if(event.input.type == InputTypeShort && event.input.key == InputKeyRight) {
                companion_start_device_request(app, CompanionRequestHttps);
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
