#include <furi.h>
#include <furi_hal_rtc.h>
#include <gui/gui.h>
#include <input/input.h>
#include <storage/storage.h>
#include <bt/bt_service/bt.h>
#include <furi_hal_bt_client.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLE_GATT_LAB_APP_ID "ble_gatt_lab"
#define BLE_GATT_LAB_DATA_DIR EXT_PATH("apps_data/ble_gatt_lab")
#define BLE_GATT_LAB_LOG_PATH_SIZE 128U
#define BLE_GATT_LAB_LINE_SIZE 48U
#define BLE_GATT_LAB_PAYLOAD_TEXT_SIZE 152U
#define BLE_GATT_LAB_QUEUE_DEPTH 12U
#define BLE_GATT_LAB_EVENT_DATA_PREVIEW_MAX 24U

typedef enum {
    BleGattLabEventTypeInput,
    BleGattLabEventTypeBridge,
    BleGattLabEventTypeScanResult,
    BleGattLabEventTypeGatt,
} BleGattLabEventType;

typedef enum {
    BleGattLabPendingNone,
    BleGattLabPendingWrite,
    BleGattLabPendingNotify,
} BleGattLabPendingOperation;

typedef struct {
    BleGattLabEventType type;
    InputEvent input;
    BtAppBridgeEvent bridge;
    FuriHalBtScanResult scan_result;
    FuriHalBtGattEvent gatt_event;
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
    uint32_t scan_count;
    bool scan_active;
    bool gatt_connected;
    bool log_ready;
    bool bridge_ready;
    bool authorized;
    BleGattLabPendingOperation pending_operation;
    BtAppBridgeEvent pending_request;
    uint16_t pending_handle;
    uint16_t pending_data_len;
    uint8_t pending_write_data[FURI_HAL_BT_GATT_DATA_MAX];
    bool pending_notify_enabled;
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

static bool ble_gatt_lab_is_sensitive_command(const char* command) {
    return command &&
           (strcmp(command, "connect") == 0 || strcmp(command, "write") == 0 ||
            strcmp(command, "notify") == 0 || strcmp(command, "echo") == 0);
}

static void ble_gatt_lab_format_request_log_payload(
    const BtAppBridgeEvent* event,
    char* output,
    size_t output_size) {
    if(!output || output_size == 0U) return;
    if(!event) {
        snprintf(output, output_size, "<none>");
    } else if(ble_gatt_lab_is_sensitive_command(event->command)) {
        snprintf(output, output_size, "<redacted>;len=%u", event->payload_len);
    } else {
        ble_gatt_lab_payload_to_text(event->payload, event->payload_len, output, output_size);
    }
}

static size_t ble_gatt_lab_format_hex(
    const uint8_t* data,
    uint16_t data_len,
    char* output,
    size_t output_size,
    bool* truncated) {
    if(truncated) *truncated = false;
    if(!output || output_size == 0U) return 0U;
    const size_t max_bytes = MIN(
        (size_t)data_len,
        MIN((size_t)BLE_GATT_LAB_EVENT_DATA_PREVIEW_MAX, (output_size - 1U) / 2U));
    for(size_t index = 0U; index < max_bytes; index++) {
        snprintf(&output[index * 2U], output_size - index * 2U, "%02X", data[index]);
    }
    output[max_bytes * 2U] = '\0';
    if(truncated && max_bytes < data_len) *truncated = true;
    return max_bytes * 2U;
}

static void ble_gatt_lab_clear_pending_locked(BleGattLabApp* app) {
    app->pending_operation = BleGattLabPendingNone;
    memset(&app->pending_request, 0, sizeof(app->pending_request));
    app->pending_handle = 0U;
    app->pending_data_len = 0U;
    memset(app->pending_write_data, 0, sizeof(app->pending_write_data));
    app->pending_notify_enabled = false;
}

static bool ble_gatt_lab_pending_is_active(BleGattLabApp* app) {
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    const bool active = app->pending_operation != BleGattLabPendingNone;
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
    return active;
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

    if(!app->authorized) {
        canvas_draw_str(canvas, 0, 24, "Use only with permission");
        canvas_draw_str(canvas, 0, 36, "on devices you own.");
        canvas_draw_str(canvas, 0, 48, "OK: accept   Back: exit");
        furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
        return;
    }

    if(app->pending_operation != BleGattLabPendingNone) {
        canvas_draw_str(canvas, 0, 24, "Confirm BLE operation");
        canvas_draw_str(
            canvas,
            0,
            36,
            app->pending_operation == BleGattLabPendingWrite ? "WRITE characteristic" :
                                                                "CHANGE notifications");
        canvas_draw_str(canvas, 0, 48, "OK: apply   Back: cancel");
        furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
        return;
    }

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
        "RX:%lu TX:%lu ERR:%lu S:%lu %s",
        (unsigned long)app->rx_count,
        (unsigned long)app->tx_count,
        (unsigned long)app->error_count,
        (unsigned long)app->scan_count,
        app->gatt_connected ? "GATT" : "idle");
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

static void ble_gatt_lab_scan_callback(const FuriHalBtScanResult* result, void* context) {
    if(!result || !context) return;
    BleGattLabApp* app = context;
    BleGattLabEvent event = {
        .type = BleGattLabEventTypeScanResult,
        .scan_result = *result,
    };
    furi_message_queue_put(app->queue, &event, 0);
}

static void ble_gatt_lab_gatt_callback(const FuriHalBtGattEvent* gatt_event, void* context) {
    if(!gatt_event || !context) return;
    BleGattLabApp* app = context;
    BleGattLabEvent event = {
        .type = BleGattLabEventTypeGatt,
        .gatt_event = *gatt_event,
    };
    furi_message_queue_put(app->queue, &event, 0);
}

static bool ble_gatt_lab_parse_u16(const uint8_t* payload, uint16_t length, uint16_t* value) {
    if(!payload || !value || length != 2U) return false;
    *value = payload[0] | ((uint16_t)payload[1] << 8);
    return true;
}

static bool ble_gatt_lab_parse_u32(const uint8_t* payload, uint16_t length, uint32_t* value) {
    if(!payload || !value || length != 4U) return false;
    *value = (uint32_t)payload[0] | ((uint32_t)payload[1] << 8) |
             ((uint32_t)payload[2] << 16) | ((uint32_t)payload[3] << 24);
    return true;
}

static bool ble_gatt_lab_parse_peer(
    const uint8_t* payload,
    uint16_t length,
    FuriHalBtPeer* peer) {
    if(!payload || !peer || length != sizeof(peer->address) + 1U) return false;
    peer->address_type = payload[0];
    memcpy(peer->address, &payload[1], sizeof(peer->address));
    return peer->address_type <= 3U;
}

static void ble_gatt_lab_format_address(const uint8_t* address, char* output, size_t output_size) {
    snprintf(
        output,
        output_size,
        "%02X:%02X:%02X:%02X:%02X:%02X",
        address[5],
        address[4],
        address[3],
        address[2],
        address[1],
        address[0]);
}

static const char* ble_gatt_lab_gatt_event_name(FuriHalBtGattEventType type) {
    switch(type) {
    case FuriHalBtGattEventConnected:
        return "connected";
    case FuriHalBtGattEventDisconnected:
        return "disconnected";
    case FuriHalBtGattEventService:
        return "service";
    case FuriHalBtGattEventCharacteristic:
        return "characteristic";
    case FuriHalBtGattEventRead:
        return "read";
    case FuriHalBtGattEventNotification:
        return "notification";
    case FuriHalBtGattEventProcedureComplete:
        return "procedure_complete";
    case FuriHalBtGattEventError:
        return "error";
    default:
        return "unknown";
    }
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

    const char* log_payload =
        (command && (strcmp(command, "write") == 0 || strcmp(command, "notify") == 0)) ?
            "<redacted>" :
            payload;
    ble_gatt_lab_log_event(
        app,
        sent ? "tx" : "tx_error",
        request ? request->protocol_version : 2U,
        request ? request->request_id : 0U,
        request ? request->flags : 0U,
        BLE_GATT_LAB_APP_ID,
        command,
        log_payload,
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
    char log_payload[BLE_GATT_LAB_PAYLOAD_TEXT_SIZE];

    if(!app->authorized) {
        ble_gatt_lab_send_text(app, event, "error", "authorization_required", true);
        ble_gatt_lab_set_lines(app, "Accept on device first", "OK: authorize");
        return;
    }

    ble_gatt_lab_payload_to_text(event->payload, event->payload_len, payload, sizeof(payload));
    ble_gatt_lab_format_request_log_payload(event, log_payload, sizeof(log_payload));

    if(ble_gatt_lab_pending_is_active(app)) {
        ble_gatt_lab_send_text(app, event, "error", "operation_pending", true);
        ble_gatt_lab_set_lines(app, "BLE operation pending", "OK apply / Back cancel");
        return;
    }

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
        log_payload,
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
    } else if(strcmp(event->command, "scan_start") == 0) {
        uint32_t duration_ms = 5000U;
        if(event->payload_len != 0U &&
           !ble_gatt_lab_parse_u32(event->payload, event->payload_len, &duration_ms)) {
            ble_gatt_lab_send_text(app, event, "error", "duration_must_be_u32", true);
            ble_gatt_lab_set_lines(app, "Invalid scan duration", "use 4-byte LE ms");
            return;
        }
        const bool accepted = furi_hal_bt_scan_start(duration_ms, ble_gatt_lab_scan_callback, app);
        furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
        app->scan_active = accepted;
        furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
        ble_gatt_lab_send_text(app, event, "scan_start", accepted ? "accepted" : "unavailable", !accepted);
        ble_gatt_lab_set_lines(app, accepted ? "Passive scan started" : "Scan unavailable", "Full BLE stack required");
    } else if(strcmp(event->command, "scan_stop") == 0) {
        const bool accepted = furi_hal_bt_scan_stop();
        furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
        app->scan_active = false;
        furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
        ble_gatt_lab_send_text(app, event, "scan_stop", accepted ? "accepted" : "idle", !accepted);
        ble_gatt_lab_set_lines(app, accepted ? "Passive scan stopping" : "Scan already idle", "");
    } else if(strcmp(event->command, "connect") == 0) {
        FuriHalBtPeer peer;
        const bool parsed = ble_gatt_lab_parse_peer(event->payload, event->payload_len, &peer);
        const bool accepted = parsed &&
                              furi_hal_bt_gatt_connect(&peer, ble_gatt_lab_gatt_callback, app);
        furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
        app->gatt_connected = accepted;
        furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
        ble_gatt_lab_send_text(app, event, "connect", accepted ? "accepted" : "invalid_or_unavailable", !accepted);
        ble_gatt_lab_set_lines(app, accepted ? "GATT connect started" : "GATT connect rejected", "Full stack + 7-byte peer");
    } else if(strcmp(event->command, "disconnect") == 0) {
        const bool accepted = furi_hal_bt_gatt_disconnect();
        ble_gatt_lab_send_text(app, event, "disconnect", accepted ? "accepted" : "idle", !accepted);
        ble_gatt_lab_set_lines(app, accepted ? "GATT disconnecting" : "GATT already idle", "");
    } else if(strcmp(event->command, "services") == 0) {
        const bool accepted = furi_hal_bt_gatt_discover_services();
        ble_gatt_lab_send_text(app, event, "services", accepted ? "accepted" : "not_connected", !accepted);
        ble_gatt_lab_set_lines(app, accepted ? "Service discovery started" : "Not connected", "");
    } else if(strcmp(event->command, "characteristics") == 0) {
        uint16_t start_handle;
        uint16_t end_handle;
        const bool parsed = event->payload_len == 4U &&
                            ble_gatt_lab_parse_u16(event->payload, 2U, &start_handle) &&
                            ble_gatt_lab_parse_u16(&event->payload[2], 2U, &end_handle);
        const bool accepted = parsed &&
                              furi_hal_bt_gatt_discover_characteristics(start_handle, end_handle);
        ble_gatt_lab_send_text(app, event, "characteristics", accepted ? "accepted" : "invalid_or_not_connected", !accepted);
        ble_gatt_lab_set_lines(app, accepted ? "Characteristic discovery started" : "Invalid range", "two LE u16 handles");
    } else if(strcmp(event->command, "read") == 0) {
        uint16_t handle;
        const bool parsed = ble_gatt_lab_parse_u16(event->payload, event->payload_len, &handle);
        const bool accepted = parsed && furi_hal_bt_gatt_read(handle);
        ble_gatt_lab_send_text(app, event, "read", accepted ? "accepted" : "invalid_or_not_connected", !accepted);
        ble_gatt_lab_set_lines(app, accepted ? "GATT read started" : "Read rejected", "two LE u16 handle");
    } else if(strcmp(event->command, "write") == 0) {
        // Payload: little-endian handle (2), confirmation byte (1), value (1..244).
        uint16_t handle;
        const bool parsed = event->payload_len >= 4U &&
                            (event->payload_len - 3U) <= FURI_HAL_BT_GATT_DATA_MAX &&
                            ble_gatt_lab_parse_u16(event->payload, 2U, &handle);
        const bool accepted = parsed && event->payload[2] == 1U;
        if(accepted) {
            furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
            app->pending_operation = BleGattLabPendingWrite;
            app->pending_request = *event;
            app->pending_handle = handle;
            app->pending_data_len = event->payload_len - 3U;
            memcpy(app->pending_write_data, &event->payload[3], app->pending_data_len);
            furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
            ble_gatt_lab_set_lines(app, "Confirm GATT write", "OK apply / Back cancel");
        } else {
            ble_gatt_lab_send_text(
                app,
                event,
                "write",
                "confirmation_or_connection_required",
                true);
            ble_gatt_lab_set_lines(app, "Write rejected", "explicit confirm required");
        }
    } else if(strcmp(event->command, "notify") == 0) {
        // Payload: little-endian CCCD handle (2), enabled byte (1), confirmation byte (1).
        uint16_t handle;
        const bool parsed = event->payload_len == 4U &&
                            ble_gatt_lab_parse_u16(event->payload, 2U, &handle);
        const bool accepted = parsed && (event->payload[2] <= 1U) && (event->payload[3] == 1U);
        if(accepted) {
            furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
            app->pending_operation = BleGattLabPendingNotify;
            app->pending_request = *event;
            app->pending_handle = handle;
            app->pending_notify_enabled = event->payload[2] == 1U;
            furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
            ble_gatt_lab_set_lines(app, "Confirm notification change", "OK apply / Back cancel");
        } else {
            ble_gatt_lab_send_text(
                app,
                event,
                "notify",
                "confirmation_or_connection_required",
                true);
            ble_gatt_lab_set_lines(app, "Notify rejected", "CCCD + explicit confirm required");
        }
    } else {
        ble_gatt_lab_send_text(app, event, "error", "unsupported_command", true);
        ble_gatt_lab_set_lines(app, "Unsupported command", event->command);
    }
}

static void ble_gatt_lab_handle_scan_result(
    BleGattLabApp* app,
    const FuriHalBtScanResult* result) {
    char address[24];
    char payload[BT_APP_BRIDGE_V2_PAYLOAD_LEN_MAX];
    char log_payload[96];
    ble_gatt_lab_format_address(result->address, address, sizeof(address));

    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    app->scan_count++;
    const uint32_t scan_count = app->scan_count;
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);

    snprintf(
        payload,
        sizeof(payload),
        "count=%lu;addr=%s;rssi=%d;name=%s;data=%u",
        (unsigned long)scan_count,
        address,
        result->rssi,
        result->name[0] ? result->name : "<unknown>",
        result->data_len);
    snprintf(
        log_payload,
        sizeof(log_payload),
        "count=%lu;rssi=%d;data_len=%u",
        (unsigned long)scan_count,
        result->rssi,
        result->data_len);
    const uint32_t request_id = app->request_id_next++;
    bt_app_bridge_send_text_v2(
        app->bt, BLE_GATT_LAB_APP_ID, "scan_result", request_id, 0, payload);
    ble_gatt_lab_log_event(
        app,
        "scan",
        2,
        request_id,
        0,
        BLE_GATT_LAB_APP_ID,
        "scan_result",
        log_payload,
        "");
    ble_gatt_lab_set_lines(app, "BLE advertisement", payload);
}

static void ble_gatt_lab_handle_gatt_event(
    BleGattLabApp* app,
    const FuriHalBtGattEvent* event) {
    char payload[BT_APP_BRIDGE_V2_PAYLOAD_LEN_MAX];
    char log_payload[128];
    char hex_data[(BLE_GATT_LAB_EVENT_DATA_PREVIEW_MAX * 2U) + 1U];
    const uint32_t request_id = app->request_id_next++;
    const int base_written = snprintf(
        payload,
        sizeof(payload),
        "type=%s;conn=%u;start=%u;end=%u;attr=%u;props=%u;len=%u;status=%u",
        ble_gatt_lab_gatt_event_name(event->type),
        event->connection_handle,
        event->start_handle,
        event->end_handle,
        event->attribute_handle,
        event->properties,
        event->data_len,
        event->status);
    snprintf(
        log_payload,
        sizeof(log_payload),
        "type=%s;conn=%u;attr=%u;len=%u;status=%u",
        ble_gatt_lab_gatt_event_name(event->type),
        event->connection_handle,
        event->attribute_handle,
        event->data_len,
        event->status);
    if(base_written > 0 && (size_t)base_written < sizeof(payload) && event->data_len > 0U) {
        bool truncated = false;
        ble_gatt_lab_format_hex(
            event->data, event->data_len, hex_data, sizeof(hex_data), &truncated);
        snprintf(
            &payload[base_written],
            sizeof(payload) - (size_t)base_written,
            ";data=%s%s",
            hex_data,
            truncated ? ";truncated=1" : "");
    }

    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    if(event->type == FuriHalBtGattEventConnected) app->gatt_connected = true;
    if(event->type == FuriHalBtGattEventDisconnected ||
       event->type == FuriHalBtGattEventError) {
        app->gatt_connected = false;
    }
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);

    bt_app_bridge_send_text_v2(
        app->bt, BLE_GATT_LAB_APP_ID, "gatt_event", request_id, 0, payload);
    ble_gatt_lab_log_event(
        app,
        "gatt",
        2,
        request_id,
        0,
        BLE_GATT_LAB_APP_ID,
        "gatt_event",
        log_payload,
        "");
    ble_gatt_lab_set_lines(app, "GATT event", payload);
}

static void ble_gatt_lab_confirm_pending(BleGattLabApp* app) {
    BtAppBridgeEvent request;
    BleGattLabPendingOperation operation;
    uint16_t handle;
    uint16_t data_len;
    uint8_t data[FURI_HAL_BT_GATT_DATA_MAX];
    bool notify_enabled;

    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    operation = app->pending_operation;
    request = app->pending_request;
    handle = app->pending_handle;
    data_len = app->pending_data_len;
    memcpy(data, app->pending_write_data, sizeof(data));
    notify_enabled = app->pending_notify_enabled;
    ble_gatt_lab_clear_pending_locked(app);
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);

    if(operation == BleGattLabPendingWrite) {
        const bool accepted = furi_hal_bt_gatt_write(handle, data, data_len, true);
        ble_gatt_lab_send_text(
            app,
            &request,
            "write",
            accepted ? "accepted" : "connection_required",
            !accepted);
        ble_gatt_lab_set_lines(
            app,
            accepted ? "GATT write started" : "Write rejected",
            "device confirmation accepted");
    } else if(operation == BleGattLabPendingNotify) {
        const bool accepted = furi_hal_bt_gatt_set_notifications(handle, notify_enabled, true);
        ble_gatt_lab_send_text(
            app,
            &request,
            "notify",
            accepted ? "accepted" : "connection_required",
            !accepted);
        ble_gatt_lab_set_lines(
            app,
            accepted ? "GATT notifications updating" : "Notify rejected",
            "device confirmation accepted");
    }
}

static void ble_gatt_lab_cancel_pending(BleGattLabApp* app) {
    BtAppBridgeEvent request;
    BleGattLabPendingOperation operation;
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    operation = app->pending_operation;
    request = app->pending_request;
    ble_gatt_lab_clear_pending_locked(app);
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
    if(operation == BleGattLabPendingWrite) {
        ble_gatt_lab_send_text(app, &request, "write", "user_cancelled", true);
    } else if(operation == BleGattLabPendingNotify) {
        ble_gatt_lab_send_text(app, &request, "notify", "user_cancelled", true);
    }
    ble_gatt_lab_set_lines(app, "BLE operation cancelled", "No change sent");
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
    // Stop callbacks before releasing the queue/context.  The HAL APIs are
    // asynchronous, so give the GAP worker a short hand-off window.
    furi_hal_bt_scan_stop();
    furi_hal_bt_gatt_disconnect();
    furi_delay_ms(50);

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

    bool running = true;
    BleGattLabEvent event;
    while(running) {
        if(furi_message_queue_get(app->queue, &event, FuriWaitForever) != FuriStatusOk) continue;
        if(event.type == BleGattLabEventTypeInput) {
            if((event.input.type == InputTypeShort) && (event.input.key == InputKeyBack)) {
                if(ble_gatt_lab_pending_is_active(app)) {
                    ble_gatt_lab_cancel_pending(app);
                } else {
                    running = false;
                }
            } else if((event.input.type == InputTypeShort) && (event.input.key == InputKeyOk)) {
                if(!app->authorized) {
                    app->authorized = true;
                    ble_gatt_lab_set_lines(app, "Authorized BLE session", "Bridge ready");
                } else if(ble_gatt_lab_pending_is_active(app)) {
                    ble_gatt_lab_confirm_pending(app);
                } else {
                    ble_gatt_lab_send_manual_event(app);
                }
            }
        } else if(event.type == BleGattLabEventTypeBridge) {
            ble_gatt_lab_handle_bridge(app, &event.bridge);
        } else if(event.type == BleGattLabEventTypeScanResult) {
            ble_gatt_lab_handle_scan_result(app, &event.scan_result);
        } else if(event.type == BleGattLabEventTypeGatt) {
            ble_gatt_lab_handle_gatt_event(app, &event.gatt_event);
        }
    }

    ble_gatt_lab_free(app);
    return 0;
}
