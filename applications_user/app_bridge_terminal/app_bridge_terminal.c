#include <furi.h>
#include <furi_hal_rtc.h>
#include <gui/gui.h>
#include <input/input.h>
#include <storage/storage.h>
#include <bt/bt_service/bt.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define APP_BRIDGE_TERMINAL_APP_ID "app_bridge_terminal"
#define APP_BRIDGE_TERMINAL_DATA_DIR EXT_PATH("apps_data/app_bridge_terminal")
#define APP_BRIDGE_TERMINAL_LOG_PATH_SIZE 128U
#define APP_BRIDGE_TERMINAL_LINE_SIZE 48U
#define APP_BRIDGE_TERMINAL_PAYLOAD_SIZE 160U
#define APP_BRIDGE_TERMINAL_OWNER_SIZE 24U
#define APP_BRIDGE_TERMINAL_QUEUE_DEPTH 12U
#define APP_BRIDGE_TERMINAL_SESSION_TIMEOUT_MS 30000U

typedef enum {
    AppBridgeTerminalEventTypeInput,
    AppBridgeTerminalEventTypeBridge,
} AppBridgeTerminalEventType;

typedef struct {
    AppBridgeTerminalEventType type;
    InputEvent input;
    BtAppBridgeEvent bridge;
} AppBridgeTerminalEvent;

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    Storage* storage;
    Bt* bt;
    FuriPubSub* bridge_pubsub;
    FuriPubSubSubscription* bridge_sub;
    FuriMessageQueue* queue;
    FuriMutex* mutex;
    File* log_file;
    char log_path[APP_BRIDGE_TERMINAL_LOG_PATH_SIZE];
    char line1[APP_BRIDGE_TERMINAL_LINE_SIZE];
    char line2[APP_BRIDGE_TERMINAL_LINE_SIZE];
    char owner[APP_BRIDGE_TERMINAL_OWNER_SIZE + 1U];
    char last_command[BT_APP_BRIDGE_COMMAND_LEN_MAX + 1U];
    char last_payload[APP_BRIDGE_TERMINAL_LINE_SIZE];
    uint32_t session_id;
    uint32_t session_tick;
    uint32_t request_id_next;
    uint32_t rx_count;
    uint32_t tx_count;
    uint32_t error_count;
    uint32_t event_seq;
    bool log_ready;
    bool bridge_ready;
} AppBridgeTerminalApp;

static void app_bridge_terminal_format_iso_timestamp(char* output, size_t output_size) {
    DateTime now;
    furi_hal_rtc_get_datetime(&now);
    snprintf(
        output,
        output_size,
        "%04u-%02u-%02uT%02u:%02u:%02u",
        now.year,
        now.month,
        now.day,
        now.hour,
        now.minute,
        now.second);
}

static void app_bridge_terminal_format_filename_timestamp(char* output, size_t output_size) {
    DateTime now;
    furi_hal_rtc_get_datetime(&now);
    snprintf(
        output,
        output_size,
        "%04u%02u%02u_%02u%02u%02u",
        now.year,
        now.month,
        now.day,
        now.hour,
        now.minute,
        now.second);
}

static bool app_bridge_terminal_mkdir(Storage* storage, const char* path) {
    const FS_Error error = storage_common_mkdir(storage, path);
    return (error == FSE_OK) || (error == FSE_EXIST);
}

static bool app_bridge_terminal_write(File* file, const char* text) {
    const size_t size = strlen(text);
    return storage_file_write(file, text, size) == size;
}

static void app_bridge_terminal_write_csv_value(File* file, const char* text) {
    storage_file_write(file, "\"", 1);
    for(const char* cursor = text; *cursor; cursor++) {
        if(*cursor == '"') {
            storage_file_write(file, "\"\"", 2);
        } else {
            storage_file_write(file, cursor, 1);
        }
    }
    storage_file_write(file, "\"", 1);
}

static void app_bridge_terminal_payload_to_text(
    const uint8_t* payload,
    uint16_t payload_len,
    char* output,
    size_t output_size) {
    if(output_size == 0U) return;

    size_t copied = payload_len;
    if(copied > (output_size - 1U)) {
        copied = output_size - 1U;
    }

    for(size_t i = 0; i < copied; i++) {
        const uint8_t byte = payload[i];
        output[i] = ((byte >= 0x20U) && (byte <= 0x7EU)) ? (char)byte : '.';
    }
    output[copied] = '\0';
}

static bool app_bridge_terminal_token_valid(const char* text) {
    if(!text || !text[0]) return false;
    for(const char* cursor = text; *cursor; cursor++) {
        const char ch = *cursor;
        if(!isalnum((unsigned char)ch) && (ch != '_') && (ch != '-') && (ch != '.')) {
            return false;
        }
    }
    return true;
}

static bool app_bridge_terminal_field_copy(
    const char* payload,
    const char* key,
    char* output,
    size_t output_size) {
    const size_t key_len = strlen(key);
    const char* cursor = payload;
    while(cursor && *cursor) {
        const char* next = strchr(cursor, ';');
        const size_t field_len = next ? (size_t)(next - cursor) : strlen(cursor);
        if((field_len > key_len + 1U) && (strncmp(cursor, key, key_len) == 0) &&
           (cursor[key_len] == '=')) {
            const size_t value_len = field_len - key_len - 1U;
            const size_t copy_len =
                value_len < (output_size - 1U) ? value_len : (output_size - 1U);
            memcpy(output, cursor + key_len + 1U, copy_len);
            output[copy_len] = '\0';
            return true;
        }
        cursor = next ? next + 1 : NULL;
    }
    return false;
}

static bool app_bridge_terminal_payload_has_sid(const char* payload, uint32_t session_id) {
    char expected[16];
    snprintf(expected, sizeof(expected), "%08lX", (unsigned long)session_id);

    char sid[16];
    if(!app_bridge_terminal_field_copy(payload, "sid", sid, sizeof(sid))) return false;

    for(size_t i = 0; (sid[i] != '\0') || (expected[i] != '\0'); i++) {
        if(toupper((unsigned char)sid[i]) != toupper((unsigned char)expected[i])) {
            return false;
        }
    }
    return true;
}

static void app_bridge_terminal_set_lines(
    AppBridgeTerminalApp* app,
    const char* line1,
    const char* line2) {
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    strlcpy(app->line1, line1 ? line1 : "", sizeof(app->line1));
    strlcpy(app->line2, line2 ? line2 : "", sizeof(app->line2));
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
    view_port_update(app->view_port);
}

static void app_bridge_terminal_log_event(
    AppBridgeTerminalApp* app,
    const char* event,
    uint8_t protocol,
    uint32_t request_id,
    uint8_t flags,
    const char* command,
    const char* payload,
    const char* status,
    const char* error) {
    if(!app->log_file || !app->log_ready) return;

    char timestamp[32];
    char prefix[96];
    app_bridge_terminal_format_iso_timestamp(timestamp, sizeof(timestamp));
    snprintf(
        prefix,
        sizeof(prefix),
        "%s,%s,%u,%lu,%u,",
        timestamp,
        event,
        protocol,
        (unsigned long)request_id,
        flags);
    app_bridge_terminal_write(app->log_file, prefix);
    app_bridge_terminal_write_csv_value(app->log_file, APP_BRIDGE_TERMINAL_APP_ID);
    app_bridge_terminal_write(app->log_file, ",");
    app_bridge_terminal_write_csv_value(app->log_file, command ? command : "");
    app_bridge_terminal_write(app->log_file, ",");
    app_bridge_terminal_write_csv_value(app->log_file, payload ? payload : "");
    app_bridge_terminal_write(app->log_file, ",");
    app_bridge_terminal_write_csv_value(app->log_file, status ? status : "");
    app_bridge_terminal_write(app->log_file, ",");
    app_bridge_terminal_write_csv_value(app->log_file, error ? error : "");
    app_bridge_terminal_write(app->log_file, "\n");
    storage_file_sync(app->log_file);
}

static void app_bridge_terminal_expire_session(AppBridgeTerminalApp* app) {
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    const bool expired =
        (app->session_id != 0U) &&
        ((furi_get_tick() - app->session_tick) >
         furi_ms_to_ticks(APP_BRIDGE_TERMINAL_SESSION_TIMEOUT_MS));
    if(expired) {
        app->session_id = 0;
        app->owner[0] = '\0';
        strlcpy(app->line1, "Session timeout", sizeof(app->line1));
        strlcpy(app->line2, "Send hello owner=...", sizeof(app->line2));
    }
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
    if(expired) view_port_update(app->view_port);
}

static void app_bridge_terminal_draw_callback(Canvas* canvas, void* context) {
    AppBridgeTerminalApp* app = context;
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 10, "App Bridge Terminal");
    canvas_set_font(canvas, FontSecondary);

    char line[64];
    snprintf(
        line,
        sizeof(line),
        "Bridge:%s Log:%s",
        app->bridge_ready ? "sub" : "--",
        app->log_ready ? "OK" : "--");
    canvas_draw_str(canvas, 0, 22, line);
    snprintf(
        line,
        sizeof(line),
        "RX:%lu TX:%lu ERR:%lu",
        (unsigned long)app->rx_count,
        (unsigned long)app->tx_count,
        (unsigned long)app->error_count);
    canvas_draw_str(canvas, 0, 34, line);
    snprintf(
        line,
        sizeof(line),
        "SID:%08lX %.16s",
        (unsigned long)app->session_id,
        app->owner[0] ? app->owner : "no owner");
    canvas_draw_str(canvas, 0, 46, line);
    canvas_draw_str(canvas, 0, 58, app->line1);
    canvas_draw_str_aligned(canvas, 127, 10, AlignRight, AlignBottom, "OK Event");
    canvas_draw_str_aligned(canvas, 127, 22, AlignRight, AlignBottom, "Back Exit");

    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
}

static void app_bridge_terminal_input_callback(InputEvent* input_event, void* context) {
    AppBridgeTerminalApp* app = context;
    AppBridgeTerminalEvent event = {
        .type = AppBridgeTerminalEventTypeInput,
        .input = *input_event,
    };
    furi_message_queue_put(app->queue, &event, FuriWaitForever);
}

static void app_bridge_terminal_bridge_callback(const void* message, void* context) {
    AppBridgeTerminalApp* app = context;
    const BtAppBridgeEvent* bridge = message;
    if(strcmp(bridge->app_id, APP_BRIDGE_TERMINAL_APP_ID) != 0) return;
    if(bridge->flags & BtAppBridgeFlagResponse) return;

    AppBridgeTerminalEvent event = {.type = AppBridgeTerminalEventTypeBridge};
    event.bridge = *bridge;
    furi_message_queue_put(app->queue, &event, 0);
}

static bool app_bridge_terminal_send_text(
    AppBridgeTerminalApp* app,
    const BtAppBridgeEvent* request,
    const char* command,
    const char* payload,
    bool error) {
    const uint8_t flags = BtAppBridgeFlagResponse | (error ? BtAppBridgeFlagError : 0U);
    const uint32_t request_id = request ? request->request_id : app->request_id_next++;
    const uint8_t send_flags = request ? flags : 0U;
    const bool sent = bt_app_bridge_send_text_v2(
        app->bt,
        APP_BRIDGE_TERMINAL_APP_ID,
        command,
        request_id,
        send_flags,
        payload ? payload : "");

    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    if(sent) {
        app->tx_count++;
    } else {
        app->error_count++;
    }
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);

    app_bridge_terminal_log_event(
        app,
        sent ? "tx" : "tx_error",
        request ? request->protocol_version : 2U,
        request_id,
        send_flags,
        command,
        payload,
        sent ? "ok" : "send_failed",
        sent ? "" : "send_failed");
    return sent;
}

static void app_bridge_terminal_send_event(
    AppBridgeTerminalApp* app,
    const char* event_type,
    const char* text) {
    char payload[APP_BRIDGE_TERMINAL_PAYLOAD_SIZE];
    uint32_t request_id;
    uint32_t event_seq;
    char owner[APP_BRIDGE_TERMINAL_OWNER_SIZE + 1U];

    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    request_id = app->request_id_next++;
    event_seq = ++app->event_seq;
    strlcpy(owner, app->owner, sizeof(owner));
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);

    snprintf(
        payload,
        sizeof(payload),
        "schema=1;type=%.12s;seq=%lu;owner=%.16s;text=%.48s",
        event_type,
        (unsigned long)event_seq,
        owner[0] ? owner : "none",
        text ? text : "");

    const bool sent = bt_app_bridge_send_text_v2(
        app->bt, APP_BRIDGE_TERMINAL_APP_ID, "event", request_id, 0, payload);

    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    if(sent) {
        app->tx_count++;
    } else {
        app->error_count++;
    }
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);

    app_bridge_terminal_log_event(
        app,
        sent ? "tx_event" : "tx_error",
        2,
        request_id,
        0,
        "event",
        payload,
        sent ? "ok" : "send_failed",
        sent ? "" : "send_failed");
    app_bridge_terminal_set_lines(app, sent ? "Event sent" : "Event failed", payload);
}

static bool app_bridge_terminal_session_required(
    AppBridgeTerminalApp* app,
    const char* payload,
    char* error,
    size_t error_size) {
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    const uint32_t session_id = app->session_id;
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);

    if(session_id == 0U) {
        strlcpy(error, "no_session", error_size);
        return false;
    }
    if(!app_bridge_terminal_payload_has_sid(payload, session_id)) {
        strlcpy(error, "sid", error_size);
        return false;
    }
    return true;
}

static void app_bridge_terminal_make_status(
    AppBridgeTerminalApp* app,
    char* output,
    size_t output_size) {
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    snprintf(
        output,
        output_size,
        "schema=1;sid=%08lX;owner=%.16s;rx=%lu;tx=%lu;err=%lu;seq=%lu;log=%hhu",
        (unsigned long)app->session_id,
        app->owner[0] ? app->owner : "none",
        (unsigned long)app->rx_count,
        (unsigned long)app->tx_count,
        (unsigned long)app->error_count,
        (unsigned long)app->event_seq,
        app->log_ready ? 1U : 0U);
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
}

static void app_bridge_terminal_handle_hello(
    AppBridgeTerminalApp* app,
    const BtAppBridgeEvent* event,
    const char* payload) {
    char owner[APP_BRIDGE_TERMINAL_OWNER_SIZE + 1U];
    if(!app_bridge_terminal_field_copy(payload, "owner", owner, sizeof(owner)) ||
       !app_bridge_terminal_token_valid(owner)) {
        app_bridge_terminal_send_text(app, event, "error", "owner", true);
        app_bridge_terminal_set_lines(app, "Invalid owner", "Use owner=iphone");
        return;
    }

    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    app->session_id = event->request_id;
    app->session_tick = furi_get_tick();
    strlcpy(app->owner, owner, sizeof(app->owner));
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);

    char response[APP_BRIDGE_TERMINAL_PAYLOAD_SIZE];
    snprintf(
        response,
        sizeof(response),
        "schema=1;sid=%08lX;owner=%.16s;timeout_ms=%u;commands=help,status,echo,emit,release",
        (unsigned long)event->request_id,
        owner,
        APP_BRIDGE_TERMINAL_SESSION_TIMEOUT_MS);
    app_bridge_terminal_send_text(app, event, "hello", response, false);
    app_bridge_terminal_set_lines(app, "Session opened", response);
}

static void app_bridge_terminal_handle_bridge(
    AppBridgeTerminalApp* app,
    const BtAppBridgeEvent* event) {
    char payload[APP_BRIDGE_TERMINAL_PAYLOAD_SIZE];
    app_bridge_terminal_payload_to_text(
        event->payload, event->payload_len, payload, sizeof(payload));

    app_bridge_terminal_expire_session(app);

    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    app->rx_count++;
    app->session_tick = app->session_id ? furi_get_tick() : app->session_tick;
    strlcpy(app->last_command, event->command, sizeof(app->last_command));
    strlcpy(app->last_payload, payload, sizeof(app->last_payload));
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);

    app_bridge_terminal_log_event(
        app,
        "rx",
        event->protocol_version,
        event->request_id,
        event->flags,
        event->command,
        payload,
        "rx",
        "");

    if(event->protocol_version != 2U) {
        app_bridge_terminal_send_text(app, event, "error", "fab2_required", true);
        app_bridge_terminal_set_lines(app, "FAB2 required", event->command);
        return;
    }
    if((event->chunk_count != 1U) || (event->chunk_index != 0U)) {
        app_bridge_terminal_send_text(app, event, "error", "chunk", true);
        app_bridge_terminal_set_lines(app, "Unsupported chunked request", event->command);
        return;
    }

    if(strcmp(event->command, "ping") == 0) {
        app_bridge_terminal_send_text(app, event, "pong", "ok", false);
        app_bridge_terminal_set_lines(app, "RX ping -> pong", "ok");
    } else if(strcmp(event->command, "help") == 0) {
        app_bridge_terminal_send_text(
            app,
            event,
            "help",
            "schema=1;commands=hello,ping,status,help,echo,emit,release",
            false);
        app_bridge_terminal_set_lines(app, "RX help -> help", "commands sent");
    } else if(strcmp(event->command, "hello") == 0) {
        app_bridge_terminal_handle_hello(app, event, payload);
    } else if(strcmp(event->command, "status") == 0) {
        char status[APP_BRIDGE_TERMINAL_PAYLOAD_SIZE];
        app_bridge_terminal_make_status(app, status, sizeof(status));
        app_bridge_terminal_send_text(app, event, "status", status, false);
        app_bridge_terminal_set_lines(app, "RX status -> status", status);
    } else if(strcmp(event->command, "echo") == 0) {
        char error[16];
        if(!app_bridge_terminal_session_required(app, payload, error, sizeof(error))) {
            app_bridge_terminal_send_text(app, event, "error", error, true);
            app_bridge_terminal_set_lines(app, "Session rejected", error);
        } else {
            char text[80];
            if(!app_bridge_terminal_field_copy(payload, "text", text, sizeof(text))) {
                strlcpy(text, payload, sizeof(text));
            }
            app_bridge_terminal_send_text(app, event, "echo", text, false);
            app_bridge_terminal_set_lines(app, "RX echo -> echo", text);
        }
    } else if(strcmp(event->command, "emit") == 0) {
        char error[16];
        if(!app_bridge_terminal_session_required(app, payload, error, sizeof(error))) {
            app_bridge_terminal_send_text(app, event, "error", error, true);
            app_bridge_terminal_set_lines(app, "Session rejected", error);
        } else {
            char text[80];
            if(!app_bridge_terminal_field_copy(payload, "text", text, sizeof(text))) {
                strlcpy(text, "remote_emit", sizeof(text));
            }
            app_bridge_terminal_send_text(app, event, "emit", "ok", false);
            app_bridge_terminal_send_event(app, "remote", text);
        }
    } else if(strcmp(event->command, "release") == 0) {
        char error[16];
        if(!app_bridge_terminal_session_required(app, payload, error, sizeof(error))) {
            app_bridge_terminal_send_text(app, event, "error", error, true);
            app_bridge_terminal_set_lines(app, "Session rejected", error);
        } else {
            furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
            app->session_id = 0;
            app->owner[0] = '\0';
            furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
            app_bridge_terminal_send_text(app, event, "release", "ok", false);
            app_bridge_terminal_set_lines(app, "Session released", "owner cleared");
        }
    } else {
        app_bridge_terminal_send_text(app, event, "error", "unsupported_command", true);
        app_bridge_terminal_set_lines(app, "Unsupported command", event->command);
    }
}

static void app_bridge_terminal_open_log(AppBridgeTerminalApp* app) {
    char timestamp[32];
    app_bridge_terminal_format_filename_timestamp(timestamp, sizeof(timestamp));
    snprintf(
        app->log_path,
        sizeof(app->log_path),
        APP_BRIDGE_TERMINAL_DATA_DIR "/terminal_%s.csv",
        timestamp);

    if(!app_bridge_terminal_mkdir(app->storage, APP_BRIDGE_TERMINAL_DATA_DIR)) {
        app->log_ready = false;
        return;
    }

    app->log_file = storage_file_alloc(app->storage);
    app->log_ready =
        storage_file_open(app->log_file, app->log_path, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    if(app->log_ready) {
        app_bridge_terminal_write(
            app->log_file,
            "timestamp,event,protocol,request_id,flags,app_id,command,payload,status,error\n");
        storage_file_sync(app->log_file);
    }
}

static AppBridgeTerminalApp* app_bridge_terminal_alloc(void) {
    AppBridgeTerminalApp* app = malloc(sizeof(AppBridgeTerminalApp));
    furi_check(app);
    memset(app, 0, sizeof(AppBridgeTerminalApp));
    app->queue =
        furi_message_queue_alloc(APP_BRIDGE_TERMINAL_QUEUE_DEPTH, sizeof(AppBridgeTerminalEvent));
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->bt = furi_record_open(RECORD_BT);
    app->request_id_next = 1U;
    strlcpy(app->line1, "Send hello owner=iphone", sizeof(app->line1));
    strlcpy(app->line2, "Commands are whitelisted", sizeof(app->line2));

    app_bridge_terminal_open_log(app);

    app->bridge_pubsub = bt_app_bridge_get_pubsub(app->bt);
    if(app->bridge_pubsub) {
        app->bridge_sub =
            furi_pubsub_subscribe(app->bridge_pubsub, app_bridge_terminal_bridge_callback, app);
        app->bridge_ready = app->bridge_sub != NULL;
    }

    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, app_bridge_terminal_draw_callback, app);
    view_port_input_callback_set(app->view_port, app_bridge_terminal_input_callback, app);
    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);
    return app;
}

static void app_bridge_terminal_free(AppBridgeTerminalApp* app) {
    if(app->bridge_sub && app->bridge_pubsub) {
        furi_pubsub_unsubscribe(app->bridge_pubsub, app->bridge_sub);
    }
    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);

    if(app->log_file) {
        storage_file_sync(app->log_file);
        storage_file_close(app->log_file);
        storage_file_free(app->log_file);
    }

    furi_message_queue_free(app->queue);
    furi_mutex_free(app->mutex);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_BT);
    furi_record_close(RECORD_STORAGE);
    free(app);
}

int32_t app_bridge_terminal_app(void* context) {
    UNUSED(context);
    AppBridgeTerminalApp* app = app_bridge_terminal_alloc();

    bool running = true;
    AppBridgeTerminalEvent event;
    while(running) {
        app_bridge_terminal_expire_session(app);
        if(furi_message_queue_get(app->queue, &event, furi_ms_to_ticks(250)) != FuriStatusOk) {
            continue;
        }
        if(event.type == AppBridgeTerminalEventTypeInput) {
            if((event.input.type == InputTypeShort) && (event.input.key == InputKeyBack)) {
                running = false;
            } else if((event.input.type == InputTypeShort) && (event.input.key == InputKeyOk)) {
                app_bridge_terminal_send_event(app, "manual", "button_ok");
            }
        } else if(event.type == AppBridgeTerminalEventTypeBridge) {
            app_bridge_terminal_handle_bridge(app, &event.bridge);
        }
    }

    app_bridge_terminal_free(app);
    return 0;
}
