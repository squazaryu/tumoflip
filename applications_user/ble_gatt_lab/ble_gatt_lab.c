#include <furi.h>
#include <furi_hal_rtc.h>
#include <gui/gui.h>
#include <input/input.h>
#include <storage/storage.h>
#include <bt/bt_service/bt.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLE_GATT_LAB_APP_ID "ble_gatt_lab"
#define BLE_GATT_LAB_DATA_DIR EXT_PATH("apps_data/ble_gatt_lab")
#define BLE_GATT_LAB_LOG_PATH_SIZE 128U
#define BLE_GATT_LAB_LINE_SIZE 48U
#define BLE_GATT_LAB_PAYLOAD_TEXT_SIZE 152U
#define BLE_GATT_LAB_QUEUE_DEPTH 12U

typedef enum {
    BleGattLabEventTypeInput,
    BleGattLabEventTypeBridge,
} BleGattLabEventType;

typedef struct {
    BleGattLabEventType type;
    InputEvent input;
    BtAppBridgeEvent bridge;
} BleGattLabEvent;

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
    char log_path[BLE_GATT_LAB_LOG_PATH_SIZE];
    char line1[BLE_GATT_LAB_LINE_SIZE];
    char line2[BLE_GATT_LAB_LINE_SIZE];
    char last_app[BT_APP_BRIDGE_APP_ID_LEN_MAX + 1];
    char last_command[BT_APP_BRIDGE_COMMAND_LEN_MAX + 1];
    uint32_t rx_count;
    uint32_t tx_count;
    uint32_t error_count;
    uint32_t request_id_next;
    uint32_t last_request_id;
    uint8_t last_protocol;
    bool log_ready;
    bool bridge_ready;
} BleGattLabApp;

static void ble_gatt_lab_format_iso_timestamp(char* output, size_t output_size) {
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

static void ble_gatt_lab_format_filename_timestamp(char* output, size_t output_size) {
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

static bool ble_gatt_lab_mkdir(Storage* storage, const char* path) {
    const FS_Error error = storage_common_mkdir(storage, path);
    return (error == FSE_OK) || (error == FSE_EXIST);
}

static bool ble_gatt_lab_write(File* file, const char* text) {
    const size_t size = strlen(text);
    return storage_file_write(file, text, size) == size;
}

static void ble_gatt_lab_write_csv_value(File* file, const char* text) {
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

static void ble_gatt_lab_payload_to_text(
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

static void ble_gatt_lab_log_event(
    BleGattLabApp* app,
    const char* event,
    uint8_t protocol,
    uint32_t request_id,
    uint8_t flags,
    const char* app_id,
    const char* command,
    const char* payload,
    const char* error) {
    if(!app->log_file || !app->log_ready) return;

    char timestamp[32];
    char prefix[80];
    ble_gatt_lab_format_iso_timestamp(timestamp, sizeof(timestamp));
    snprintf(
        prefix,
        sizeof(prefix),
        "%s,%s,%u,%lu,%u,",
        timestamp,
        event,
        protocol,
        (unsigned long)request_id,
        flags);
    ble_gatt_lab_write(app->log_file, prefix);
    ble_gatt_lab_write_csv_value(app->log_file, app_id ? app_id : "");
    ble_gatt_lab_write(app->log_file, ",");
    ble_gatt_lab_write_csv_value(app->log_file, command ? command : "");
    ble_gatt_lab_write(app->log_file, ",");
    ble_gatt_lab_write_csv_value(app->log_file, payload ? payload : "");
    ble_gatt_lab_write(app->log_file, ",");
    ble_gatt_lab_write_csv_value(app->log_file, error ? error : "");
    ble_gatt_lab_write(app->log_file, "\n");
    storage_file_sync(app->log_file);
}

static void ble_gatt_lab_set_lines(BleGattLabApp* app, const char* line1, const char* line2) {
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    strlcpy(app->line1, line1 ? line1 : "", sizeof(app->line1));
    strlcpy(app->line2, line2 ? line2 : "", sizeof(app->line2));
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
    view_port_update(app->view_port);
}

static void ble_gatt_lab_draw_callback(Canvas* canvas, void* context) {
    BleGattLabApp* app = context;
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 10, "BLE GATT Lab");
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

    canvas_draw_str(canvas, 0, 46, app->line1);
    canvas_draw_str(canvas, 0, 58, app->line2);
    canvas_draw_str_aligned(canvas, 127, 10, AlignRight, AlignBottom, "OK Event");

    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
}

static void ble_gatt_lab_input_callback(InputEvent* input_event, void* context) {
    BleGattLabApp* app = context;
    BleGattLabEvent event = {
        .type = BleGattLabEventTypeInput,
        .input = *input_event,
    };
    furi_message_queue_put(app->queue, &event, FuriWaitForever);
}

static void ble_gatt_lab_bridge_callback(const void* message, void* context) {
    BleGattLabApp* app = context;
    const BtAppBridgeEvent* bridge = message;
    if(strcmp(bridge->app_id, BLE_GATT_LAB_APP_ID) != 0) return;
    if(bridge->flags & BtAppBridgeFlagResponse) return;

    BleGattLabEvent event = {.type = BleGattLabEventTypeBridge};
    event.bridge = *bridge;
    furi_message_queue_put(app->queue, &event, 0);
}

static bool ble_gatt_lab_send_text(
    BleGattLabApp* app,
    const BtAppBridgeEvent* request,
    const char* command,
    const char* payload,
    bool error) {
    bool sent = false;
    if(request && (request->protocol_version == 2U)) {
        const uint8_t flags = BtAppBridgeFlagResponse | (error ? BtAppBridgeFlagError : 0U);
        sent = bt_app_bridge_send_text_v2(
            app->bt, BLE_GATT_LAB_APP_ID, command, request->request_id, flags, payload);
    } else {
        sent = bt_app_bridge_send_text(app->bt, BLE_GATT_LAB_APP_ID, command, payload);
    }

    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    if(sent) {
        app->tx_count++;
    } else {
        app->error_count++;
    }
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);

    ble_gatt_lab_log_event(
        app,
        sent ? "tx" : "tx_error",
        request ? request->protocol_version : 2U,
        request ? request->request_id : 0U,
        request ? request->flags : 0U,
        BLE_GATT_LAB_APP_ID,
        command,
        payload,
        sent ? "" : "send_failed");
    return sent;
}

static void ble_gatt_lab_send_manual_event(BleGattLabApp* app) {
    char payload[96];
    uint32_t request_id;
    uint32_t rx_count;
    uint32_t tx_count;
    uint32_t error_count;

    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    request_id = app->request_id_next++;
    rx_count = app->rx_count;
    tx_count = app->tx_count;
    error_count = app->error_count;
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);

    snprintf(
        payload,
        sizeof(payload),
        "event=manual;rx=%lu;tx=%lu;err=%lu",
        (unsigned long)rx_count,
        (unsigned long)tx_count,
        (unsigned long)error_count);

    const bool sent =
        bt_app_bridge_send_text_v2(app->bt, BLE_GATT_LAB_APP_ID, "event", request_id, 0, payload);

    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    if(sent) {
        app->tx_count++;
    } else {
        app->error_count++;
    }
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);

    ble_gatt_lab_log_event(
        app,
        sent ? "tx_event" : "tx_error",
        2,
        request_id,
        0,
        BLE_GATT_LAB_APP_ID,
        "event",
        payload,
        sent ? "" : "send_failed");
    ble_gatt_lab_set_lines(app, sent ? "Manual event sent" : "Manual event failed", payload);
}

static void ble_gatt_lab_handle_bridge(BleGattLabApp* app, const BtAppBridgeEvent* event) {
    char payload[BLE_GATT_LAB_PAYLOAD_TEXT_SIZE];
    char status[BT_APP_BRIDGE_V2_PAYLOAD_LEN_MAX];
    ble_gatt_lab_payload_to_text(event->payload, event->payload_len, payload, sizeof(payload));

    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    app->rx_count++;
    app->last_request_id = event->request_id;
    app->last_protocol = event->protocol_version;
    strlcpy(app->last_app, event->app_id, sizeof(app->last_app));
    strlcpy(app->last_command, event->command, sizeof(app->last_command));
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);

    ble_gatt_lab_log_event(
        app,
        "rx",
        event->protocol_version,
        event->request_id,
        event->flags,
        event->app_id,
        event->command,
        payload,
        "");

    if((event->chunk_count != 1U) || (event->chunk_index != 0U)) {
        ble_gatt_lab_send_text(app, event, "error", "chunking_unsupported", true);
        ble_gatt_lab_set_lines(app, "Unsupported chunked request", event->command);
        return;
    }

    if(strcmp(event->command, "ping") == 0) {
        ble_gatt_lab_send_text(app, event, "pong", "ok", false);
        ble_gatt_lab_set_lines(app, "RX ping -> pong", "payload ok");
    } else if(strcmp(event->command, "status") == 0) {
        uint32_t rx_count;
        uint32_t tx_count;
        uint32_t error_count;
        furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
        rx_count = app->rx_count;
        tx_count = app->tx_count;
        error_count = app->error_count;
        furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
        snprintf(
            status,
            sizeof(status),
            "schema=1;app=%s;rx=%lu;tx=%lu;err=%lu;log=%hhu",
            BLE_GATT_LAB_APP_ID,
            (unsigned long)rx_count,
            (unsigned long)tx_count,
            (unsigned long)error_count,
            app->log_ready ? 1U : 0U);
        ble_gatt_lab_send_text(app, event, "status", status, false);
        ble_gatt_lab_set_lines(app, "RX status -> status", status);
    } else if(strcmp(event->command, "echo") == 0) {
        ble_gatt_lab_send_text(app, event, "echo", payload, false);
        ble_gatt_lab_set_lines(app, "RX echo -> echo", payload[0] ? payload : "<empty>");
    } else {
        ble_gatt_lab_send_text(app, event, "error", "unsupported_command", true);
        ble_gatt_lab_set_lines(app, "Unsupported command", event->command);
    }
}

static void ble_gatt_lab_open_log(BleGattLabApp* app) {
    char timestamp[32];
    ble_gatt_lab_format_filename_timestamp(timestamp, sizeof(timestamp));
    snprintf(
        app->log_path,
        sizeof(app->log_path),
        BLE_GATT_LAB_DATA_DIR "/ble_gatt_lab_%s.csv",
        timestamp);

    if(!ble_gatt_lab_mkdir(app->storage, BLE_GATT_LAB_DATA_DIR)) {
        app->log_ready = false;
        return;
    }

    app->log_file = storage_file_alloc(app->storage);
    app->log_ready =
        storage_file_open(app->log_file, app->log_path, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    if(app->log_ready) {
        ble_gatt_lab_write(
            app->log_file,
            "timestamp,event,protocol,request_id,flags,app_id,command,payload,error\n");
        storage_file_sync(app->log_file);
    }
}

static BleGattLabApp* ble_gatt_lab_alloc(void) {
    BleGattLabApp* app = malloc(sizeof(BleGattLabApp));
    memset(app, 0, sizeof(BleGattLabApp));
    app->queue = furi_message_queue_alloc(BLE_GATT_LAB_QUEUE_DEPTH, sizeof(BleGattLabEvent));
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->bt = furi_record_open(RECORD_BT);
    app->request_id_next = 1U;
    strlcpy(app->line1, "Waiting for App Bridge", sizeof(app->line1));
    strlcpy(app->line2, "Send ping/status/echo", sizeof(app->line2));

    ble_gatt_lab_open_log(app);

    app->bridge_pubsub = bt_app_bridge_get_pubsub(app->bt);
    if(app->bridge_pubsub) {
        app->bridge_sub =
            furi_pubsub_subscribe(app->bridge_pubsub, ble_gatt_lab_bridge_callback, app);
        app->bridge_ready = app->bridge_sub != NULL;
    }

    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, ble_gatt_lab_draw_callback, app);
    view_port_input_callback_set(app->view_port, ble_gatt_lab_input_callback, app);
    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);
    return app;
}

static void ble_gatt_lab_free(BleGattLabApp* app) {
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

int32_t ble_gatt_lab_app(void* context) {
    UNUSED(context);
    BleGattLabApp* app = ble_gatt_lab_alloc();

    ble_gatt_lab_send_manual_event(app);

    bool running = true;
    BleGattLabEvent event;
    while(running) {
        if(furi_message_queue_get(app->queue, &event, FuriWaitForever) != FuriStatusOk) continue;
        if(event.type == BleGattLabEventTypeInput) {
            if((event.input.type == InputTypeShort) && (event.input.key == InputKeyBack)) {
                running = false;
            } else if((event.input.type == InputTypeShort) && (event.input.key == InputKeyOk)) {
                ble_gatt_lab_send_manual_event(app);
            }
        } else if(event.type == BleGattLabEventTypeBridge) {
            ble_gatt_lab_handle_bridge(app, &event.bridge);
        }
    }

    ble_gatt_lab_free(app);
    return 0;
}
