#include <furi.h>
#include <furi_hal_bt_client.h>
#include <gui/gui.h>
#include <input/input.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLE_SCANNER_MAX_RESULTS 16U
#define BLE_SCANNER_QUEUE_DEPTH 12U
#define BLE_SCANNER_SCAN_MS     8000U
#define BLE_SCANNER_LINE_MAX    32U

typedef enum {
    BleScannerEventInput,
    BleScannerEventScanResult,
} BleScannerEventType;

typedef struct {
    BleScannerEventType type;
    InputEvent input;
    FuriHalBtScanResult scan_result;
} BleScannerEvent;

typedef struct {
    FuriHalBtScanResult result;
    uint32_t seen;
} BleScannerEntry;

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    FuriMessageQueue* queue;
    BleScannerEntry entries[BLE_SCANNER_MAX_RESULTS];
    uint8_t entry_count;
    uint8_t selected;
    uint32_t seen_count;
    bool authorized;
    bool scanning;
    bool unavailable;
} BleScannerApp;

static void ble_scanner_set_scan_state(BleScannerApp* app, bool scanning, bool unavailable) {
    app->scanning = scanning;
    app->unavailable = unavailable;
    view_port_update(app->view_port);
}

static void ble_scanner_format_address(
    const uint8_t* address,
    char* output,
    size_t output_size) {
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

static int8_t ble_scanner_find_entry(BleScannerApp* app, const FuriHalBtScanResult* result) {
    for(uint8_t index = 0U; index < app->entry_count; index++) {
        if(app->entries[index].result.address_type == result->address_type &&
           memcmp(app->entries[index].result.address, result->address, sizeof(result->address)) ==
               0) {
            return (int8_t)index;
        }
    }
    return -1;
}

static void ble_scanner_draw_callback(Canvas* canvas, void* context) {
    BleScannerApp* app = context;
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 10, "BLE Scanner");
    canvas_set_font(canvas, FontSecondary);

    if(!app->authorized) {
        canvas_draw_str(canvas, 0, 24, "Use only with permission");
        canvas_draw_str(canvas, 0, 36, "on devices you own.");
        canvas_draw_str(canvas, 0, 48, "OK: accept   Back: exit");
        return;
    }

    char line[BLE_SCANNER_LINE_MAX];
    snprintf(
        line,
        sizeof(line),
        "%s  %u/%u",
        app->unavailable ? "UNAVAILABLE" : (app->scanning ? "SCANNING" : "IDLE"),
        app->entry_count,
        BLE_SCANNER_MAX_RESULTS);
    canvas_draw_str(canvas, 0, 22, line);

    if(app->entry_count == 0U) {
        canvas_draw_str(canvas, 0, 36, app->unavailable ? "Full BLE stack required" : "OK: start scan");
    } else {
        const BleScannerEntry* entry = &app->entries[app->selected];
        char address[20];
        ble_scanner_format_address(entry->result.address, address, sizeof(address));
        snprintf(
            line,
            sizeof(line),
            "%s %ddBm",
            entry->result.name[0] ? entry->result.name : "<unknown>",
            entry->result.rssi);
        canvas_draw_str(canvas, 0, 36, line);
        canvas_draw_str(canvas, 0, 47, address);
        snprintf(line, sizeof(line), "%u adv  Up/Down select", entry->result.data_len);
        canvas_draw_str(canvas, 0, 58, line);
    }
    canvas_draw_str_aligned(
        canvas,
        127,
        10,
        AlignRight,
        AlignBottom,
        app->scanning ? "OK stop" : "OK scan");
}

static void ble_scanner_input_callback(InputEvent* input_event, void* context) {
    BleScannerApp* app = context;
    BleScannerEvent event = {
        .type = BleScannerEventInput,
        .input = *input_event,
    };
    furi_message_queue_put(app->queue, &event, FuriWaitForever);
}

static void ble_scanner_scan_callback(const FuriHalBtScanResult* result, void* context) {
    if(!result || !context) return;
    BleScannerApp* app = context;
    BleScannerEvent event = {
        .type = BleScannerEventScanResult,
        .scan_result = *result,
    };
    furi_message_queue_put(app->queue, &event, 0U);
}

static void ble_scanner_start(BleScannerApp* app) {
    if(app->scanning) return;
    const bool started = furi_hal_bt_scan_start(BLE_SCANNER_SCAN_MS, ble_scanner_scan_callback, app);
    ble_scanner_set_scan_state(app, started, !started);
}

static void ble_scanner_stop(BleScannerApp* app) {
    if(app->scanning) furi_hal_bt_scan_stop();
    ble_scanner_set_scan_state(app, false, false);
}

static void ble_scanner_handle_scan_result(
    BleScannerApp* app,
    const FuriHalBtScanResult* result) {
    int8_t found = ble_scanner_find_entry(app, result);
    if(found < 0) {
        if(app->entry_count >= BLE_SCANNER_MAX_RESULTS) return;
        found = (int8_t)app->entry_count++;
        memset(&app->entries[found], 0, sizeof(app->entries[found]));
    }
    app->entries[found].result = *result;
    app->entries[found].seen++;
    app->seen_count++;
    view_port_update(app->view_port);
}

static BleScannerApp* ble_scanner_alloc(void) {
    BleScannerApp* app = calloc(1U, sizeof(BleScannerApp));
    app->queue = furi_message_queue_alloc(BLE_SCANNER_QUEUE_DEPTH, sizeof(BleScannerEvent));
    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, ble_scanner_draw_callback, app);
    view_port_input_callback_set(app->view_port, ble_scanner_input_callback, app);
    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);
    return app;
}

static void ble_scanner_free(BleScannerApp* app) {
    ble_scanner_stop(app);
    furi_delay_ms(50U);
    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);
    furi_message_queue_free(app->queue);
    furi_record_close(RECORD_GUI);
    free(app);
}

int32_t ble_scanner_app(void* context) {
    UNUSED(context);
    BleScannerApp* app = ble_scanner_alloc();
    bool running = true;
    BleScannerEvent event;
    while(running) {
        if(furi_message_queue_get(app->queue, &event, FuriWaitForever) != FuriStatusOk) continue;
        if(event.type == BleScannerEventScanResult) {
            ble_scanner_handle_scan_result(app, &event.scan_result);
        } else if(event.type == BleScannerEventInput && event.input.type == InputTypeShort) {
            if(event.input.key == InputKeyBack) {
                running = false;
            } else if(event.input.key == InputKeyOk) {
                if(!app->authorized) {
                    app->authorized = true;
                    view_port_update(app->view_port);
                } else if(app->scanning) {
                    ble_scanner_stop(app);
                } else {
                    ble_scanner_start(app);
                }
            } else if(app->authorized && app->entry_count > 0U && event.input.key == InputKeyUp) {
                if(app->selected > 0U) app->selected--;
                view_port_update(app->view_port);
            } else if(app->authorized && app->entry_count > 0U && event.input.key == InputKeyDown) {
                if(app->selected + 1U < app->entry_count) app->selected++;
                view_port_update(app->view_port);
            }
        }
    }
    ble_scanner_free(app);
    return 0;
}
