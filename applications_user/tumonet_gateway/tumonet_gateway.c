#include "tumonet_gateway_protocol.h"
#include "tumonet_gateway_rf.h"

#include <bt/bt_service/bt.h>
#include <cli/cli.h>
#include <furi.h>
#include <furi_hal_rtc.h>
#include <gui/elements.h>
#include <gui/gui.h>
#include <storage/storage.h>
#include <toolbox/args.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TUMONET_GATEWAY_VERSION        "0.1.0"
#define TUMONET_GATEWAY_APP_ID         "tumonet"
#define TUMONET_GATEWAY_DATA_DIR       EXT_PATH("apps_data/tumonet")
#define TUMONET_GATEWAY_INBOX_PATH     TUMONET_GATEWAY_DATA_DIR "/inbox.csv"
#define TUMONET_GATEWAY_SEEN_PATH      TUMONET_GATEWAY_DATA_DIR "/seen.bin"
#define TUMONET_GATEWAY_SEEN_CAPACITY  16U
#define TUMONET_GATEWAY_QUEUE_CAPACITY 10U

typedef enum {
    TumoNetGatewayScreenMain,
    TumoNetGatewayScreenAbout,
} TumoNetGatewayScreen;

typedef enum {
    TumoNetGatewayEventInput,
    TumoNetGatewayEventBridge,
} TumoNetGatewayEventType;

typedef enum {
    TumoNetGatewayDeliveryDelivered,
    TumoNetGatewayDeliveryDuplicate,
    TumoNetGatewayDeliveryStopped,
    TumoNetGatewayDeliveryRfUnavailable,
    TumoNetGatewayDeliveryStorage,
    TumoNetGatewayDeliveryFailed,
} TumoNetGatewayDelivery;

typedef struct {
    uint32_t source_id;
    uint32_t message_id;
} TumoNetGatewaySeen;

typedef struct {
    TumoNetGatewayEventType type;
    InputEvent input;
    BtAppBridgeEvent bridge;
} TumoNetGatewayEvent;

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    Storage* storage;
    Bt* bt;
    FuriPubSub* bridge_pubsub;
    FuriPubSubSubscription* bridge_subscription;
    FuriMessageQueue* queue;
    FuriMutex* state_mutex;
    FuriMutex* work_mutex;
    bool cli_registered;
    bool storage_ready;
    uint8_t cli_inflight;

    TumoNetGatewayScreen screen;
    bool active;
    bool busy;
    bool shutting_down;
    volatile bool cancel_requested;
    uint32_t inbox_count;
    uint32_t duplicate_count;
    uint32_t last_source_id;
    uint32_t last_message_id;
    char last_ingress[8];
    char last_route[8];
    char last_status[24];
    char last_text[29];

    TumoNetGatewaySeen seen[TUMONET_GATEWAY_SEEN_CAPACITY];
    uint8_t seen_count;
    uint8_t seen_next;
} TumoNetGatewayApp;

static bool tumonet_gateway_write(File* file, const void* data, size_t size) {
    return size == 0U || storage_file_write(file, data, size) == size;
}

static void tumonet_gateway_write_u32(uint8_t output[4], uint32_t value) {
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8U);
    output[2] = (uint8_t)(value >> 16U);
    output[3] = (uint8_t)(value >> 24U);
}

static uint32_t tumonet_gateway_read_u32(const uint8_t input[4]) {
    return (uint32_t)input[0] | ((uint32_t)input[1] << 8U) |
           ((uint32_t)input[2] << 16U) | ((uint32_t)input[3] << 24U);
}

static void tumonet_gateway_set_status(
    TumoNetGatewayApp* app,
    const char* ingress,
    const TumoNetGatewayEnvelope* envelope,
    const char* status,
    bool busy) {
    furi_check(furi_mutex_acquire(app->state_mutex, FuriWaitForever) == FuriStatusOk);
    app->busy = busy;
    if(ingress != NULL) strlcpy(app->last_ingress, ingress, sizeof(app->last_ingress));
    if(envelope != NULL) {
        app->last_source_id = envelope->source_id;
        app->last_message_id = envelope->message_id;
        strlcpy(
            app->last_route,
            tumonet_gateway_route_name(envelope->route),
            sizeof(app->last_route));
        size_t preview_size = envelope->text_size;
        if(preview_size >= sizeof(app->last_text)) preview_size = sizeof(app->last_text) - 1U;
        while(preview_size > 0U &&
              (((uint8_t)envelope->text[preview_size] & 0xC0U) == 0x80U)) {
            preview_size--;
        }
        memcpy(app->last_text, envelope->text, preview_size);
        app->last_text[preview_size] = '\0';
    }
    if(status != NULL) strlcpy(app->last_status, status, sizeof(app->last_status));
    furi_check(furi_mutex_release(app->state_mutex) == FuriStatusOk);
    view_port_update(app->view_port);
}

static bool tumonet_gateway_seen_contains(
    const TumoNetGatewayApp* app,
    uint32_t source_id,
    uint32_t message_id) {
    for(uint8_t i = 0U; i < app->seen_count; i++) {
        if(app->seen[i].source_id == source_id && app->seen[i].message_id == message_id) {
            return true;
        }
    }
    return false;
}

static void tumonet_gateway_seen_add(
    TumoNetGatewayApp* app,
    uint32_t source_id,
    uint32_t message_id) {
    const uint8_t index = app->seen_count < TUMONET_GATEWAY_SEEN_CAPACITY ?
                              app->seen_count++ :
                              app->seen_next;
    app->seen[index].source_id = source_id;
    app->seen[index].message_id = message_id;
    app->seen_next = (uint8_t)((index + 1U) % TUMONET_GATEWAY_SEEN_CAPACITY);
}

static bool tumonet_gateway_save_seen(TumoNetGatewayApp* app) {
    if(!app->storage_ready) return false;
    File* file = storage_file_alloc(app->storage);
    bool ok = storage_file_open(file, TUMONET_GATEWAY_SEEN_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    if(ok) {
        static const uint8_t header[] = {'T', 'N', 'S', '1'};
        ok = tumonet_gateway_write(file, header, sizeof(header)) &&
             tumonet_gateway_write(file, &app->seen_count, 1U);
        for(uint8_t i = 0U; ok && i < app->seen_count; i++) {
            uint8_t record[8];
            tumonet_gateway_write_u32(&record[0], app->seen[i].source_id);
            tumonet_gateway_write_u32(&record[4], app->seen[i].message_id);
            ok = tumonet_gateway_write(file, record, sizeof(record));
        }
        ok = ok && storage_file_sync(file);
    }
    storage_file_close(file);
    storage_file_free(file);
    return ok;
}

static void tumonet_gateway_load_seen(TumoNetGatewayApp* app) {
    if(!app->storage_ready) return;
    File* file = storage_file_alloc(app->storage);
    if(storage_file_open(file, TUMONET_GATEWAY_SEEN_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        uint8_t header[5];
        if(storage_file_read(file, header, sizeof(header)) == sizeof(header) &&
           memcmp(header, "TNS1", 4U) == 0 && header[4] <= TUMONET_GATEWAY_SEEN_CAPACITY) {
            for(uint8_t i = 0U; i < header[4]; i++) {
                uint8_t record[8];
                if(storage_file_read(file, record, sizeof(record)) != sizeof(record)) break;
                const uint32_t source_id = tumonet_gateway_read_u32(&record[0]);
                const uint32_t message_id = tumonet_gateway_read_u32(&record[4]);
                if(source_id != 0U && message_id != 0U) {
                    tumonet_gateway_seen_add(app, source_id, message_id);
                }
            }
        }
    }
    storage_file_close(file);
    storage_file_free(file);
}

static uint32_t tumonet_gateway_count_inbox(TumoNetGatewayApp* app) {
    if(!app->storage_ready) return 0U;
    File* file = storage_file_alloc(app->storage);
    uint32_t line_count = 0U;
    if(storage_file_open(file, TUMONET_GATEWAY_INBOX_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        uint8_t buffer[128];
        size_t read;
        while((read = storage_file_read(file, buffer, sizeof(buffer))) > 0U) {
            for(size_t i = 0U; i < read; i++) {
                if(buffer[i] == '\n') line_count++;
            }
        }
    }
    storage_file_close(file);
    storage_file_free(file);
    return line_count > 0U ? line_count - 1U : 0U;
}

static bool tumonet_gateway_persist(
    TumoNetGatewayApp* app,
    const char* ingress,
    const TumoNetGatewayEnvelope* envelope) {
    if(!app->storage_ready) return false;
    File* file = storage_file_alloc(app->storage);
    bool ok = storage_file_open(file, TUMONET_GATEWAY_INBOX_PATH, FSAM_WRITE, FSOM_OPEN_APPEND);
    if(ok && storage_file_size(file) == 0U) {
        static const char header[] =
            "timestamp,source_id,message_id,ingress,route,status,text\n";
        ok = tumonet_gateway_write(file, header, sizeof(header) - 1U);
    }

    if(ok) {
        DateTime now;
        furi_hal_rtc_get_datetime(&now);
        char prefix[128];
        const int prefix_size = snprintf(
            prefix,
            sizeof(prefix),
            "%04u-%02u-%02uT%02u:%02u:%02u,%08lX,%08lX,%s,%s,delivered,\"",
            now.year,
            now.month,
            now.day,
            now.hour,
            now.minute,
            now.second,
            (unsigned long)envelope->source_id,
            (unsigned long)envelope->message_id,
            ingress,
            tumonet_gateway_route_name(envelope->route));
        ok = prefix_size > 0 && (size_t)prefix_size < sizeof(prefix) &&
             tumonet_gateway_write(file, prefix, (size_t)prefix_size);
        for(uint8_t i = 0U; ok && i < envelope->text_size; i++) {
            const char ch = envelope->text[i];
            if(ch == '"') ok = tumonet_gateway_write(file, "\"\"", 2U);
            else ok = tumonet_gateway_write(file, &ch, 1U);
        }
        ok = ok && tumonet_gateway_write(file, "\"\n", 2U) && storage_file_sync(file);
    }
    storage_file_close(file);
    storage_file_free(file);
    return ok;
}

static bool tumonet_gateway_rf_unavailable(TumoNetRadioResult result) {
    return result == TumoNetRadioResultExternalPower ||
           result == TumoNetRadioResultExternalDriver || result == TumoNetRadioResultNoExternal ||
           result == TumoNetRadioResultExternalBegin;
}

static TumoNetGatewayDelivery tumonet_gateway_deliver(
    TumoNetGatewayApp* app,
    const char* ingress,
    const TumoNetGatewayEnvelope* envelope) {
    furi_check(furi_mutex_acquire(app->work_mutex, FuriWaitForever) == FuriStatusOk);

    furi_check(furi_mutex_acquire(app->state_mutex, FuriWaitForever) == FuriStatusOk);
    const bool active = app->active && !app->shutting_down;
    furi_check(furi_mutex_release(app->state_mutex) == FuriStatusOk);
    if(!active) {
        tumonet_gateway_set_status(app, ingress, envelope, "Gateway stopped", false);
        furi_check(furi_mutex_release(app->work_mutex) == FuriStatusOk);
        return TumoNetGatewayDeliveryStopped;
    }

    if(tumonet_gateway_seen_contains(app, envelope->source_id, envelope->message_id)) {
        furi_check(furi_mutex_acquire(app->state_mutex, FuriWaitForever) == FuriStatusOk);
        app->duplicate_count++;
        furi_check(furi_mutex_release(app->state_mutex) == FuriStatusOk);
        tumonet_gateway_set_status(app, ingress, envelope, "Duplicate suppressed", false);
        furi_check(furi_mutex_release(app->work_mutex) == FuriStatusOk);
        return TumoNetGatewayDeliveryDuplicate;
    }

    tumonet_gateway_set_status(
        app,
        ingress,
        envelope,
        envelope->route == TumoNetGatewayRouteRf ? "Sending encrypted RF" : "Saving inbox",
        true);

    if(envelope->route == TumoNetGatewayRouteRf) {
        uint8_t encoded[TUMONET_GATEWAY_ENVELOPE_MAX];
        size_t encoded_size = 0U;
        TumoNetGatewayRfResult rf_result = {
            .radio_result = TumoNetRadioResultInit,
            .status = "ENCODE",
        };
        if(!tumonet_gateway_envelope_encode(
               envelope, encoded, sizeof(encoded), &encoded_size) ||
           !tumonet_gateway_rf_deliver(
               encoded, encoded_size, &app->cancel_requested, &rf_result)) {
            const TumoNetGatewayDelivery delivery =
                tumonet_gateway_rf_unavailable(rf_result.radio_result) ?
                    TumoNetGatewayDeliveryRfUnavailable :
                    TumoNetGatewayDeliveryFailed;
            tumonet_gateway_set_status(app, ingress, envelope, rf_result.status, false);
            furi_check(furi_mutex_release(app->work_mutex) == FuriStatusOk);
            return delivery;
        }
    }

    if(!tumonet_gateway_persist(app, ingress, envelope)) {
        tumonet_gateway_set_status(app, ingress, envelope, "Storage error", false);
        furi_check(furi_mutex_release(app->work_mutex) == FuriStatusOk);
        return TumoNetGatewayDeliveryStorage;
    }

    tumonet_gateway_seen_add(app, envelope->source_id, envelope->message_id);
    const bool seen_saved = tumonet_gateway_save_seen(app);
    furi_check(furi_mutex_acquire(app->state_mutex, FuriWaitForever) == FuriStatusOk);
    app->inbox_count++;
    furi_check(furi_mutex_release(app->state_mutex) == FuriStatusOk);
    tumonet_gateway_set_status(
        app, ingress, envelope, seen_saved ? "Delivered" : "Delivered RAM state", false);
    furi_check(furi_mutex_release(app->work_mutex) == FuriStatusOk);
    return TumoNetGatewayDeliveryDelivered;
}

static void tumonet_gateway_status_payload(
    TumoNetGatewayApp* app,
    char* output,
    size_t output_size) {
    furi_check(furi_mutex_acquire(app->state_mutex, FuriWaitForever) == FuriStatusOk);
    snprintf(
        output,
        output_size,
        "schema=1;active=%u;busy=%u;inbox=%lu;duplicates=%lu;ingress=%s;route=%s;status=%s",
        app->active ? 1U : 0U,
        app->busy ? 1U : 0U,
        (unsigned long)app->inbox_count,
        (unsigned long)app->duplicate_count,
        app->last_ingress,
        app->last_route,
        app->last_status);
    furi_check(furi_mutex_release(app->state_mutex) == FuriStatusOk);
}

static const char* tumonet_gateway_delivery_token(TumoNetGatewayDelivery delivery) {
    switch(delivery) {
    case TumoNetGatewayDeliveryDelivered:
        return "delivered";
    case TumoNetGatewayDeliveryDuplicate:
        return "duplicate";
    case TumoNetGatewayDeliveryStopped:
        return "stopped";
    case TumoNetGatewayDeliveryRfUnavailable:
        return "rf_unavailable";
    case TumoNetGatewayDeliveryStorage:
        return "storage";
    case TumoNetGatewayDeliveryFailed:
    default:
        return "delivery_failed";
    }
}

static void tumonet_gateway_draw_badge(Canvas* canvas, bool active, bool busy) {
    const char* label = busy ? "BUSY" : (active ? "ACTIVE" : "IDLE");
    const uint8_t width = busy ? 34U : (active ? 39U : 31U);
    const uint8_t x = 127U - width;
    canvas_draw_rframe(canvas, x, 1, width, 11, 2);
    if(active || busy) {
        canvas_draw_rbox(canvas, x, 1, width, 11, 2);
        canvas_set_color(canvas, ColorWhite);
    }
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, x + width / 2U, 10, AlignCenter, AlignBottom, label);
    canvas_set_color(canvas, ColorBlack);
}

static void tumonet_gateway_draw(Canvas* canvas, void* context) {
    TumoNetGatewayApp* app = context;
    furi_check(furi_mutex_acquire(app->state_mutex, FuriWaitForever) == FuriStatusOk);
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 1, 10, app->screen == TumoNetGatewayScreenAbout ? "TumoNet" : "TumoNet Gateway");

    if(app->screen == TumoNetGatewayScreenAbout) {
        canvas_draw_line(canvas, 0, 14, 127, 14);
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 2, 24, "BLE / USB / CC1101");
        canvas_draw_str(canvas, 2, 35, "v" TUMONET_GATEWAY_VERSION "  Max: 96 bytes");
        canvas_draw_str(canvas, 2, 46, "GH: squazaryu/tumoflip");
        elements_button_left(canvas, "Back");
        furi_check(furi_mutex_release(app->state_mutex) == FuriStatusOk);
        return;
    }

    tumonet_gateway_draw_badge(canvas, app->active, app->busy);
    canvas_draw_line(canvas, 0, 14, 127, 14);
    canvas_set_font(canvas, FontSecondary);
    char row[64];
    snprintf(
        row,
        sizeof(row),
        "%s > %s  I:%lu D:%lu",
        app->last_ingress,
        app->last_route,
        (unsigned long)app->inbox_count,
        (unsigned long)app->duplicate_count);
    canvas_draw_str(canvas, 2, 25, row);
    canvas_draw_str(canvas, 2, 36, app->last_status);
    canvas_draw_str(canvas, 2, 47, app->last_text);
    elements_button_left(canvas, "Back");
    elements_button_center(canvas, app->active ? "Stop" : "Start");
    elements_button_right(canvas, "Info");
    furi_check(furi_mutex_release(app->state_mutex) == FuriStatusOk);
}

static void tumonet_gateway_input_callback(InputEvent* input, void* context) {
    TumoNetGatewayApp* app = context;
    if(input->type == InputTypeShort &&
       (input->key == InputKeyBack || input->key == InputKeyLeft)) {
        furi_check(furi_mutex_acquire(app->state_mutex, FuriWaitForever) == FuriStatusOk);
        const bool exiting = app->screen == TumoNetGatewayScreenMain;
        furi_check(furi_mutex_release(app->state_mutex) == FuriStatusOk);
        if(exiting) app->cancel_requested = true;
    } else if(input->type == InputTypeShort && input->key == InputKeyOk) {
        furi_check(furi_mutex_acquire(app->state_mutex, FuriWaitForever) == FuriStatusOk);
        const bool stopping = app->screen == TumoNetGatewayScreenMain && app->active;
        furi_check(furi_mutex_release(app->state_mutex) == FuriStatusOk);
        if(stopping) app->cancel_requested = true;
    }
    TumoNetGatewayEvent event = {.type = TumoNetGatewayEventInput, .input = *input};
    furi_message_queue_put(app->queue, &event, FuriWaitForever);
}

static void tumonet_gateway_bridge_callback(const void* message, void* context) {
    TumoNetGatewayApp* app = context;
    const BtAppBridgeEvent* bridge = message;
    if(strcmp(bridge->app_id, TUMONET_GATEWAY_APP_ID) != 0 ||
       (bridge->flags & BtAppBridgeFlagResponse)) {
        return;
    }
    TumoNetGatewayEvent event = {.type = TumoNetGatewayEventBridge, .bridge = *bridge};
    furi_message_queue_put(app->queue, &event, 0U);
}

static void tumonet_gateway_bridge_reply(
    TumoNetGatewayApp* app,
    const BtAppBridgeEvent* request,
    const char* command,
    const char* payload,
    bool error) {
    const uint8_t flags = BtAppBridgeFlagResponse | (error ? BtAppBridgeFlagError : 0U);
    bt_app_bridge_send_text_v2(
        app->bt,
        TUMONET_GATEWAY_APP_ID,
        command,
        request->request_id,
        flags,
        payload);
}

static void tumonet_gateway_handle_bridge(
    TumoNetGatewayApp* app,
    const BtAppBridgeEvent* event) {
    if(event->protocol_version != 2U) {
        tumonet_gateway_bridge_reply(app, event, "error", "fab2_required", true);
        return;
    }
    if(event->chunk_count != 1U || event->chunk_index != 0U) {
        tumonet_gateway_bridge_reply(app, event, "error", "chunk", true);
        return;
    }

    if(strcmp(event->command, "capabilities") == 0) {
        tumonet_gateway_bridge_reply(
            app,
            event,
            "capabilities",
            "schema=1;max=96;routes=inbox,rf;ingress=ble,usb;rf=local_loopback",
            false);
    } else if(strcmp(event->command, "status") == 0) {
        char status[160];
        tumonet_gateway_status_payload(app, status, sizeof(status));
        tumonet_gateway_bridge_reply(app, event, "status", status, false);
    } else if(strcmp(event->command, "send") == 0) {
        TumoNetGatewayEnvelope envelope;
        if(!tumonet_gateway_envelope_decode(event->payload, event->payload_len, &envelope)) {
            tumonet_gateway_bridge_reply(app, event, "error", "payload", true);
            return;
        }
        const TumoNetGatewayDelivery delivery = tumonet_gateway_deliver(app, "BLE", &envelope);
        const char* token = tumonet_gateway_delivery_token(delivery);
        if(delivery == TumoNetGatewayDeliveryDelivered ||
           delivery == TumoNetGatewayDeliveryDuplicate) {
            char response[128];
            snprintf(
                response,
                sizeof(response),
                "schema=1;status=%s;source=%08lX;id=%08lX;route=%s",
                token,
                (unsigned long)envelope.source_id,
                (unsigned long)envelope.message_id,
                tumonet_gateway_route_name(envelope.route));
            tumonet_gateway_bridge_reply(app, event, "send", response, false);
        } else {
            tumonet_gateway_bridge_reply(app, event, "error", token, true);
        }
    } else {
        tumonet_gateway_bridge_reply(app, event, "error", "unsupported_command", true);
    }
}

static bool tumonet_gateway_parse_hex32(const char* text, uint32_t* value) {
    if(text == NULL || value == NULL || text[0] == '\0' || strlen(text) > 8U) return false;
    for(const char* cursor = text; *cursor != '\0'; cursor++) {
        if(!isxdigit((unsigned char)*cursor)) return false;
    }
    char* end = NULL;
    const unsigned long parsed = strtoul(text, &end, 16);
    if(end == NULL || *end != '\0' || parsed == 0UL || parsed > UINT32_MAX) return false;
    *value = (uint32_t)parsed;
    return true;
}

static void tumonet_gateway_cli(PipeSide* pipe, FuriString* args, void* context) {
    UNUSED(pipe);
    TumoNetGatewayApp* app = context;
    furi_check(furi_mutex_acquire(app->state_mutex, FuriWaitForever) == FuriStatusOk);
    if(app->shutting_down) {
        furi_check(furi_mutex_release(app->state_mutex) == FuriStatusOk);
        printf("ERROR gateway closing\r\n");
        return;
    }
    app->cli_inflight++;
    furi_check(furi_mutex_release(app->state_mutex) == FuriStatusOk);

    FuriString* command = furi_string_alloc();
    if(!args_read_string_and_trim(args, command)) {
        printf("Usage: tumonet caps|status|send <source_hex> <message_hex> <inbox|rf> <text>\r\n");
    } else if(furi_string_cmp_str(command, "caps") == 0) {
        printf("schema=1;max=96;routes=inbox,rf;ingress=ble,usb;rf=local_loopback\r\n");
    } else if(furi_string_cmp_str(command, "status") == 0) {
        char status[160];
        tumonet_gateway_status_payload(app, status, sizeof(status));
        printf("%s\r\n", status);
    } else if(furi_string_cmp_str(command, "send") == 0) {
        FuriString* source = furi_string_alloc();
        FuriString* message = furi_string_alloc();
        FuriString* route = furi_string_alloc();
        TumoNetGatewayEnvelope envelope = {0};
        const bool parsed = args_read_string_and_trim(args, source) &&
                            args_read_string_and_trim(args, message) &&
                            args_read_string_and_trim(args, route) && !furi_string_empty(args) &&
                            tumonet_gateway_parse_hex32(
                                furi_string_get_cstr(source), &envelope.source_id) &&
                            tumonet_gateway_parse_hex32(
                                furi_string_get_cstr(message), &envelope.message_id);
        if(parsed) {
            if(furi_string_cmp_str(route, "inbox") == 0) {
                envelope.route = TumoNetGatewayRouteInbox;
            } else if(furi_string_cmp_str(route, "rf") == 0) {
                envelope.route = TumoNetGatewayRouteRf;
            } else {
                envelope.message_id = 0U;
            }
            furi_string_trim(args);
            const size_t text_size = furi_string_size(args);
            if(text_size <= TUMONET_GATEWAY_TEXT_MAX) {
                envelope.text_size = (uint8_t)text_size;
                memcpy(envelope.text, furi_string_get_cstr(args), envelope.text_size);
                envelope.text[envelope.text_size] = '\0';
            }
        }

        if(!parsed || envelope.message_id == 0U ||
           !tumonet_gateway_text_valid(
               (const uint8_t*)envelope.text, envelope.text_size)) {
            printf("ERROR usage: tumonet send <source_hex> <message_hex> <inbox|rf> <text>\r\n");
        } else {
            const TumoNetGatewayDelivery delivery = tumonet_gateway_deliver(app, "USB", &envelope);
            printf(
                "schema=1;status=%s;source=%08lX;id=%08lX;route=%s\r\n",
                tumonet_gateway_delivery_token(delivery),
                (unsigned long)envelope.source_id,
                (unsigned long)envelope.message_id,
                tumonet_gateway_route_name(envelope.route));
        }
        furi_string_free(route);
        furi_string_free(message);
        furi_string_free(source);
    } else {
        printf("ERROR unsupported command\r\n");
    }
    furi_string_free(command);

    furi_check(furi_mutex_acquire(app->state_mutex, FuriWaitForever) == FuriStatusOk);
    furi_check(app->cli_inflight > 0U);
    app->cli_inflight--;
    furi_check(furi_mutex_release(app->state_mutex) == FuriStatusOk);
}

static void tumonet_gateway_set_active(TumoNetGatewayApp* app, bool active) {
    if(!active) app->cancel_requested = true;
    furi_check(furi_mutex_acquire(app->work_mutex, FuriWaitForever) == FuriStatusOk);
    furi_check(furi_mutex_acquire(app->state_mutex, FuriWaitForever) == FuriStatusOk);
    app->active = active;
    app->busy = false;
    strlcpy(app->last_status, active ? "Ready for BLE or USB" : "Gateway stopped", sizeof(app->last_status));
    furi_check(furi_mutex_release(app->state_mutex) == FuriStatusOk);
    app->cancel_requested = false;
    furi_check(furi_mutex_release(app->work_mutex) == FuriStatusOk);
    view_port_update(app->view_port);
}

static TumoNetGatewayApp* tumonet_gateway_alloc(void) {
    TumoNetGatewayApp* app = calloc(1U, sizeof(TumoNetGatewayApp));
    furi_check(app != NULL);
    app->queue = furi_message_queue_alloc(
        TUMONET_GATEWAY_QUEUE_CAPACITY, sizeof(TumoNetGatewayEvent));
    app->state_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->work_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->bt = furi_record_open(RECORD_BT);
    app->gui = furi_record_open(RECORD_GUI);
    strlcpy(app->last_ingress, "--", sizeof(app->last_ingress));
    strlcpy(app->last_route, "--", sizeof(app->last_route));
    strlcpy(app->last_status, "Press Start", sizeof(app->last_status));
    strlcpy(app->last_text, "No messages yet", sizeof(app->last_text));

    const FS_Error mkdir_result = storage_common_mkdir(app->storage, TUMONET_GATEWAY_DATA_DIR);
    app->storage_ready = mkdir_result == FSE_OK || mkdir_result == FSE_EXIST;
    tumonet_gateway_load_seen(app);
    app->inbox_count = tumonet_gateway_count_inbox(app);

    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, tumonet_gateway_draw, app);
    view_port_input_callback_set(app->view_port, tumonet_gateway_input_callback, app);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);
    if(!app->storage_ready) {
        strlcpy(app->last_status, "Storage unavailable", sizeof(app->last_status));
    }

    app->bridge_pubsub = bt_app_bridge_get_pubsub(app->bt);
    if(app->bridge_pubsub != NULL) {
        app->bridge_subscription = furi_pubsub_subscribe(
            app->bridge_pubsub, tumonet_gateway_bridge_callback, app);
    }

    CliRegistry* cli = furi_record_open(RECORD_CLI);
    cli_registry_add_command_ex(
        cli,
        "tumonet",
        CliCommandFlagParallelSafe,
        tumonet_gateway_cli,
        app,
        3072U);
    app->cli_registered = true;
    furi_record_close(RECORD_CLI);

    return app;
}

static void tumonet_gateway_free(TumoNetGatewayApp* app) {
    app->cancel_requested = true;
    furi_check(furi_mutex_acquire(app->state_mutex, FuriWaitForever) == FuriStatusOk);
    app->shutting_down = true;
    app->active = false;
    furi_check(furi_mutex_release(app->state_mutex) == FuriStatusOk);

    if(app->cli_registered) {
        CliRegistry* cli = furi_record_open(RECORD_CLI);
        cli_registry_delete_command(cli, "tumonet");
        furi_record_close(RECORD_CLI);
        app->cli_registered = false;
    }
    if(app->bridge_subscription != NULL && app->bridge_pubsub != NULL) {
        furi_pubsub_unsubscribe(app->bridge_pubsub, app->bridge_subscription);
    }

    while(true) {
        furi_check(furi_mutex_acquire(app->state_mutex, FuriWaitForever) == FuriStatusOk);
        const bool cli_idle = app->cli_inflight == 0U;
        furi_check(furi_mutex_release(app->state_mutex) == FuriStatusOk);
        if(cli_idle) break;
        furi_delay_ms(10U);
    }
    furi_check(furi_mutex_acquire(app->work_mutex, FuriWaitForever) == FuriStatusOk);
    furi_check(furi_mutex_release(app->work_mutex) == FuriStatusOk);

    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_BT);
    furi_record_close(RECORD_STORAGE);
    furi_mutex_free(app->work_mutex);
    furi_mutex_free(app->state_mutex);
    furi_message_queue_free(app->queue);
    free(app);
}

int32_t tumonet_gateway_app(void* context) {
    UNUSED(context);
    TumoNetGatewayApp* app = tumonet_gateway_alloc();
    bool running = true;
    TumoNetGatewayEvent event;

    while(running) {
        if(furi_message_queue_get(app->queue, &event, FuriWaitForever) != FuriStatusOk) continue;
        if(event.type == TumoNetGatewayEventBridge) {
            tumonet_gateway_handle_bridge(app, &event.bridge);
            continue;
        }
        if(event.input.type != InputTypeShort) continue;

        if(app->screen == TumoNetGatewayScreenAbout) {
            if(event.input.key == InputKeyBack || event.input.key == InputKeyLeft) {
                furi_check(
                    furi_mutex_acquire(app->state_mutex, FuriWaitForever) == FuriStatusOk);
                app->screen = TumoNetGatewayScreenMain;
                furi_check(furi_mutex_release(app->state_mutex) == FuriStatusOk);
                view_port_update(app->view_port);
            }
            continue;
        }

        if(event.input.key == InputKeyBack || event.input.key == InputKeyLeft) {
            app->cancel_requested = true;
            running = false;
        } else if(event.input.key == InputKeyOk) {
            furi_check(furi_mutex_acquire(app->state_mutex, FuriWaitForever) == FuriStatusOk);
            const bool active = app->active;
            furi_check(furi_mutex_release(app->state_mutex) == FuriStatusOk);
            tumonet_gateway_set_active(app, !active);
        } else if(event.input.key == InputKeyRight) {
            furi_check(furi_mutex_acquire(app->state_mutex, FuriWaitForever) == FuriStatusOk);
            app->screen = TumoNetGatewayScreenAbout;
            furi_check(furi_mutex_release(app->state_mutex) == FuriStatusOk);
            view_port_update(app->view_port);
        }
    }

    tumonet_gateway_free(app);
    return 0;
}
