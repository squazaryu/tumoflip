#include <furi.h>
#include <furi_hal.h>

#include <gui/gui.h>
#include <gui/elements.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include "wifi_mapper_icons.h" // generated from icons/ (fap_icon_assets)

#include <notification/notification.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>
#include <bt/bt_service/bt.h>
#include <expansion/expansion.h>

#include "wifi_mapper_insights.h"
#include "wifi_mapper_inspector.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAG "WiFiMapper"

#define WIFI_MAPPER_BAUDRATE            115200
#define WIFI_MAPPER_RX_BUFFER_SIZE      2048U
#define WIFI_MAPPER_LINE_SIZE           160U
#define WIFI_MAPPER_PATH_SIZE           128U
#define WIFI_MAPPER_SESSION_LINE_SIZE   256U
#define WIFI_MAPPER_LAST_LINE_SIZE      48U
#define WIFI_MAPPER_BSSID_SIZE          18U
#define WIFI_MAPPER_SSID_SIZE           64U
#define WIFI_MAPPER_AUTH_SIZE           24U
#define WIFI_MAPPER_GEO_SIZE            16U
#define WIFI_MAPPER_FILE_NAME_SIZE      48U
#define WIFI_MAPPER_MAX_UNIQUE          64U
#define WIFI_MAPPER_MAX_EXPORT_FEATURES WIFI_MAPPER_MAX_UNIQUE
#define WIFI_MAPPER_CHANNEL_BUCKETS     15U
#define WIFI_MAPPER_CSV_FIELD_SIZE      64U

#define WIFI_MAPPER_DATA_DIR         EXT_PATH("apps_data/wifi_mapper")
#define WIFI_MAPPER_SESSIONS_DIR     EXT_PATH("apps_data/wifi_mapper/sessions")
#define WIFI_MAPPER_EXPORTS_DIR      EXT_PATH("apps_data/wifi_mapper/exports")
#define WIFI_MAPPER_BASELINE_PATH    EXT_PATH("apps_data/wifi_mapper/baseline.txt")
#define WIFI_MAPPER_SCAN_ALL_COMMAND "scanall\r\n"
#define WIFI_MAPPER_WARDRIVE_COMMAND "wardrive -serial\r\n"
#define WIFI_MAPPER_STOP_COMMAND     "stopscan\r\n"

// Opt-in live relay for Companion Live WiFi scan. The payload is one or more
// raw printable UART lines separated by '\n' and sent as a single FAB2 frame:
// app_id "wifi_mapper", command "live_line", request_id 0, flags 0.
// Keep the payload below the App Bridge v2 160-byte cap; long single lines are
// clipped rather than dropped so the phone still receives a useful sample.
#define WIFI_MAPPER_RELAY_APP_ID        "wifi_mapper"
#define WIFI_MAPPER_RELAY_COMMAND       "live_line"
#define WIFI_MAPPER_RELAY_START_COMMAND "survey_start"
#define WIFI_MAPPER_RELAY_STOP_COMMAND  "survey_stop"
#define WIFI_MAPPER_RELAY_PAYLOAD_MAX   150U
#define WIFI_MAPPER_RELAY_FLUSH_MS      120U
#define WIFI_MAPPER_DEVICE_SERVICES_APP "device_services"
#define WIFI_MAPPER_GPS_TIMEOUT_MS      12000U
#define WIFI_MAPPER_GPS_RESPONSE_MAX    256U

typedef enum {
    WiFiMapperEventReserved = (1 << 0),
    WiFiMapperEventStop = (1 << 1),
    WiFiMapperEventRxData = (1 << 2),
    WiFiMapperEventRxIdle = (1 << 3),
    WiFiMapperEventRxError = (1 << 4),
    WiFiMapperEventBridge = (1 << 5),
} WiFiMapperEvent;

#define WIFI_MAPPER_WORKER_EVENTS                                          \
    (WiFiMapperEventStop | WiFiMapperEventRxData | WiFiMapperEventRxIdle | \
     WiFiMapperEventRxError | WiFiMapperEventBridge)

typedef enum {
    WiFiMapperGpsIdle,
    WiFiMapperGpsWaiting,
    WiFiMapperGpsReady,
    WiFiMapperGpsUnavailable,
} WiFiMapperGpsState;

typedef enum {
    WiFiMapperScanModeAll,
    WiFiMapperScanModeWardrive,
    WiFiMapperScanModeCount,
} WiFiMapperScanMode;

typedef enum {
    WiFiMapperScreenLive,
    WiFiMapperScreenInsights,
    WiFiMapperScreenSession,
    WiFiMapperScreenAbout,
    WiFiMapperScreenInspector,
    WiFiMapperScreenInspectorList,
    WiFiMapperScreenLocator,
} WiFiMapperScreen;

typedef enum {
    WiFiMapperSessionPageSummary,
    WiFiMapperSessionPageSecurity,
    WiFiMapperSessionPageChannels,
    WiFiMapperSessionPageBaseline,
    WiFiMapperSessionPageCount,
} WiFiMapperSessionPage;

typedef enum {
    WiFiMapperExportModeClean,
    WiFiMapperExportModeRaw,
    WiFiMapperExportModeCount,
} WiFiMapperExportMode;

typedef struct {
    const char* label;
    const char* command;
    const char* sent_status;
} WiFiMapperScanModeConfig;

typedef struct {
    bool is_wifi;
    const char* type;
    int32_t rssi;
    uint8_t channel;
    char bssid[WIFI_MAPPER_BSSID_SIZE];
    char ssid[WIFI_MAPPER_SSID_SIZE];
    char auth[WIFI_MAPPER_AUTH_SIZE];
    char latitude[WIFI_MAPPER_GEO_SIZE];
    char longitude[WIFI_MAPPER_GEO_SIZE];
    char altitude[WIFI_MAPPER_GEO_SIZE];
    char accuracy[WIFI_MAPPER_GEO_SIZE];
    bool has_location;
} WiFiMapperRecord;

typedef struct {
    bool valid;
    uint32_t tick_ms;
    int32_t rssi;
    uint8_t channel;
    char bssid[WIFI_MAPPER_BSSID_SIZE];
    char ssid[WIFI_MAPPER_SSID_SIZE];
    char auth[WIFI_MAPPER_AUTH_SIZE];
    char latitude[WIFI_MAPPER_GEO_SIZE];
    char longitude[WIFI_MAPPER_GEO_SIZE];
    char altitude[WIFI_MAPPER_GEO_SIZE];
    char accuracy[WIFI_MAPPER_GEO_SIZE];
} WiFiMapperExportRow;

typedef struct {
    WiFiMapperExportRow row;
    uint32_t samples;
    uint32_t first_tick_ms;
    uint32_t last_tick_ms;
    int32_t best_rssi;
    int32_t last_rssi;
    int32_t rssi_sum;
} WiFiMapperExportAggregate;

typedef struct {
    bool loaded;
    bool truncated;
    bool has_baseline;
    bool baseline_truncated;
    char status[24];
    char file_name[WIFI_MAPPER_FILE_NAME_SIZE];
    char baseline_file_name[WIFI_MAPPER_FILE_NAME_SIZE];
    char export_name[WIFI_MAPPER_FILE_NAME_SIZE];
    uint32_t rows;
    uint32_t aps;
    uint32_t unique;
    uint32_t located;
    uint32_t mapped;
    uint32_t duplicates;
    uint32_t exported;
    int32_t best_rssi;
    int32_t avg_rssi;
    uint8_t top_channel;
    uint32_t top_channel_count;
    uint32_t observations;
    uint32_t open;
    uint32_t legacy;
    uint32_t wpa2;
    uint32_t wpa3;
    uint32_t other_security;
    uint32_t channel_counts[WIFI_MAPPER_INSIGHTS_CHANNEL_COUNT];
    int32_t delta_unique;
    int32_t delta_observations;
    int32_t delta_open;
    int32_t delta_wpa3;
} WiFiMapperSessionStats;

typedef struct {
    char mapped_bssids[WIFI_MAPPER_MAX_UNIQUE][WIFI_MAPPER_BSSID_SIZE];
    WiFiMapperInsights insights;
    char line[WIFI_MAPPER_SESSION_LINE_SIZE];
    uint8_t buffer[64];
} WiFiMapperSessionScratch;

typedef struct {
    WiFiMapperExportAggregate aggregates[WIFI_MAPPER_MAX_EXPORT_FEATURES];
    uint32_t aggregate_count;
    char line[WIFI_MAPPER_SESSION_LINE_SIZE];
    uint8_t buffer[64];
} WiFiMapperExportScratch;

typedef struct {
    char line[WIFI_MAPPER_SESSION_LINE_SIZE];
    uint8_t buffer[64];
} WiFiMapperInspectorReadScratch;

static const WiFiMapperScanModeConfig wifi_mapper_scan_modes[WiFiMapperScanModeCount] = {
    [WiFiMapperScanModeAll] =
        {
            .label = "Scan All",
            .command = WIFI_MAPPER_SCAN_ALL_COMMAND,
            .sent_status = "scanall sent",
        },
    [WiFiMapperScanModeWardrive] =
        {
            .label = "Wardrive",
            .command = WIFI_MAPPER_WARDRIVE_COMMAND,
            .sent_status = "wardrive sent",
        },
};

typedef struct {
    WiFiMapperScreen screen;
    WiFiMapperSessionPage session_page;
    bool logging;
    bool uart_ready;
    bool ble_relay;
    WiFiMapperGpsState gps_state;
    WiFiMapperScanMode scan_mode;
    WiFiMapperExportMode export_mode;
    WiFiMapperSessionStats session;
    WiFiMapperInsights insights;
    uint32_t lines;
    uint32_t wifi_records;
    uint32_t errors;
    char status[24];
    char file_name[WIFI_MAPPER_FILE_NAME_SIZE];
    char last_line[WIFI_MAPPER_LAST_LINE_SIZE];
    bool inspector_loaded;
    bool inspector_has_baseline;
    bool inspector_partial;
    size_t inspector_new_count;
    size_t inspector_changed_count;
    size_t inspector_gone_count;
    WiFiMapperInspectorCategory inspector_category;
    size_t inspector_index;
    size_t inspector_item_count;
    bool inspector_has_item;
    WiFiMapperInspectorAp inspector_item;
    char inspector_status[24];
    char baseline_file_name[WIFI_MAPPER_FILE_NAME_SIZE];
    char current_file_name[WIFI_MAPPER_FILE_NAME_SIZE];
    bool locator_running;
    bool locator_seen;
    int32_t locator_rssi;
    int32_t locator_peak_rssi;
    WiFiMapperLocatorTrend locator_trend;
    uint32_t locator_samples;
} WiFiMapperModel;

typedef struct {
    Gui* gui;
    Storage* storage;
    NotificationApp* notification;
    Bt* bt;
    FuriPubSub* bridge_pubsub;
    FuriPubSubSubscription* bridge_sub;
    Expansion* expansion;
    ViewDispatcher* view_dispatcher;
    View* view;
    FuriThread* worker_thread;
    FuriStreamBuffer* rx_stream;
    FuriMessageQueue* bridge_queue;
    FuriMutex* mutex;
    FuriHalSerialHandle* serial_handle;
    File* log_file;
    char log_path[WIFI_MAPPER_PATH_SIZE];
    char log_temp_path[WIFI_MAPPER_PATH_SIZE];
    char line[WIFI_MAPPER_LINE_SIZE];
    size_t line_len;
    WiFiMapperScreen screen;
    WiFiMapperSessionPage session_page;
    bool logging;
    bool uart_ready;
    bool ble_relay;
    bool expansion_disabled;
    char relay_buffer[WIFI_MAPPER_RELAY_PAYLOAD_MAX + 1U];
    size_t relay_buffer_len;
    uint32_t relay_last_send_ms;
    WiFiMapperGpsState gps_state;
    uint32_t gps_request_id_next;
    uint32_t gps_pending_request_id;
    uint32_t gps_request_started_at;
    uint8_t gps_response_chunk_count;
    uint8_t gps_response_next_chunk;
    size_t gps_response_size;
    uint8_t gps_response[WIFI_MAPPER_GPS_RESPONSE_MAX + 1U];
    char gps_latitude[WIFI_MAPPER_GEO_SIZE];
    char gps_longitude[WIFI_MAPPER_GEO_SIZE];
    char gps_altitude[WIFI_MAPPER_GEO_SIZE];
    char gps_accuracy[WIFI_MAPPER_GEO_SIZE];
    WiFiMapperScanMode scan_mode;
    WiFiMapperExportMode export_mode;
    WiFiMapperSessionStats session;
    WiFiMapperInsights insights;
    uint32_t lines;
    uint32_t wifi_records;
    uint32_t errors;
    char status[24];
    char file_name[WIFI_MAPPER_FILE_NAME_SIZE];
    char last_line[WIFI_MAPPER_LAST_LINE_SIZE];
    WiFiMapperInspectorComparison* inspector;
    WiFiMapperInspectorCategory inspector_category;
    size_t inspector_index;
    char inspector_status[24];
    char baseline_file_name[WIFI_MAPPER_FILE_NAME_SIZE];
    char inspector_current_file_name[WIFI_MAPPER_FILE_NAME_SIZE];
    bool inspector_loaded;
    bool inspector_has_baseline;
    bool locator_running;
    bool locator_seen;
    int32_t locator_rssi;
    int32_t locator_peak_rssi;
    int32_t locator_previous_rssi;
    WiFiMapperLocatorTrend locator_trend;
    uint32_t locator_samples;
    char locator_bssid[WIFI_MAPPER_BSSID_SIZE];
} WiFiMapperApp;

static const NotificationSequence sequence_wifi_mapper_rx = {
    &message_display_backlight_on,
    &message_green_255,
    &message_delay_10,
    NULL,
};

static const NotificationSequence sequence_wifi_mapper_error = {
    &message_display_backlight_on,
    &message_red_255,
    &message_delay_10,
    NULL,
};

static void wifi_mapper_update_model(WiFiMapperApp* app);
static bool wifi_mapper_parse_export_row(const char* line, WiFiMapperExportRow* row);
static void wifi_mapper_write_geojson_feature(
    File* file,
    const WiFiMapperExportRow* row,
    bool first_feature);
static void wifi_mapper_write_clean_geojson_feature(
    File* file,
    const WiFiMapperExportAggregate* aggregate,
    bool first_feature);

static void wifi_mapper_update_model(WiFiMapperApp* app) {
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    size_t inspector_item_count = 0U;
    const WiFiMapperInspectorAp* inspector_item = NULL;
    bool inspector_partial = false;
    if(app->inspector) {
        inspector_item_count =
            wifi_mapper_inspector_category_count(app->inspector, app->inspector_category);
        inspector_item = wifi_mapper_inspector_category_get(
            app->inspector, app->inspector_category, app->inspector_index);
        inspector_partial = (app->inspector->baseline.overflow_count > 0U) ||
                            (app->inspector->current.overflow_count > 0U);
    }
    with_view_model(
        app->view,
        WiFiMapperModel * model,
        {
            model->screen = app->screen;
            model->session_page = app->session_page;
            model->logging = app->logging;
            model->uart_ready = app->uart_ready;
            model->ble_relay = app->ble_relay;
            model->gps_state = app->gps_state;
            model->scan_mode = app->scan_mode;
            model->export_mode = app->export_mode;
            model->session = app->session;
            model->insights = app->insights;
            model->lines = app->lines;
            model->wifi_records = app->wifi_records;
            model->errors = app->errors;
            strlcpy(model->status, app->status, sizeof(model->status));
            strlcpy(model->file_name, app->file_name, sizeof(model->file_name));
            strlcpy(model->last_line, app->last_line, sizeof(model->last_line));
            model->inspector_loaded = app->inspector_loaded;
            model->inspector_has_baseline = app->inspector_has_baseline;
            model->inspector_partial = inspector_partial;
            model->inspector_new_count = app->inspector ? app->inspector->new_count : 0U;
            model->inspector_changed_count = app->inspector ? app->inspector->changed_count : 0U;
            model->inspector_gone_count = app->inspector ? app->inspector->gone_count : 0U;
            model->inspector_category = app->inspector_category;
            model->inspector_index = app->inspector_index;
            model->inspector_item_count = inspector_item_count;
            model->inspector_has_item = inspector_item != NULL;
            if(inspector_item) {
                model->inspector_item = *inspector_item;
            } else {
                memset(&model->inspector_item, 0, sizeof(model->inspector_item));
            }
            strlcpy(
                model->inspector_status, app->inspector_status, sizeof(model->inspector_status));
            strlcpy(
                model->baseline_file_name,
                app->baseline_file_name,
                sizeof(model->baseline_file_name));
            strlcpy(
                model->current_file_name,
                app->inspector_current_file_name,
                sizeof(model->current_file_name));
            model->locator_running = app->locator_running;
            model->locator_seen = app->locator_seen;
            model->locator_rssi = app->locator_rssi;
            model->locator_peak_rssi = app->locator_peak_rssi;
            model->locator_trend = app->locator_trend;
            model->locator_samples = app->locator_samples;
        },
        true);
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
}

static void wifi_mapper_switch_screen(WiFiMapperApp* app, WiFiMapperScreen screen) {
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    app->screen = screen;
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
    wifi_mapper_update_model(app);
}

static uint32_t wifi_mapper_exit(void* context) {
    WiFiMapperApp* app = context;
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    const bool return_to_live = app->screen != WiFiMapperScreenLive;
    if(return_to_live) {
        app->screen = WiFiMapperScreenLive;
    }
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);

    if(return_to_live) {
        wifi_mapper_update_model(app);
        return 0;
    }

    return VIEW_NONE;
}

static void wifi_mapper_send_command(WiFiMapperApp* app, const char* command) {
    if(!app->serial_handle) {
        return;
    }

    furi_hal_serial_tx(app->serial_handle, (const uint8_t*)command, strlen(command));
    furi_hal_serial_tx_wait_complete(app->serial_handle);
}

static const WiFiMapperScanModeConfig*
    wifi_mapper_get_scan_mode_config(WiFiMapperScanMode scan_mode) {
    if(scan_mode >= WiFiMapperScanModeCount) {
        scan_mode = WiFiMapperScanModeAll;
    }

    return &wifi_mapper_scan_modes[scan_mode];
}

static const char* wifi_mapper_get_export_mode_label(WiFiMapperExportMode export_mode) {
    return export_mode == WiFiMapperExportModeRaw ? "Raw" : "Clean";
}

static void wifi_mapper_set_status(WiFiMapperApp* app, const char* status) {
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    strlcpy(app->status, status, sizeof(app->status));
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
    wifi_mapper_update_model(app);
}

static void wifi_mapper_toggle_export_mode(WiFiMapperApp* app) {
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    app->export_mode = app->export_mode == WiFiMapperExportModeClean ? WiFiMapperExportModeRaw :
                                                                       WiFiMapperExportModeClean;
    strlcpy(
        app->session.status,
        wifi_mapper_get_export_mode_label(app->export_mode),
        sizeof(app->session.status));
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);

    wifi_mapper_update_model(app);
}

static void wifi_mapper_send_active_scan_command(WiFiMapperApp* app) {
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    const WiFiMapperScanMode scan_mode = app->scan_mode;
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);

    const WiFiMapperScanModeConfig* config = wifi_mapper_get_scan_mode_config(scan_mode);
    wifi_mapper_send_command(app, config->command);
    wifi_mapper_set_status(app, config->sent_status);
}

static void wifi_mapper_set_scan_mode(WiFiMapperApp* app, WiFiMapperScanMode scan_mode) {
    const WiFiMapperScanModeConfig* config = wifi_mapper_get_scan_mode_config(scan_mode);

    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    app->scan_mode = scan_mode;
    strlcpy(app->status, config->label, sizeof(app->status));
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);

    wifi_mapper_update_model(app);
}

static void wifi_mapper_next_scan_mode(WiFiMapperApp* app) {
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    WiFiMapperScanMode scan_mode = (WiFiMapperScanMode)(app->scan_mode + 1);
    if(scan_mode >= WiFiMapperScanModeCount) {
        scan_mode = WiFiMapperScanModeAll;
    }
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);

    wifi_mapper_set_scan_mode(app, scan_mode);
}

static void wifi_mapper_previous_scan_mode(WiFiMapperApp* app) {
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    WiFiMapperScanMode scan_mode = app->scan_mode;
    if(scan_mode == WiFiMapperScanModeAll) {
        scan_mode = (WiFiMapperScanMode)(WiFiMapperScanModeCount - 1);
    } else {
        scan_mode--;
    }
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);

    wifi_mapper_set_scan_mode(app, scan_mode);
}

static bool wifi_mapper_make_log_path(WiFiMapperApp* app) {
    DateTime now;
    furi_hal_rtc_get_datetime(&now);
    snprintf(
        app->log_path,
        sizeof(app->log_path),
        WIFI_MAPPER_SESSIONS_DIR "/wifi_%04u%02u%02u_%02u%02u%02u.csv",
        now.year,
        now.month,
        now.day,
        now.hour,
        now.minute,
        now.second);
    strlcpy(app->log_temp_path, app->log_path, sizeof(app->log_temp_path));
    strlcat(app->log_temp_path, ".part", sizeof(app->log_temp_path));
    snprintf(
        app->file_name,
        sizeof(app->file_name),
        "wifi_%04u%02u%02u_%02u%02u%02u.csv",
        now.year,
        now.month,
        now.day,
        now.hour,
        now.minute,
        now.second);
    return true;
}

static bool wifi_mapper_open_log(WiFiMapperApp* app) {
    storage_common_mkdir(app->storage, WIFI_MAPPER_DATA_DIR);
    storage_common_mkdir(app->storage, WIFI_MAPPER_SESSIONS_DIR);

    wifi_mapper_make_log_path(app);
    app->log_file = storage_file_alloc(app->storage);
    storage_common_remove(app->storage, app->log_temp_path);
    if(!storage_file_open(app->log_file, app->log_temp_path, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        storage_file_free(app->log_file);
        app->log_file = NULL;
        return false;
    }

    const char* header = "tick_ms,type,rssi,channel,bssid,ssid,auth,lat,lon,alt,accuracy,raw\n";
    storage_file_write(app->log_file, header, strlen(header));
    return true;
}

static void wifi_mapper_close_log(WiFiMapperApp* app) {
    if(app->log_file) {
        storage_file_sync(app->log_file);
        storage_file_close(app->log_file);
        storage_file_free(app->log_file);
        app->log_file = NULL;
        storage_common_remove(app->storage, app->log_path);
        if(storage_common_rename(app->storage, app->log_temp_path, app->log_path) != FSE_OK) {
            strlcpy(app->status, "Commit error", sizeof(app->status));
            app->errors++;
        }
    }
}

static const char* wifi_mapper_skip_spaces(const char* cursor) {
    while(*cursor && isspace((unsigned char)*cursor)) {
        cursor++;
    }

    return cursor;
}

static bool wifi_mapper_parse_i32(const char** cursor, int32_t* value) {
    const char* start = wifi_mapper_skip_spaces(*cursor);
    char* end = NULL;
    const long parsed = strtol(start, &end, 10);
    if(end == start) {
        return false;
    }

    *cursor = end;
    *value = parsed;
    return true;
}

static bool wifi_mapper_parse_u32_field(const char* field, uint32_t* value) {
    const char* cursor = field;
    int32_t parsed = 0;
    if(!wifi_mapper_parse_i32(&cursor, &parsed) || (parsed < 0)) {
        return false;
    }

    *value = (uint32_t)parsed;
    return true;
}

static bool wifi_mapper_starts_with_nocase(const char* text, const char* prefix) {
    while(*prefix) {
        if(tolower((unsigned char)*text) != tolower((unsigned char)*prefix)) {
            return false;
        }
        text++;
        prefix++;
    }

    return true;
}

static bool wifi_mapper_geo_number_valid(const char* value) {
    bool has_digit = false;
    bool has_dot = false;
    if((*value == '-') || (*value == '+')) {
        value++;
    }

    while(*value) {
        if(isdigit((unsigned char)*value)) {
            has_digit = true;
        } else if((*value == '.') && !has_dot) {
            has_dot = true;
        } else {
            return false;
        }
        value++;
    }

    return has_digit;
}

static bool wifi_mapper_geo_parse(const char* value, float* parsed) {
    if(!wifi_mapper_geo_number_valid(value)) {
        return false;
    }

    char* end = NULL;
    *parsed = strtof(value, &end);
    return end && (*end == '\0');
}

static bool wifi_mapper_geo_coordinates_valid(const char* latitude, const char* longitude) {
    float lat = 0.0f;
    float lon = 0.0f;
    if(!wifi_mapper_geo_parse(latitude, &lat) || !wifi_mapper_geo_parse(longitude, &lon)) {
        return false;
    }

    if((lat < -90.0f) || (lat > 90.0f) || (lon < -180.0f) || (lon > 180.0f)) {
        return false;
    }

    // Marauder reports 0/0 while the GPS has no fix. Do not map that as a real point.
    if((lat > -0.000001f) && (lat < 0.000001f) && (lon > -0.000001f) && (lon < 0.000001f)) {
        return false;
    }

    return true;
}

static bool wifi_mapper_copy_bridge_field(
    const char* payload,
    const char* key,
    char* output,
    size_t output_size) {
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

static bool wifi_mapper_is_mac(const char* cursor) {
    if(!cursor || (strlen(cursor) < 17U)) {
        return false;
    }

    for(size_t i = 0; i < 17U; i++) {
        if((i % 3U) == 2U) {
            if(cursor[i] != ':') {
                return false;
            }
        } else if(!isxdigit((unsigned char)cursor[i])) {
            return false;
        }
    }

    return true;
}

static const char* wifi_mapper_find_mac(const char* text) {
    for(const char* cursor = text; *cursor; cursor++) {
        if(wifi_mapper_is_mac(cursor)) {
            return cursor;
        }
    }

    return NULL;
}

static bool wifi_mapper_csv_read_field(const char** cursor, char* value, size_t value_size) {
    size_t offset = 0;
    bool quoted = false;

    if(**cursor == '"') {
        quoted = true;
        (*cursor)++;
    }

    while(**cursor) {
        if(quoted) {
            if(**cursor == '"') {
                if((*cursor)[1] == '"') {
                    if(offset < (value_size - 1U)) {
                        value[offset++] = '"';
                    }
                    *cursor += 2;
                    continue;
                }

                (*cursor)++;
                if(**cursor == ',') {
                    (*cursor)++;
                }
                break;
            }
        } else if(**cursor == ',') {
            (*cursor)++;
            break;
        }

        if(offset < (value_size - 1U)) {
            value[offset++] = **cursor;
        }
        (*cursor)++;
    }

    value[offset] = '\0';
    return true;
}

static bool wifi_mapper_parse_marauder_ap_line(const char* line, WiFiMapperRecord* record) {
    const char* cursor = line;
    int32_t rssi = 0;
    int32_t channel = 0;

    if(!wifi_mapper_parse_i32(&cursor, &rssi)) {
        return false;
    }

    cursor = wifi_mapper_skip_spaces(cursor);
    if(!wifi_mapper_starts_with_nocase(cursor, "Ch")) {
        return false;
    }
    cursor += 2;
    if(*cursor == ':') {
        cursor++;
    }

    if(!wifi_mapper_parse_i32(&cursor, &channel) || (channel < 0) || (channel > UINT8_MAX)) {
        return false;
    }

    cursor = wifi_mapper_skip_spaces(cursor);
    if(!wifi_mapper_is_mac(cursor)) {
        return false;
    }
    strlcpy(record->bssid, cursor, sizeof(record->bssid));
    cursor += 17;

    cursor = wifi_mapper_skip_spaces(cursor);
    if(wifi_mapper_starts_with_nocase(cursor, "ESSID")) {
        cursor += 5;
    } else if(wifi_mapper_starts_with_nocase(cursor, "SSID")) {
        cursor += 4;
    } else {
        return false;
    }
    if(*cursor == ':') {
        cursor++;
    }

    record->is_wifi = true;
    record->type = "ap";
    record->rssi = rssi;
    record->channel = (uint8_t)channel;
    strlcpy(record->ssid, wifi_mapper_skip_spaces(cursor), sizeof(record->ssid));
    return true;
}

static bool wifi_mapper_parse_marauder_wardrive_line(const char* line, WiFiMapperRecord* record) {
    const char* cursor = line;
    char field[WIFI_MAPPER_CSV_FIELD_SIZE];
    char bssid_copy[WIFI_MAPPER_BSSID_SIZE];
    char ssid[WIFI_MAPPER_SSID_SIZE];
    char auth[WIFI_MAPPER_AUTH_SIZE];
    char latitude[WIFI_MAPPER_GEO_SIZE];
    char longitude[WIFI_MAPPER_GEO_SIZE];
    char altitude[WIFI_MAPPER_GEO_SIZE];
    char accuracy[WIFI_MAPPER_GEO_SIZE];

    wifi_mapper_csv_read_field(&cursor, field, sizeof(field));
    const char* bssid = wifi_mapper_find_mac(field);
    if(!bssid) {
        return false;
    }
    strlcpy(bssid_copy, bssid, sizeof(bssid_copy));

    wifi_mapper_csv_read_field(&cursor, ssid, sizeof(ssid));
    if(wifi_mapper_starts_with_nocase(ssid, "SSID")) {
        return false;
    }
    wifi_mapper_csv_read_field(&cursor, auth, sizeof(auth));
    wifi_mapper_csv_read_field(&cursor, field, sizeof(field));

    wifi_mapper_csv_read_field(&cursor, field, sizeof(field));
    const char* channel_cursor = field;
    int32_t channel = 0;
    if(!wifi_mapper_parse_i32(&channel_cursor, &channel) || (channel < 0) ||
       (channel > UINT8_MAX)) {
        return false;
    }

    wifi_mapper_csv_read_field(&cursor, field, sizeof(field));
    const char* rssi_cursor = field;
    int32_t rssi = 0;
    if(!wifi_mapper_parse_i32(&rssi_cursor, &rssi)) {
        return false;
    }

    wifi_mapper_csv_read_field(&cursor, latitude, sizeof(latitude));
    wifi_mapper_csv_read_field(&cursor, longitude, sizeof(longitude));
    wifi_mapper_csv_read_field(&cursor, altitude, sizeof(altitude));
    wifi_mapper_csv_read_field(&cursor, accuracy, sizeof(accuracy));
    const bool has_type = wifi_mapper_csv_read_field(&cursor, field, sizeof(field)) && field[0];
    if(has_type && !wifi_mapper_starts_with_nocase(field, "WIFI")) {
        return false;
    }

    record->is_wifi = true;
    record->type = "wardrive";
    record->rssi = rssi;
    record->channel = (uint8_t)channel;
    strlcpy(record->bssid, bssid_copy, sizeof(record->bssid));
    strlcpy(record->ssid, ssid, sizeof(record->ssid));
    strlcpy(record->auth, auth, sizeof(record->auth));
    strlcpy(record->latitude, latitude, sizeof(record->latitude));
    strlcpy(record->longitude, longitude, sizeof(record->longitude));
    strlcpy(record->altitude, altitude, sizeof(record->altitude));
    strlcpy(record->accuracy, accuracy, sizeof(record->accuracy));
    record->has_location = wifi_mapper_geo_coordinates_valid(latitude, longitude);
    return true;
}

static bool wifi_mapper_parse_structured_wifi_line(const char* line, WiFiMapperRecord* record) {
    const char* cursor = line;
    char field[WIFI_MAPPER_CSV_FIELD_SIZE];

    wifi_mapper_csv_read_field(&cursor, field, sizeof(field));
    if(!wifi_mapper_starts_with_nocase(field, "WIFI")) {
        return false;
    }

    wifi_mapper_csv_read_field(&cursor, record->bssid, sizeof(record->bssid));
    if(!wifi_mapper_is_mac(record->bssid)) {
        return false;
    }

    wifi_mapper_csv_read_field(&cursor, record->ssid, sizeof(record->ssid));
    wifi_mapper_csv_read_field(&cursor, record->auth, sizeof(record->auth));

    wifi_mapper_csv_read_field(&cursor, field, sizeof(field));
    const char* rssi_cursor = field;
    if(!wifi_mapper_parse_i32(&rssi_cursor, &record->rssi)) {
        return false;
    }

    wifi_mapper_csv_read_field(&cursor, field, sizeof(field));
    const char* channel_cursor = field;
    int32_t channel = 0;
    if(!wifi_mapper_parse_i32(&channel_cursor, &channel) || (channel < 0) ||
       (channel > UINT8_MAX)) {
        return false;
    }

    wifi_mapper_csv_read_field(&cursor, record->latitude, sizeof(record->latitude));
    wifi_mapper_csv_read_field(&cursor, record->longitude, sizeof(record->longitude));
    wifi_mapper_csv_read_field(&cursor, record->altitude, sizeof(record->altitude));
    wifi_mapper_csv_read_field(&cursor, record->accuracy, sizeof(record->accuracy));

    record->is_wifi = true;
    record->type = "wifi";
    record->channel = (uint8_t)channel;
    record->has_location = wifi_mapper_geo_coordinates_valid(record->latitude, record->longitude);
    return true;
}

static WiFiMapperRecord wifi_mapper_parse_record(const char* line) {
    WiFiMapperRecord record = {
        .is_wifi = false,
        .type = "raw",
    };

    if(wifi_mapper_starts_with_nocase(line, "WIFI,")) {
        if(!wifi_mapper_parse_structured_wifi_line(line, &record)) {
            record.is_wifi = true;
            record.type = "wifi";
        }
        return record;
    }

    if(!wifi_mapper_parse_marauder_wardrive_line(line, &record)) {
        wifi_mapper_parse_marauder_ap_line(line, &record);
    }
    return record;
}

static bool wifi_mapper_name_ends_with(const char* name, const char* suffix) {
    const size_t name_len = strlen(name);
    const size_t suffix_len = strlen(suffix);
    return (name_len >= suffix_len) && (strcmp(name + name_len - suffix_len, suffix) == 0);
}

static bool wifi_mapper_is_session_file_name(const char* name) {
    return (strncmp(name, "wifi_", 5) == 0) && wifi_mapper_name_ends_with(name, ".csv");
}

static bool wifi_mapper_find_latest_session(WiFiMapperApp* app, char* name, size_t name_size) {
    bool found = false;
    File* directory = storage_file_alloc(app->storage);
    if(storage_dir_open(directory, WIFI_MAPPER_SESSIONS_DIR)) {
        FileInfo file_info;
        char current_name[WIFI_MAPPER_FILE_NAME_SIZE];
        while(storage_dir_read(directory, &file_info, current_name, sizeof(current_name))) {
            if(file_info_is_dir(&file_info) || !wifi_mapper_is_session_file_name(current_name)) {
                continue;
            }

            if(!found || (strcmp(current_name, name) > 0)) {
                strlcpy(name, current_name, name_size);
                found = true;
            }
        }
    }

    storage_dir_close(directory);
    storage_file_free(directory);
    return found;
}

static bool wifi_mapper_find_adjacent_session(
    WiFiMapperApp* app,
    const char* current,
    int direction,
    char* name,
    size_t name_size) {
    bool found = false;
    File* directory = storage_file_alloc(app->storage);
    if(storage_dir_open(directory, WIFI_MAPPER_SESSIONS_DIR)) {
        FileInfo file_info;
        char candidate[WIFI_MAPPER_FILE_NAME_SIZE];
        while(storage_dir_read(directory, &file_info, candidate, sizeof(candidate))) {
            if(file_info_is_dir(&file_info) || !wifi_mapper_is_session_file_name(candidate)) {
                continue;
            }

            const int comparison = strcmp(candidate, current);
            const bool eligible = direction < 0 ? comparison < 0 : comparison > 0;
            if(!eligible) {
                continue;
            }

            if(!found ||
               (direction < 0 ? strcmp(candidate, name) > 0 : strcmp(candidate, name) < 0)) {
                strlcpy(name, candidate, name_size);
                found = true;
            }
        }
        storage_dir_close(directory);
    }

    storage_file_free(directory);
    return found;
}

static bool wifi_mapper_parse_csv_ap_row(
    const char* line,
    uint32_t* tick_ms,
    int32_t* rssi,
    uint8_t* channel,
    char* bssid,
    size_t bssid_size,
    char* ssid,
    size_t ssid_size,
    char* auth,
    size_t auth_size,
    bool* has_location) {
    const char* cursor = line;
    char field[WIFI_MAPPER_CSV_FIELD_SIZE];

    wifi_mapper_csv_read_field(&cursor, field, sizeof(field));
    if(!wifi_mapper_parse_u32_field(field, tick_ms)) {
        return false;
    }
    wifi_mapper_csv_read_field(&cursor, field, sizeof(field));
    if((strcmp(field, "ap") != 0) && (strcmp(field, "wardrive") != 0) &&
       (strcmp(field, "wifi") != 0)) {
        return false;
    }

    wifi_mapper_csv_read_field(&cursor, field, sizeof(field));
    const char* rssi_cursor = field;
    if(!wifi_mapper_parse_i32(&rssi_cursor, rssi)) {
        return false;
    }

    wifi_mapper_csv_read_field(&cursor, field, sizeof(field));
    const char* channel_cursor = field;
    int32_t channel_value = 0;
    if(!wifi_mapper_parse_i32(&channel_cursor, &channel_value) || (channel_value < 0) ||
       (channel_value > UINT8_MAX)) {
        return false;
    }

    wifi_mapper_csv_read_field(&cursor, field, sizeof(field));
    if(!wifi_mapper_is_mac(field)) {
        return false;
    }
    strlcpy(bssid, field, bssid_size);

    wifi_mapper_csv_read_field(&cursor, ssid, ssid_size);
    wifi_mapper_csv_read_field(&cursor, auth, auth_size);
    char latitude[WIFI_MAPPER_GEO_SIZE];
    char longitude[WIFI_MAPPER_GEO_SIZE];
    wifi_mapper_csv_read_field(&cursor, latitude, sizeof(latitude));
    wifi_mapper_csv_read_field(&cursor, longitude, sizeof(longitude));

    *channel = (uint8_t)channel_value;
    *has_location = wifi_mapper_geo_coordinates_valid(latitude, longitude);
    return true;
}

static bool wifi_mapper_bssid_seen(
    char unique_bssids[WIFI_MAPPER_MAX_UNIQUE][WIFI_MAPPER_BSSID_SIZE],
    uint32_t unique_count,
    const char* bssid) {
    for(uint32_t i = 0; i < unique_count; i++) {
        if(strcmp(unique_bssids[i], bssid) == 0) {
            return true;
        }
    }

    return false;
}

static void wifi_mapper_parse_session_line(
    WiFiMapperSessionStats* stats,
    WiFiMapperSessionScratch* scratch,
    int32_t* rssi_sum,
    const char* line) {
    if(strncmp(line, "tick_ms,", 8) == 0) {
        return;
    }

    stats->rows++;

    uint32_t tick_ms = 0U;
    int32_t rssi = 0;
    uint8_t channel = 0;
    char bssid[WIFI_MAPPER_BSSID_SIZE];
    char ssid[WIFI_MAPPER_SSID_SIZE];
    char auth[WIFI_MAPPER_AUTH_SIZE];
    bool has_location = false;
    if(!wifi_mapper_parse_csv_ap_row(
           line,
           &tick_ms,
           &rssi,
           &channel,
           bssid,
           sizeof(bssid),
           ssid,
           sizeof(ssid),
           auth,
           sizeof(auth),
           &has_location)) {
        return;
    }

    stats->aps++;
    if(has_location) {
        stats->located++;
        if((stats->mapped < WIFI_MAPPER_MAX_UNIQUE) &&
           !wifi_mapper_bssid_seen(scratch->mapped_bssids, stats->mapped, bssid)) {
            strlcpy(scratch->mapped_bssids[stats->mapped], bssid, WIFI_MAPPER_BSSID_SIZE);
            stats->mapped++;
        } else {
            stats->duplicates++;
        }
    }
    *rssi_sum += rssi;
    if((stats->aps == 1U) || (rssi > stats->best_rssi)) {
        stats->best_rssi = rssi;
    }

    wifi_mapper_insights_observe(&scratch->insights, bssid, ssid, auth, channel, rssi);
    UNUSED(tick_ms);
}

static void
    wifi_mapper_store_session_stats(WiFiMapperApp* app, const WiFiMapperSessionStats* stats) {
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    app->session = *stats;
    app->screen = WiFiMapperScreenSession;
    app->session_page = WiFiMapperSessionPageSummary;
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
    wifi_mapper_update_model(app);
}

static void wifi_mapper_read_session_stats(
    WiFiMapperApp* app,
    const char* file_name,
    WiFiMapperSessionStats* stats) {
    memset(stats, 0, sizeof(WiFiMapperSessionStats));
    strlcpy(stats->status, "No sessions", sizeof(stats->status));

    if(!file_name || !wifi_mapper_is_session_file_name(file_name)) {
        return;
    }

    WiFiMapperSessionScratch* scratch = malloc(sizeof(WiFiMapperSessionScratch));
    if(!scratch) {
        strlcpy(stats->status, "No memory", sizeof(stats->status));
        return;
    }
    memset(scratch, 0, sizeof(WiFiMapperSessionScratch));

    strlcpy(stats->file_name, file_name, sizeof(stats->file_name));
    char path[WIFI_MAPPER_PATH_SIZE];
    snprintf(path, sizeof(path), WIFI_MAPPER_SESSIONS_DIR "/%s", file_name);

    File* file = storage_file_alloc(app->storage);
    if(!storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        strlcpy(stats->status, "Read error", sizeof(stats->status));
        storage_file_free(file);
        free(scratch);
        return;
    }

    int32_t rssi_sum = 0;
    size_t line_len = 0;
    size_t bytes_read = 0;
    while((bytes_read = storage_file_read(file, scratch->buffer, sizeof(scratch->buffer))) > 0U) {
        for(size_t i = 0; i < bytes_read; i++) {
            const char data = (char)scratch->buffer[i];
            if((data == '\r') || (data == '\n')) {
                if(line_len > 0U) {
                    scratch->line[line_len] = '\0';
                    wifi_mapper_parse_session_line(stats, scratch, &rssi_sum, scratch->line);
                    line_len = 0U;
                }
            } else if(line_len < (sizeof(scratch->line) - 1U)) {
                scratch->line[line_len++] = data;
            }
        }
    }
    if(line_len > 0U) {
        scratch->line[line_len] = '\0';
        wifi_mapper_parse_session_line(stats, scratch, &rssi_sum, scratch->line);
    }

    storage_file_close(file);
    storage_file_free(file);

    if(stats->aps > 0U) {
        stats->loaded = true;
        stats->avg_rssi = rssi_sum / (int32_t)stats->aps;
        stats->observations = scratch->insights.observations;
        stats->unique = scratch->insights.unique_count;
        stats->truncated = scratch->insights.overflow_count > 0U;
        stats->open = scratch->insights.security_counts[WiFiMapperSecurityOpen];
        stats->legacy = scratch->insights.security_counts[WiFiMapperSecurityLegacy];
        stats->wpa2 = scratch->insights.security_counts[WiFiMapperSecurityWpa2];
        stats->wpa3 = scratch->insights.security_counts[WiFiMapperSecurityWpa3];
        stats->other_security = scratch->insights.security_counts[WiFiMapperSecurityOther];
        memcpy(
            stats->channel_counts,
            scratch->insights.channel_counts,
            sizeof(stats->channel_counts));
        strlcpy(stats->status, "Last session", sizeof(stats->status));
        stats->top_channel =
            wifi_mapper_insights_busiest_channel(&scratch->insights, &stats->top_channel_count);
    } else {
        strlcpy(stats->status, "No AP rows", sizeof(stats->status));
    }

    free(scratch);
}

static void wifi_mapper_analyze_session(WiFiMapperApp* app, const char* file_name) {
    WiFiMapperSessionStats stats;
    wifi_mapper_read_session_stats(app, file_name, &stats);

    if(stats.loaded) {
        char baseline_name[WIFI_MAPPER_FILE_NAME_SIZE] = "";
        if(wifi_mapper_find_adjacent_session(
               app, file_name, -1, baseline_name, sizeof(baseline_name))) {
            WiFiMapperSessionStats baseline;
            wifi_mapper_read_session_stats(app, baseline_name, &baseline);
            if(baseline.loaded) {
                stats.has_baseline = true;
                stats.baseline_truncated = baseline.truncated;
                strlcpy(
                    stats.baseline_file_name,
                    baseline.file_name,
                    sizeof(stats.baseline_file_name));
                stats.delta_unique = (int32_t)stats.unique - (int32_t)baseline.unique;
                stats.delta_observations =
                    (int32_t)stats.observations - (int32_t)baseline.observations;
                stats.delta_open = (int32_t)stats.open - (int32_t)baseline.open;
                stats.delta_wpa3 = (int32_t)stats.wpa3 - (int32_t)baseline.wpa3;
            }
        }
    }

    wifi_mapper_store_session_stats(app, &stats);
}

static void wifi_mapper_analyze_latest_session(WiFiMapperApp* app) {
    char file_name[WIFI_MAPPER_FILE_NAME_SIZE] = "";
    if(!wifi_mapper_find_latest_session(app, file_name, sizeof(file_name))) {
        WiFiMapperSessionStats stats = {.loaded = false};
        strlcpy(stats.status, "No sessions", sizeof(stats.status));
        wifi_mapper_store_session_stats(app, &stats);
        return;
    }
    wifi_mapper_analyze_session(app, file_name);
}

static void wifi_mapper_select_adjacent_session(WiFiMapperApp* app, int direction) {
    char current[WIFI_MAPPER_FILE_NAME_SIZE] = "";
    char adjacent[WIFI_MAPPER_FILE_NAME_SIZE] = "";
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    strlcpy(current, app->session.file_name, sizeof(current));
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);

    if(!current[0]) {
        wifi_mapper_analyze_latest_session(app);
        return;
    }
    if(wifi_mapper_find_adjacent_session(app, current, direction, adjacent, sizeof(adjacent))) {
        wifi_mapper_analyze_session(app, adjacent);
    } else {
        furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
        strlcpy(
            app->session.status,
            direction < 0 ? "Oldest session" : "Newest session",
            sizeof(app->session.status));
        furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
        wifi_mapper_update_model(app);
    }
}

static bool wifi_mapper_read_baseline_reference(
    WiFiMapperApp* app,
    char* file_name,
    size_t file_name_size) {
    if(!file_name || (file_name_size == 0U)) return false;
    file_name[0] = '\0';

    File* file = storage_file_alloc(app->storage);
    if(!storage_file_open(file, WIFI_MAPPER_BASELINE_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        storage_file_free(file);
        return false;
    }

    const size_t bytes_read = storage_file_read(file, file_name, file_name_size - 1U);
    storage_file_close(file);
    storage_file_free(file);
    file_name[bytes_read] = '\0';
    file_name[strcspn(file_name, "\r\n")] = '\0';
    if(!wifi_mapper_is_session_file_name(file_name)) {
        file_name[0] = '\0';
        return false;
    }

    char path[WIFI_MAPPER_PATH_SIZE];
    snprintf(path, sizeof(path), WIFI_MAPPER_SESSIONS_DIR "/%s", file_name);
    if(storage_common_stat(app->storage, path, NULL) != FSE_OK) {
        file_name[0] = '\0';
        return false;
    }
    return true;
}

static bool wifi_mapper_write_baseline_reference(WiFiMapperApp* app, const char* file_name) {
    if(!wifi_mapper_is_session_file_name(file_name)) return false;

    storage_common_mkdir(app->storage, WIFI_MAPPER_DATA_DIR);
    File* file = storage_file_alloc(app->storage);
    if(!storage_file_open(file, WIFI_MAPPER_BASELINE_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        storage_file_free(file);
        return false;
    }

    const size_t length = strlen(file_name);
    const bool written = storage_file_write(file, file_name, length) == length;
    const bool synced = written && storage_file_sync(file);
    storage_file_close(file);
    storage_file_free(file);
    return synced;
}

static void
    wifi_mapper_inspector_parse_line(WiFiMapperInspectorSnapshot* snapshot, const char* line) {
    if(strncmp(line, "tick_ms,", 8) == 0) return;

    uint32_t tick_ms = 0U;
    int32_t rssi = 0;
    uint8_t channel = 0U;
    char bssid[WIFI_MAPPER_BSSID_SIZE];
    char ssid[WIFI_MAPPER_SSID_SIZE];
    char auth[WIFI_MAPPER_AUTH_SIZE];
    bool has_location = false;
    if(!wifi_mapper_parse_csv_ap_row(
           line,
           &tick_ms,
           &rssi,
           &channel,
           bssid,
           sizeof(bssid),
           ssid,
           sizeof(ssid),
           auth,
           sizeof(auth),
           &has_location)) {
        return;
    }

    wifi_mapper_inspector_snapshot_observe(snapshot, bssid, ssid, auth, channel, rssi);
    UNUSED(tick_ms);
    UNUSED(has_location);
}

static bool wifi_mapper_read_inspector_snapshot(
    WiFiMapperApp* app,
    const char* file_name,
    WiFiMapperInspectorSnapshot* snapshot) {
    wifi_mapper_inspector_snapshot_reset(snapshot);
    if(!wifi_mapper_is_session_file_name(file_name)) return false;

    WiFiMapperInspectorReadScratch* scratch = malloc(sizeof(WiFiMapperInspectorReadScratch));
    if(!scratch) return false;
    memset(scratch, 0, sizeof(*scratch));

    char path[WIFI_MAPPER_PATH_SIZE];
    snprintf(path, sizeof(path), WIFI_MAPPER_SESSIONS_DIR "/%s", file_name);
    File* file = storage_file_alloc(app->storage);
    if(!storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        storage_file_free(file);
        free(scratch);
        return false;
    }

    size_t line_len = 0U;
    size_t bytes_read = 0U;
    while((bytes_read = storage_file_read(file, scratch->buffer, sizeof(scratch->buffer))) > 0U) {
        for(size_t index = 0U; index < bytes_read; index++) {
            const char data = (char)scratch->buffer[index];
            if((data == '\r') || (data == '\n')) {
                if(line_len > 0U) {
                    scratch->line[line_len] = '\0';
                    wifi_mapper_inspector_parse_line(snapshot, scratch->line);
                    line_len = 0U;
                }
            } else if(line_len < (sizeof(scratch->line) - 1U)) {
                scratch->line[line_len++] = data;
            }
        }
    }
    if(line_len > 0U) {
        scratch->line[line_len] = '\0';
        wifi_mapper_inspector_parse_line(snapshot, scratch->line);
    }

    storage_file_close(file);
    storage_file_free(file);
    free(scratch);
    return snapshot->count > 0U;
}

static void wifi_mapper_open_inspector(WiFiMapperApp* app) {
    char current_file[WIFI_MAPPER_FILE_NAME_SIZE] = "";
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    strlcpy(current_file, app->session.file_name, sizeof(current_file));
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);

    WiFiMapperInspectorComparison* comparison = malloc(sizeof(*comparison));
    if(!comparison) {
        wifi_mapper_set_status(app, "Inspector memory");
        return;
    }
    memset(comparison, 0, sizeof(*comparison));

    const bool current_loaded =
        wifi_mapper_read_inspector_snapshot(app, current_file, &comparison->current);
    char baseline_file[WIFI_MAPPER_FILE_NAME_SIZE] = "";
    const bool baseline_reference =
        wifi_mapper_read_baseline_reference(app, baseline_file, sizeof(baseline_file));
    const bool baseline_loaded =
        baseline_reference &&
        wifi_mapper_read_inspector_snapshot(app, baseline_file, &comparison->baseline);
    if(baseline_loaded) wifi_mapper_inspector_compare(comparison);

    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    WiFiMapperInspectorComparison* previous = app->inspector;
    app->inspector = comparison;
    app->inspector_loaded = current_loaded;
    app->inspector_has_baseline = baseline_loaded;
    app->inspector_category = WiFiMapperInspectorCategoryNew;
    app->inspector_index = 0U;
    app->screen = WiFiMapperScreenInspector;
    strlcpy(
        app->inspector_current_file_name, current_file, sizeof(app->inspector_current_file_name));
    strlcpy(app->baseline_file_name, baseline_file, sizeof(app->baseline_file_name));
    strlcpy(
        app->inspector_status,
        !current_loaded ?
            "Current unreadable" :
        !baseline_loaded ?
            "Set a baseline" :
            ((comparison->baseline.overflow_count || comparison->current.overflow_count) ?
                 "Partial comparison" :
                 "Comparison ready"),
        sizeof(app->inspector_status));
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
    free(previous);
    wifi_mapper_update_model(app);
}

static void wifi_mapper_pin_current_baseline(WiFiMapperApp* app) {
    char current_file[WIFI_MAPPER_FILE_NAME_SIZE] = "";
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    const bool current_loaded = app->inspector_loaded;
    strlcpy(current_file, app->inspector_current_file_name, sizeof(current_file));
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);

    if(!current_loaded) {
        furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
        strlcpy(app->inspector_status, "No AP data to pin", sizeof(app->inspector_status));
        furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
        wifi_mapper_update_model(app);
        return;
    }

    if(!wifi_mapper_write_baseline_reference(app, current_file)) {
        furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
        strlcpy(app->inspector_status, "Baseline write error", sizeof(app->inspector_status));
        furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
        wifi_mapper_update_model(app);
        return;
    }

    wifi_mapper_open_inspector(app);
}

static void
    wifi_mapper_open_inspector_list(WiFiMapperApp* app, WiFiMapperInspectorCategory category) {
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    app->inspector_category = category;
    app->inspector_index = 0U;
    app->screen = WiFiMapperScreenInspectorList;
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
    wifi_mapper_update_model(app);
}

static void wifi_mapper_open_first_inspector_change(WiFiMapperApp* app) {
    WiFiMapperInspectorCategory category = WiFiMapperInspectorCategoryNew;
    bool has_changes = false;
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    if(app->inspector) {
        if(app->inspector->new_count > 0U) {
            category = WiFiMapperInspectorCategoryNew;
            has_changes = true;
        } else if(app->inspector->changed_count > 0U) {
            category = WiFiMapperInspectorCategoryChanged;
            has_changes = true;
        } else if(app->inspector->gone_count > 0U) {
            category = WiFiMapperInspectorCategoryGone;
            has_changes = true;
        }
    }
    if(!has_changes) {
        strlcpy(app->inspector_status, "No AP changes", sizeof(app->inspector_status));
    }
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);

    if(has_changes) {
        wifi_mapper_open_inspector_list(app, category);
    } else {
        wifi_mapper_update_model(app);
    }
}

static void wifi_mapper_inspector_change_item(WiFiMapperApp* app, int direction) {
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    const size_t count =
        app->inspector ?
            wifi_mapper_inspector_category_count(app->inspector, app->inspector_category) :
            0U;
    if(count > 0U) {
        if(direction < 0) {
            app->inspector_index = app->inspector_index == 0U ? count - 1U :
                                                                app->inspector_index - 1U;
        } else {
            app->inspector_index = (app->inspector_index + 1U) % count;
        }
    }
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
    wifi_mapper_update_model(app);
}

static void wifi_mapper_inspector_change_category(WiFiMapperApp* app, int direction) {
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    int category = (int)app->inspector_category + direction;
    if(category < 0) category = (int)WiFiMapperInspectorCategoryCount - 1;
    if(category >= (int)WiFiMapperInspectorCategoryCount) category = 0;
    app->inspector_category = (WiFiMapperInspectorCategory)category;
    app->inspector_index = 0U;
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
    wifi_mapper_update_model(app);
}

static void wifi_mapper_locator_open(WiFiMapperApp* app) {
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    const WiFiMapperInspectorAp* item =
        app->inspector ? wifi_mapper_inspector_category_get(
                             app->inspector, app->inspector_category, app->inspector_index) :
                         NULL;
    if(item && wifi_mapper_inspector_category_is_locatable(app->inspector_category)) {
        strlcpy(app->locator_bssid, item->bssid, sizeof(app->locator_bssid));
        app->locator_running = false;
        app->locator_seen = false;
        app->locator_rssi = -100;
        app->locator_peak_rssi = -100;
        app->locator_previous_rssi = -100;
        app->locator_trend = WiFiMapperLocatorTrendWaiting;
        app->locator_samples = 0U;
        app->screen = WiFiMapperScreenLocator;
    } else {
        strlcpy(app->inspector_status, "Gone AP cannot scan", sizeof(app->inspector_status));
    }
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
    wifi_mapper_update_model(app);
}

static void wifi_mapper_locator_start(WiFiMapperApp* app) {
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    const bool can_start = app->serial_handle && !app->logging && app->locator_bssid[0];
    if(can_start) {
        app->locator_running = true;
        app->locator_seen = false;
        app->locator_rssi = -100;
        app->locator_peak_rssi = -100;
        app->locator_previous_rssi = -100;
        app->locator_trend = WiFiMapperLocatorTrendWaiting;
        app->locator_samples = 0U;
    } else {
        strlcpy(
            app->inspector_status,
            app->logging ? "Stop survey first" : "UART not ready",
            sizeof(app->inspector_status));
    }
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);

    if(can_start) wifi_mapper_send_command(app, WIFI_MAPPER_SCAN_ALL_COMMAND);
    wifi_mapper_update_model(app);
}

static void wifi_mapper_locator_stop(WiFiMapperApp* app) {
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    const bool was_running = app->locator_running;
    app->locator_running = false;
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);

    if(was_running) wifi_mapper_send_command(app, WIFI_MAPPER_STOP_COMMAND);
    wifi_mapper_update_model(app);
}

static bool wifi_mapper_make_export_name(
    const char* session_name,
    WiFiMapperExportMode export_mode,
    char* export_name,
    size_t export_name_size) {
    if(!wifi_mapper_is_session_file_name(session_name)) {
        return false;
    }

    strlcpy(export_name, session_name, export_name_size);
    char* extension = strstr(export_name, ".csv");
    if(!extension) {
        return false;
    }

    snprintf(
        extension,
        export_name_size - (size_t)(extension - export_name),
        "_%s.geojson",
        export_mode == WiFiMapperExportModeRaw ? "raw" : "clean");
    return true;
}

static WiFiMapperExportAggregate*
    wifi_mapper_find_export_aggregate(WiFiMapperExportScratch* scratch, const char* bssid) {
    for(uint32_t i = 0; i < scratch->aggregate_count; i++) {
        if(strcmp(scratch->aggregates[i].row.bssid, bssid) == 0) {
            return &scratch->aggregates[i];
        }
    }

    return NULL;
}

static void wifi_mapper_add_clean_export_row(
    WiFiMapperExportScratch* scratch,
    const WiFiMapperExportRow* row) {
    WiFiMapperExportAggregate* aggregate = wifi_mapper_find_export_aggregate(scratch, row->bssid);

    if(!aggregate) {
        if(scratch->aggregate_count >= WIFI_MAPPER_MAX_EXPORT_FEATURES) {
            return;
        }

        aggregate = &scratch->aggregates[scratch->aggregate_count++];
        memset(aggregate, 0, sizeof(WiFiMapperExportAggregate));
        aggregate->row = *row;
        aggregate->samples = 1;
        aggregate->first_tick_ms = row->tick_ms;
        aggregate->last_tick_ms = row->tick_ms;
        aggregate->best_rssi = row->rssi;
        aggregate->last_rssi = row->rssi;
        aggregate->rssi_sum = row->rssi;
        return;
    }

    aggregate->samples++;
    aggregate->last_tick_ms = row->tick_ms;
    aggregate->last_rssi = row->rssi;
    aggregate->rssi_sum += row->rssi;
    if(row->rssi > aggregate->best_rssi) {
        aggregate->best_rssi = row->rssi;
        aggregate->row = *row;
    }
}

static void wifi_mapper_process_export_line(
    File* geojson_file,
    WiFiMapperExportScratch* scratch,
    WiFiMapperExportMode export_mode,
    const char* line,
    uint32_t* exported) {
    WiFiMapperExportRow row;
    if(!wifi_mapper_parse_export_row(line, &row)) {
        return;
    }

    if(export_mode == WiFiMapperExportModeRaw) {
        wifi_mapper_write_geojson_feature(geojson_file, &row, *exported == 0U);
        (*exported)++;
    } else {
        wifi_mapper_add_clean_export_row(scratch, &row);
    }
}

static uint32_t wifi_mapper_export_csv_to_geojson(
    WiFiMapperApp* app,
    const char* session_name,
    const char* export_name,
    WiFiMapperExportMode export_mode) {
    char csv_path[WIFI_MAPPER_PATH_SIZE];
    char geojson_path[WIFI_MAPPER_PATH_SIZE];
    snprintf(csv_path, sizeof(csv_path), WIFI_MAPPER_SESSIONS_DIR "/%s", session_name);
    snprintf(geojson_path, sizeof(geojson_path), WIFI_MAPPER_EXPORTS_DIR "/%s", export_name);

    storage_common_mkdir(app->storage, WIFI_MAPPER_DATA_DIR);
    storage_common_mkdir(app->storage, WIFI_MAPPER_EXPORTS_DIR);

    WiFiMapperExportScratch* scratch = malloc(sizeof(WiFiMapperExportScratch));
    if(!scratch) {
        return 0;
    }
    memset(scratch, 0, sizeof(WiFiMapperExportScratch));

    File* csv_file = storage_file_alloc(app->storage);
    File* geojson_file = storage_file_alloc(app->storage);
    uint32_t exported = 0;

    do {
        if(!storage_file_open(csv_file, csv_path, FSAM_READ, FSOM_OPEN_EXISTING)) {
            break;
        }
        if(!storage_file_open(geojson_file, geojson_path, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
            break;
        }

        const char* header = "{\"type\":\"FeatureCollection\",\"features\":[\n";
        storage_file_write(geojson_file, header, strlen(header));

        size_t line_len = 0;
        size_t bytes_read = 0;
        while((bytes_read =
                   storage_file_read(csv_file, scratch->buffer, sizeof(scratch->buffer))) > 0U) {
            for(size_t i = 0; i < bytes_read; i++) {
                const char data = (char)scratch->buffer[i];
                if((data == '\r') || (data == '\n')) {
                    if(line_len > 0U) {
                        scratch->line[line_len] = '\0';
                        wifi_mapper_process_export_line(
                            geojson_file, scratch, export_mode, scratch->line, &exported);
                        line_len = 0U;
                    }
                } else if(line_len < (sizeof(scratch->line) - 1U)) {
                    scratch->line[line_len++] = data;
                }
            }
        }
        if(line_len > 0U) {
            scratch->line[line_len] = '\0';
            wifi_mapper_process_export_line(
                geojson_file, scratch, export_mode, scratch->line, &exported);
        }

        if(export_mode == WiFiMapperExportModeClean) {
            for(uint32_t i = 0; i < scratch->aggregate_count; i++) {
                wifi_mapper_write_clean_geojson_feature(
                    geojson_file, &scratch->aggregates[i], exported == 0U);
                exported++;
            }
        }

        storage_file_write(geojson_file, "\n]}\n", 4);
    } while(false);

    if(storage_file_is_open(csv_file)) {
        storage_file_close(csv_file);
    }
    if(storage_file_is_open(geojson_file)) {
        storage_file_close(geojson_file);
    }
    storage_file_free(csv_file);
    storage_file_free(geojson_file);
    free(scratch);
    return exported;
}

static void wifi_mapper_export_latest_session(WiFiMapperApp* app) {
    WiFiMapperSessionStats stats = {
        .loaded = false,
    };

    char session_name[WIFI_MAPPER_FILE_NAME_SIZE] = "";
    char export_name[WIFI_MAPPER_FILE_NAME_SIZE] = "";
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    const WiFiMapperExportMode export_mode = app->export_mode;
    strlcpy(session_name, app->session.file_name, sizeof(session_name));
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);

    if((!session_name[0] &&
        !wifi_mapper_find_latest_session(app, session_name, sizeof(session_name))) ||
       !wifi_mapper_make_export_name(session_name, export_mode, export_name, sizeof(export_name))) {
        strlcpy(stats.status, "No sessions", sizeof(stats.status));
        wifi_mapper_store_session_stats(app, &stats);
        return;
    }

    const uint32_t exported =
        wifi_mapper_export_csv_to_geojson(app, session_name, export_name, export_mode);
    wifi_mapper_analyze_session(app, session_name);

    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    app->session.exported = exported;
    strlcpy(app->session.export_name, export_name, sizeof(app->session.export_name));
    strlcpy(
        app->session.status,
        exported > 0U ? (export_mode == WiFiMapperExportModeRaw ? "Raw saved" : "Clean saved") :
                        "No GPS rows",
        sizeof(app->session.status));
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
    wifi_mapper_update_model(app);
}

static void wifi_mapper_write_escaped_csv(File* file, const char* line) {
    const char quote = '"';
    storage_file_write(file, &quote, 1);
    for(const char* cursor = line; *cursor; cursor++) {
        if(*cursor == '"') {
            storage_file_write(file, "\"\"", 2);
        } else if((*cursor >= ' ') && (*cursor <= '~')) {
            storage_file_write(file, cursor, 1);
        }
    }
    storage_file_write(file, &quote, 1);
}

static void wifi_mapper_write_escaped_json(File* file, const char* value) {
    storage_file_write(file, "\"", 1);
    for(const char* cursor = value; *cursor; cursor++) {
        switch(*cursor) {
        case '"':
            storage_file_write(file, "\\\"", 2);
            break;
        case '\\':
            storage_file_write(file, "\\\\", 2);
            break;
        case '\b':
            storage_file_write(file, "\\b", 2);
            break;
        case '\f':
            storage_file_write(file, "\\f", 2);
            break;
        case '\n':
            storage_file_write(file, "\\n", 2);
            break;
        case '\r':
            storage_file_write(file, "\\r", 2);
            break;
        case '\t':
            storage_file_write(file, "\\t", 2);
            break;
        default:
            if((unsigned char)*cursor >= ' ') {
                storage_file_write(file, cursor, 1);
            }
            break;
        }
    }
    storage_file_write(file, "\"", 1);
}

static bool wifi_mapper_parse_export_row(const char* line, WiFiMapperExportRow* row) {
    memset(row, 0, sizeof(WiFiMapperExportRow));

    const char* cursor = line;
    char field[WIFI_MAPPER_CSV_FIELD_SIZE];

    wifi_mapper_csv_read_field(&cursor, field, sizeof(field));
    wifi_mapper_parse_u32_field(field, &row->tick_ms);

    wifi_mapper_csv_read_field(&cursor, field, sizeof(field));
    if((strcmp(field, "ap") != 0) && (strcmp(field, "wardrive") != 0) &&
       (strcmp(field, "wifi") != 0)) {
        return false;
    }

    wifi_mapper_csv_read_field(&cursor, field, sizeof(field));
    const char* rssi_cursor = field;
    if(!wifi_mapper_parse_i32(&rssi_cursor, &row->rssi)) {
        return false;
    }

    wifi_mapper_csv_read_field(&cursor, field, sizeof(field));
    const char* channel_cursor = field;
    int32_t channel = 0;
    if(!wifi_mapper_parse_i32(&channel_cursor, &channel) || (channel < 0) ||
       (channel > UINT8_MAX)) {
        return false;
    }
    row->channel = (uint8_t)channel;

    wifi_mapper_csv_read_field(&cursor, row->bssid, sizeof(row->bssid));
    if(!wifi_mapper_is_mac(row->bssid)) {
        return false;
    }

    wifi_mapper_csv_read_field(&cursor, row->ssid, sizeof(row->ssid));
    wifi_mapper_csv_read_field(&cursor, row->auth, sizeof(row->auth));
    wifi_mapper_csv_read_field(&cursor, row->latitude, sizeof(row->latitude));
    wifi_mapper_csv_read_field(&cursor, row->longitude, sizeof(row->longitude));
    wifi_mapper_csv_read_field(&cursor, row->altitude, sizeof(row->altitude));
    wifi_mapper_csv_read_field(&cursor, row->accuracy, sizeof(row->accuracy));

    row->valid = wifi_mapper_geo_coordinates_valid(row->latitude, row->longitude);
    return row->valid;
}

static void wifi_mapper_write_geojson_feature(
    File* file,
    const WiFiMapperExportRow* row,
    bool first_feature) {
    if(!first_feature) {
        storage_file_write(file, ",\n", 2);
    }

    char buffer[128];
    snprintf(
        buffer,
        sizeof(buffer),
        "{\"type\":\"Feature\",\"geometry\":{\"type\":\"Point\",\"coordinates\":[%s,%s",
        row->longitude,
        row->latitude);
    storage_file_write(file, buffer, strlen(buffer));
    if(row->altitude[0]) {
        storage_file_write(file, ",", 1);
        storage_file_write(file, row->altitude, strlen(row->altitude));
    }
    const char* properties = "]},\"properties\":{";
    storage_file_write(file, properties, strlen(properties));

    storage_file_write(file, "\"ssid\":", 7);
    wifi_mapper_write_escaped_json(file, row->ssid);
    storage_file_write(file, ",\"bssid\":", 9);
    wifi_mapper_write_escaped_json(file, row->bssid);
    storage_file_write(file, ",\"auth\":", 8);
    wifi_mapper_write_escaped_json(file, row->auth);

    snprintf(
        buffer,
        sizeof(buffer),
        ",\"rssi\":%ld,\"channel\":%u,\"tick_ms\":%lu",
        (long)row->rssi,
        row->channel,
        (unsigned long)row->tick_ms);
    storage_file_write(file, buffer, strlen(buffer));

    if(row->accuracy[0]) {
        storage_file_write(file, ",\"accuracy\":", 12);
        storage_file_write(file, row->accuracy, strlen(row->accuracy));
    }
    storage_file_write(file, "}}", 2);
}

static void wifi_mapper_write_clean_geojson_feature(
    File* file,
    const WiFiMapperExportAggregate* aggregate,
    bool first_feature) {
    const WiFiMapperExportRow* row = &aggregate->row;
    if(!first_feature) {
        storage_file_write(file, ",\n", 2);
    }

    char buffer[256];
    snprintf(
        buffer,
        sizeof(buffer),
        "{\"type\":\"Feature\",\"geometry\":{\"type\":\"Point\",\"coordinates\":[%s,%s",
        row->longitude,
        row->latitude);
    storage_file_write(file, buffer, strlen(buffer));
    if(row->altitude[0]) {
        storage_file_write(file, ",", 1);
        storage_file_write(file, row->altitude, strlen(row->altitude));
    }
    const char* properties = "]},\"properties\":{";
    storage_file_write(file, properties, strlen(properties));

    storage_file_write(file, "\"ssid\":", 7);
    wifi_mapper_write_escaped_json(file, row->ssid);
    storage_file_write(file, ",\"bssid\":", 9);
    wifi_mapper_write_escaped_json(file, row->bssid);
    storage_file_write(file, ",\"auth\":", 8);
    wifi_mapper_write_escaped_json(file, row->auth);

    const int32_t avg_rssi =
        aggregate->samples ? (aggregate->rssi_sum / (int32_t)aggregate->samples) : row->rssi;
    snprintf(
        buffer,
        sizeof(buffer),
        ",\"channel\":%u,\"samples\":%lu,\"best_rssi\":%ld,\"last_rssi\":%ld,\"avg_rssi\":%ld,\"first_tick_ms\":%lu,\"last_tick_ms\":%lu",
        row->channel,
        (unsigned long)aggregate->samples,
        (long)aggregate->best_rssi,
        (long)aggregate->last_rssi,
        (long)avg_rssi,
        (unsigned long)aggregate->first_tick_ms,
        (unsigned long)aggregate->last_tick_ms);
    storage_file_write(file, buffer, strlen(buffer));

    if(row->accuracy[0]) {
        storage_file_write(file, ",\"accuracy\":", 12);
        storage_file_write(file, row->accuracy, strlen(row->accuracy));
    }
    storage_file_write(file, "}}", 2);
}

static void wifi_mapper_write_log_record(
    File* file,
    uint32_t tick_ms,
    const WiFiMapperRecord* record,
    const char* line) {
    char prefix[48];
    if(record->is_wifi && ((strcmp(record->type, "ap") == 0) ||
                           (strcmp(record->type, "wardrive") == 0) || record->has_location)) {
        snprintf(
            prefix,
            sizeof(prefix),
            "%lu,%s,%ld,%u,",
            (unsigned long)tick_ms,
            record->type,
            (long)record->rssi,
            record->channel);
        storage_file_write(file, prefix, strlen(prefix));
        wifi_mapper_write_escaped_csv(file, record->bssid);
        storage_file_write(file, ",", 1);
        wifi_mapper_write_escaped_csv(file, record->ssid);
        storage_file_write(file, ",", 1);
        wifi_mapper_write_escaped_csv(file, record->auth);
        storage_file_write(file, ",", 1);
        wifi_mapper_write_escaped_csv(file, record->latitude);
        storage_file_write(file, ",", 1);
        wifi_mapper_write_escaped_csv(file, record->longitude);
        storage_file_write(file, ",", 1);
        wifi_mapper_write_escaped_csv(file, record->altitude);
        storage_file_write(file, ",", 1);
        wifi_mapper_write_escaped_csv(file, record->accuracy);
        storage_file_write(file, ",", 1);
    } else {
        snprintf(prefix, sizeof(prefix), "%lu,%s,,,,,,,,,,", (unsigned long)tick_ms, record->type);
        storage_file_write(file, prefix, strlen(prefix));
    }

    wifi_mapper_write_escaped_csv(file, line);
    storage_file_write(file, "\n", 1);
}

static void wifi_mapper_write_phone_location_record(
    File* file,
    uint32_t tick_ms,
    const char* latitude,
    const char* longitude,
    const char* altitude,
    const char* accuracy) {
    char prefix[48];
    snprintf(prefix, sizeof(prefix), "%lu,location,,,,,,", (unsigned long)tick_ms);
    storage_file_write(file, prefix, strlen(prefix));
    wifi_mapper_write_escaped_csv(file, latitude);
    storage_file_write(file, ",", 1);
    wifi_mapper_write_escaped_csv(file, longitude);
    storage_file_write(file, ",", 1);
    wifi_mapper_write_escaped_csv(file, altitude);
    storage_file_write(file, ",", 1);
    wifi_mapper_write_escaped_csv(file, accuracy);
    storage_file_write(file, ",iphone_gps\n", 12);
}

// Must be called with app->mutex held. Sends whatever is buffered (if any) as one
// App Bridge event and resets the buffer. Best-effort: a failed/absent BLE link just
// drops this flush, matching how the rest of the app treats a missing connection.
static void wifi_mapper_relay_flush_locked(WiFiMapperApp* app) {
    if(app->relay_buffer_len == 0U) {
        return;
    }
    app->relay_buffer[app->relay_buffer_len] = '\0';
    if(app->bt) {
        bt_app_bridge_send_text_v2(
            app->bt, WIFI_MAPPER_RELAY_APP_ID, WIFI_MAPPER_RELAY_COMMAND, 0, 0, app->relay_buffer);
    }
    app->relay_buffer_len = 0U;
    app->relay_last_send_ms = (furi_get_tick() * 1000U) / furi_kernel_get_tick_frequency();
}

// Must be called with app->mutex held. State events let Companion delimit live
// sessions without relying on timing gaps between UART lines.
static void wifi_mapper_relay_state_locked(WiFiMapperApp* app, const char* command) {
    if(!app->bt) {
        return;
    }

    char payload[96];
    snprintf(
        payload,
        sizeof(payload),
        "schema=1;mode=%u;file=%s;aps=%lu;obs=%lu",
        (unsigned int)app->scan_mode,
        app->file_name,
        (unsigned long)app->insights.unique_count,
        (unsigned long)app->insights.observations);
    bt_app_bridge_send_text_v2(app->bt, WIFI_MAPPER_RELAY_APP_ID, command, 0, 0, payload);
}

// Must be called with app->mutex held. Lines are coalesced into fewer BLE
// sends, then flushed on the first line, after a short interval, before buffer
// overflow, on stop, on explicit disable, or on app exit.
static void wifi_mapper_relay_append_locked(WiFiMapperApp* app, const char* line) {
    const uint32_t now_ms = (furi_get_tick() * 1000U) / furi_kernel_get_tick_frequency();
    const size_t capacity = sizeof(app->relay_buffer) - 1U;
    const bool due = (now_ms - app->relay_last_send_ms) >= WIFI_MAPPER_RELAY_FLUSH_MS;

    size_t line_len = strlen(line);
    if(line_len > capacity) {
        line_len = capacity;
    }

    if(app->relay_buffer_len > 0U) {
        const size_t required = 1U + line_len; // newline plus line bytes
        if((app->relay_buffer_len + required) > capacity) {
            wifi_mapper_relay_flush_locked(app);
        }
    }

    if(app->relay_buffer_len > 0U) {
        app->relay_buffer[app->relay_buffer_len++] = '\n';
    }

    const size_t available = capacity - app->relay_buffer_len;
    if(line_len > available) {
        line_len = available;
    }
    if(line_len > 0U) {
        memcpy(app->relay_buffer + app->relay_buffer_len, line, line_len);
        app->relay_buffer_len += line_len;
    }

    if(due || (app->relay_buffer_len >= capacity)) {
        wifi_mapper_relay_flush_locked(app);
    }
}

static void wifi_mapper_toggle_ble_relay(WiFiMapperApp* app) {
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    if(!app->logging) {
        strlcpy(app->status, "Start first", sizeof(app->status));
        furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
        wifi_mapper_update_model(app);
        return;
    }
    app->ble_relay = !app->ble_relay;
    if(app->ble_relay) {
        app->relay_buffer_len = 0U;
        app->relay_last_send_ms = 0U;
        strlcpy(app->status, "BLE live on", sizeof(app->status));
    } else {
        wifi_mapper_relay_flush_locked(app);
        strlcpy(app->status, "BLE live off", sizeof(app->status));
    }
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
    wifi_mapper_update_model(app);
}

static void wifi_mapper_log_line(WiFiMapperApp* app, const char* line) {
    WiFiMapperRecord record = wifi_mapper_parse_record(line);

    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    if(record.is_wifi && !record.has_location && (app->gps_state == WiFiMapperGpsReady)) {
        strlcpy(record.latitude, app->gps_latitude, sizeof(record.latitude));
        strlcpy(record.longitude, app->gps_longitude, sizeof(record.longitude));
        strlcpy(record.altitude, app->gps_altitude, sizeof(record.altitude));
        strlcpy(record.accuracy, app->gps_accuracy, sizeof(record.accuracy));
        record.has_location = true;
    }
    if(app->logging && app->log_file) {
        const uint32_t tick_ms = (furi_get_tick() * 1000U) / furi_kernel_get_tick_frequency();
        wifi_mapper_write_log_record(app->log_file, tick_ms, &record, line);
    }
    if(app->ble_relay) {
        wifi_mapper_relay_append_locked(app, line);
    }

    app->lines++;
    if(record.is_wifi) {
        app->wifi_records++;
        wifi_mapper_insights_observe(
            &app->insights, record.bssid, record.ssid, record.auth, record.channel, record.rssi);
        if(app->locator_running && app->locator_bssid[0] &&
           (strcasecmp(record.bssid, app->locator_bssid) == 0)) {
            const bool had_previous = app->locator_seen;
            const int32_t previous_rssi = app->locator_rssi;
            app->locator_previous_rssi = previous_rssi;
            app->locator_rssi = record.rssi;
            if(!had_previous || (record.rssi > app->locator_peak_rssi)) {
                app->locator_peak_rssi = record.rssi;
            }
            app->locator_trend =
                wifi_mapper_locator_trend(had_previous, previous_rssi, record.rssi);
            app->locator_seen = true;
            app->locator_samples++;
        }
    }
    strlcpy(app->last_line, line, sizeof(app->last_line));
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
}

// Runs on the BT service thread. Only copy the bounded event and wake the worker.
static void wifi_mapper_bridge_callback(const void* message, void* context) {
    WiFiMapperApp* app = context;
    const BtAppBridgeEvent* event = message;
    if((event->protocol_version != 2U) || ((event->flags & BtAppBridgeFlagResponse) == 0U) ||
       (strcmp(event->app_id, WIFI_MAPPER_DEVICE_SERVICES_APP) != 0) ||
       (strcmp(event->command, "gps_once") != 0)) {
        return;
    }

    if(furi_message_queue_put(app->bridge_queue, event, 0U) == FuriStatusOk) {
        furi_thread_flags_set(furi_thread_get_id(app->worker_thread), WiFiMapperEventBridge);
    }
}

static void wifi_mapper_finish_gps_unavailable(WiFiMapperApp* app) {
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    app->gps_pending_request_id = 0U;
    app->gps_response_chunk_count = 0U;
    app->gps_response_next_chunk = 0U;
    app->gps_response_size = 0U;
    app->gps_state = WiFiMapperGpsUnavailable;
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
    wifi_mapper_update_model(app);
}

static void wifi_mapper_handle_gps_response(WiFiMapperApp* app, const BtAppBridgeEvent* event) {
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    if((app->gps_pending_request_id == 0U) || (event->request_id != app->gps_pending_request_id)) {
        furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
        return;
    }

    const bool invalid =
        ((event->flags & BtAppBridgeFlagError) != 0U) || (event->chunk_count == 0U) ||
        ((app->gps_response_chunk_count != 0U) &&
         (app->gps_response_chunk_count != event->chunk_count)) ||
        (event->chunk_index != app->gps_response_next_chunk) ||
        ((app->gps_response_size + event->payload_len) > WIFI_MAPPER_GPS_RESPONSE_MAX);
    if(invalid) {
        app->gps_pending_request_id = 0U;
        app->gps_state = WiFiMapperGpsUnavailable;
        furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
        wifi_mapper_update_model(app);
        return;
    }

    if(app->gps_response_chunk_count == 0U) {
        app->gps_response_chunk_count = event->chunk_count;
    }
    memcpy(&app->gps_response[app->gps_response_size], event->payload, event->payload_len);
    app->gps_response_size += event->payload_len;
    app->gps_response_next_chunk++;
    const bool complete = app->gps_response_next_chunk == app->gps_response_chunk_count;
    if(!complete) {
        furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
        return;
    }

    app->gps_response[app->gps_response_size] = '\0';
    char schema[4];
    char latitude[WIFI_MAPPER_GEO_SIZE];
    char longitude[WIFI_MAPPER_GEO_SIZE];
    char altitude[WIFI_MAPPER_GEO_SIZE];
    char accuracy[WIFI_MAPPER_GEO_SIZE];
    float parsed_altitude = 0.0f;
    float parsed_accuracy = 0.0f;
    const char* response = (const char*)app->gps_response;
    const bool valid =
        wifi_mapper_copy_bridge_field(response, "schema", schema, sizeof(schema)) &&
        (strcmp(schema, "1") == 0) &&
        wifi_mapper_copy_bridge_field(response, "lat", latitude, sizeof(latitude)) &&
        wifi_mapper_copy_bridge_field(response, "lon", longitude, sizeof(longitude)) &&
        wifi_mapper_copy_bridge_field(response, "alt", altitude, sizeof(altitude)) &&
        wifi_mapper_copy_bridge_field(response, "acc", accuracy, sizeof(accuracy)) &&
        wifi_mapper_geo_coordinates_valid(latitude, longitude) &&
        wifi_mapper_geo_parse(altitude, &parsed_altitude) &&
        wifi_mapper_geo_parse(accuracy, &parsed_accuracy) && (parsed_altitude >= -2000.0f) &&
        (parsed_altitude <= 100000.0f) && (parsed_accuracy >= 0.0f) &&
        (parsed_accuracy <= 100000.0f);

    app->gps_pending_request_id = 0U;
    if(valid) {
        strlcpy(app->gps_latitude, latitude, sizeof(app->gps_latitude));
        strlcpy(app->gps_longitude, longitude, sizeof(app->gps_longitude));
        strlcpy(app->gps_altitude, altitude, sizeof(app->gps_altitude));
        strlcpy(app->gps_accuracy, accuracy, sizeof(app->gps_accuracy));
        app->gps_state = WiFiMapperGpsReady;
        if(app->logging && app->log_file) {
            const uint32_t tick_ms = (furi_get_tick() * 1000U) / furi_kernel_get_tick_frequency();
            wifi_mapper_write_phone_location_record(
                app->log_file,
                tick_ms,
                app->gps_latitude,
                app->gps_longitude,
                app->gps_altitude,
                app->gps_accuracy);
        }
    } else {
        app->gps_state = WiFiMapperGpsUnavailable;
    }
    app->gps_response_chunk_count = 0U;
    app->gps_response_next_chunk = 0U;
    app->gps_response_size = 0U;
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);

    notification_message(
        app->notification, valid ? &sequence_blink_green_100 : &sequence_wifi_mapper_error);
    wifi_mapper_update_model(app);
}

static void wifi_mapper_request_phone_location(WiFiMapperApp* app) {
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    app->gps_request_id_next++;
    if(app->gps_request_id_next == 0U) app->gps_request_id_next++;
    app->gps_pending_request_id = app->gps_request_id_next;
    app->gps_request_started_at = furi_get_tick();
    app->gps_response_chunk_count = 0U;
    app->gps_response_next_chunk = 0U;
    app->gps_response_size = 0U;
    app->gps_latitude[0] = '\0';
    app->gps_longitude[0] = '\0';
    app->gps_altitude[0] = '\0';
    app->gps_accuracy[0] = '\0';
    app->gps_state = WiFiMapperGpsWaiting;
    const uint32_t request_id = app->gps_pending_request_id;
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);

    static const char payload[] = "schema=1;purpose=survey";
    if(!bt_app_bridge_send_v2(
           app->bt,
           WIFI_MAPPER_DEVICE_SERVICES_APP,
           "gps_once",
           request_id,
           BtAppBridgeFlagAckRequested,
           0U,
           1U,
           (const uint8_t*)payload,
           strlen(payload))) {
        wifi_mapper_finish_gps_unavailable(app);
    } else {
        wifi_mapper_update_model(app);
    }
}

static void wifi_mapper_check_gps_timeout(WiFiMapperApp* app) {
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    const bool timed_out = (app->gps_pending_request_id != 0U) &&
                           ((furi_get_tick() - app->gps_request_started_at) >=
                            furi_ms_to_ticks(WIFI_MAPPER_GPS_TIMEOUT_MS));
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
    if(timed_out) wifi_mapper_finish_gps_unavailable(app);
}

static void wifi_mapper_start_logging(WiFiMapperApp* app) {
    if(!app->serial_handle) {
        wifi_mapper_set_status(app, "UART not ready");
        notification_message(app->notification, &sequence_wifi_mapper_error);
        return;
    }

    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    const bool already_logging = app->logging;
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
    if(already_logging) {
        return;
    }

    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    const bool opened = wifi_mapper_open_log(app);
    if(opened) {
        wifi_mapper_insights_reset(&app->insights);
        app->lines = 0U;
        app->wifi_records = 0U;
        app->errors = 0U;
        app->ble_relay = true;
        app->relay_buffer_len = 0U;
        app->relay_last_send_ms = 0U;
        app->logging = true;
        strlcpy(app->status, "Survey live", sizeof(app->status));
        wifi_mapper_relay_state_locked(app, WIFI_MAPPER_RELAY_START_COMMAND);
    } else {
        app->logging = false;
        strlcpy(app->status, "Log error", sizeof(app->status));
    }
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);

    if(opened) {
        wifi_mapper_request_phone_location(app);
        wifi_mapper_send_active_scan_command(app);
        notification_message(app->notification, &sequence_wifi_mapper_rx);
    } else {
        notification_message(app->notification, &sequence_wifi_mapper_error);
    }
    wifi_mapper_update_model(app);
}

static void wifi_mapper_stop_logging(WiFiMapperApp* app) {
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    const bool active = app->logging || (app->log_file != NULL);
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
    if(!active) {
        return;
    }

    if(app->serial_handle) {
        wifi_mapper_send_command(app, WIFI_MAPPER_STOP_COMMAND);
    }

    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    const uint32_t errors_before_commit = app->errors;
    wifi_mapper_close_log(app);
    wifi_mapper_relay_flush_locked(app);
    wifi_mapper_relay_state_locked(app, WIFI_MAPPER_RELAY_STOP_COMMAND);
    app->gps_pending_request_id = 0U;
    if(app->gps_state == WiFiMapperGpsWaiting) app->gps_state = WiFiMapperGpsUnavailable;
    app->ble_relay = false;
    app->logging = false;
    strlcpy(
        app->status,
        app->errors > errors_before_commit ?
            "Commit error" :
            (app->uart_ready ? "Session saved" : "UART not ready"),
        sizeof(app->status));
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
    wifi_mapper_update_model(app);
}

static void wifi_mapper_draw_live(Canvas* canvas, WiFiMapperModel* model) {
    char line[48];

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 10, "TumoSurvey");
    canvas_set_font(canvas, FontSecondary);
    const char* uart = model->uart_ready ? "M1 OK" : "M1 --";
    canvas_draw_str(canvas, 128 - canvas_string_width(canvas, uart), 10, uart);

    snprintf(line, sizeof(line), "%s", wifi_mapper_get_scan_mode_config(model->scan_mode)->label);
    canvas_draw_str(canvas, 0, 24, line);
    char chips[16];
    chips[0] = '\0';
    if(model->logging) strlcat(chips, "LIVE ", sizeof(chips));
    if(model->ble_relay) strlcat(chips, "BLE", sizeof(chips));
    if(model->gps_state == WiFiMapperGpsReady) {
        strlcat(chips, " GPS", sizeof(chips));
    } else if(model->gps_state == WiFiMapperGpsWaiting) {
        strlcat(chips, " GPS?", sizeof(chips));
    } else if(model->gps_state == WiFiMapperGpsUnavailable) {
        strlcat(chips, " GPS--", sizeof(chips));
    }
    size_t clen = strlen(chips);
    if(clen > 0U && chips[clen - 1U] == ' ') chips[clen - 1U] = '\0';
    if(chips[0]) canvas_draw_str(canvas, 128 - canvas_string_width(canvas, chips), 24, chips);

    snprintf(
        line,
        sizeof(line),
        "AP %lu%s  obs %lu  open %lu",
        (unsigned long)model->insights.unique_count,
        model->insights.overflow_count > 0U ? "+" : "",
        (unsigned long)model->insights.observations,
        (unsigned long)model->insights.security_counts[WiFiMapperSecurityOpen]);
    canvas_draw_str(canvas, 0, 36, line);

    if(model->insights.has_strongest) {
        snprintf(
            line,
            sizeof(line),
            "%.12s  %ld dBm",
            model->insights.strongest_ssid,
            (long)model->insights.strongest_rssi);
    } else {
        strlcpy(line, model->status, sizeof(line));
    }
    canvas_draw_str(canvas, 0, 46, line);

    canvas_draw_line(canvas, 0, 50, 127, 50);
    elements_button_left(canvas, "Mode");
    elements_button_center(canvas, model->logging ? "Stop" : "Start");
    elements_button_right(canvas, "Data");
}

static void wifi_mapper_draw_insights(Canvas* canvas, WiFiMapperModel* model) {
    char line[48];
    uint32_t busiest_count = 0U;
    const uint8_t busiest = wifi_mapper_insights_busiest_channel(&model->insights, &busiest_count);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 10, "Survey Insights");
    canvas_set_font(canvas, FontSecondary);
    const char* state = model->logging ? "LIVE" : "IDLE";
    canvas_draw_str(canvas, 128 - canvas_string_width(canvas, state), 10, state);

    snprintf(
        line,
        sizeof(line),
        "AP %lu%s   observations %lu",
        (unsigned long)model->insights.unique_count,
        model->insights.overflow_count > 0U ? "+" : "",
        (unsigned long)model->insights.observations);
    canvas_draw_str(canvas, 0, 21, line);
    snprintf(
        line,
        sizeof(line),
        "Open %lu  Legacy %lu",
        (unsigned long)model->insights.security_counts[WiFiMapperSecurityOpen],
        (unsigned long)model->insights.security_counts[WiFiMapperSecurityLegacy]);
    canvas_draw_str(canvas, 0, 30, line);
    snprintf(
        line,
        sizeof(line),
        "WPA2 %lu  WPA3 %lu  Other %lu",
        (unsigned long)model->insights.security_counts[WiFiMapperSecurityWpa2],
        (unsigned long)model->insights.security_counts[WiFiMapperSecurityWpa3],
        (unsigned long)model->insights.security_counts[WiFiMapperSecurityOther]);
    canvas_draw_str(canvas, 0, 39, line);
    if(model->insights.has_strongest) {
        snprintf(
            line,
            sizeof(line),
            "Ch %u x%lu   best %ld dBm",
            busiest,
            (unsigned long)busiest_count,
            (long)model->insights.strongest_rssi);
    } else {
        strlcpy(line, "Waiting for AP data", sizeof(line));
    }
    canvas_draw_str(canvas, 0, 48, line);

    elements_button_left(canvas, "Back");
    elements_button_center(canvas, "Info");
    elements_button_right(canvas, "Saved");
}

static void wifi_mapper_draw_about(Canvas* canvas) {
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 10, "TumoSurvey v1.3.0");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 0, 22, "Survey + AP Inspector");
    canvas_draw_str(canvas, 0, 34, "Module One / API 88");
    canvas_draw_str(canvas, 0, 46, "squazaryu/tumoflip");
    elements_button_left(canvas, "Back");
}

static void wifi_mapper_draw_session(Canvas* canvas, WiFiMapperModel* model) {
    char line[48];

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 10, "Survey Sessions");

    canvas_set_font(canvas, FontSecondary);
    snprintf(
        line,
        sizeof(line),
        "%u/%u",
        (unsigned int)model->session_page + 1U,
        (unsigned int)WiFiMapperSessionPageCount);
    canvas_draw_str(canvas, 128 - canvas_string_width(canvas, line), 10, line);

    if(model->session.loaded) {
        if(model->session.file_name[0]) {
            canvas_draw_str(canvas, 0, 20, model->session.file_name);
        }
        if(model->session_page == WiFiMapperSessionPageSummary) {
            snprintf(
                line,
                sizeof(line),
                "AP %lu  unique %lu%s",
                (unsigned long)model->session.aps,
                (unsigned long)model->session.unique,
                model->session.truncated ? "+" : "");
            canvas_draw_str(canvas, 0, 30, line);
            snprintf(
                line,
                sizeof(line),
                "GPS %lu  mapped %lu",
                (unsigned long)model->session.located,
                (unsigned long)model->session.mapped);
            canvas_draw_str(canvas, 0, 39, line);
            snprintf(
                line,
                sizeof(line),
                "best/avg %ld/%ld dBm",
                (long)model->session.best_rssi,
                (long)model->session.avg_rssi);
            canvas_draw_str(canvas, 0, 48, line);
        } else if(model->session_page == WiFiMapperSessionPageSecurity) {
            snprintf(
                line,
                sizeof(line),
                "Open %lu  Legacy %lu",
                (unsigned long)model->session.open,
                (unsigned long)model->session.legacy);
            canvas_draw_str(canvas, 0, 30, line);
            snprintf(
                line,
                sizeof(line),
                "WPA2 %lu  WPA3 %lu",
                (unsigned long)model->session.wpa2,
                (unsigned long)model->session.wpa3);
            canvas_draw_str(canvas, 0, 39, line);
            snprintf(
                line,
                sizeof(line),
                "Other %lu  %s",
                (unsigned long)model->session.other_security,
                wifi_mapper_get_export_mode_label(model->export_mode));
            canvas_draw_str(canvas, 0, 48, line);
        } else if(model->session_page == WiFiMapperSessionPageChannels) {
            uint8_t shown = 0U;
            line[0] = '\0';
            for(uint8_t channel = 1U; channel < WIFI_MAPPER_INSIGHTS_CHANNEL_COUNT && shown < 3U;
                channel++) {
                if(model->session.channel_counts[channel] == 0U) continue;
                char item[16];
                snprintf(
                    item,
                    sizeof(item),
                    "%s%u:%lu",
                    shown == 0U ? "" : "  ",
                    channel,
                    (unsigned long)model->session.channel_counts[channel]);
                strlcat(line, item, sizeof(line));
                shown++;
            }
            canvas_draw_str(canvas, 0, 30, shown > 0U ? line : "No channel data");
            snprintf(
                line,
                sizeof(line),
                "Top channel %u x%lu",
                model->session.top_channel,
                (unsigned long)model->session.top_channel_count);
            canvas_draw_str(canvas, 0, 39, line);
            snprintf(
                line,
                sizeof(line),
                "Rows %lu  export %lu",
                (unsigned long)model->session.rows,
                (unsigned long)model->session.exported);
            canvas_draw_str(canvas, 0, 48, line);
        } else if(
            model->session.has_baseline &&
            (model->session.truncated || model->session.baseline_truncated)) {
            canvas_draw_str(canvas, 0, 30, "AP limit reached");
            canvas_draw_str(canvas, 0, 39, "Full comparison is in");
            canvas_draw_str(canvas, 0, 48, "Unleashed Companion");
        } else if(model->session.has_baseline) {
            snprintf(line, sizeof(line), "vs %.19s", model->session.baseline_file_name);
            canvas_draw_str(canvas, 0, 30, line);
            snprintf(
                line,
                sizeof(line),
                "AP %+ld  Open %+ld",
                (long)model->session.delta_unique,
                (long)model->session.delta_open);
            canvas_draw_str(canvas, 0, 39, line);
            snprintf(
                line,
                sizeof(line),
                "Obs %+ld  WPA3 %+ld",
                (long)model->session.delta_observations,
                (long)model->session.delta_wpa3);
            canvas_draw_str(canvas, 0, 48, line);
        } else {
            canvas_draw_str(canvas, 0, 32, "No earlier baseline");
            canvas_draw_str(canvas, 0, 44, "Record another session");
        }
    } else {
        canvas_draw_str(canvas, 0, 24, model->session.status);
        canvas_draw_str(canvas, 0, 34, "Press Up to analyze latest.");
    }

    elements_button_left(canvas, "Prev");
    elements_button_center(
        canvas, model->session_page == WiFiMapperSessionPageBaseline ? "Inspect" : "Export");
    elements_button_right(canvas, "Next");
}

static const char* wifi_mapper_short_session_name(const char* file_name) {
    if(!file_name) return "-";
    return strncmp(file_name, "wifi_", 5) == 0 ? file_name + 5 : file_name;
}

static void wifi_mapper_draw_inspector(Canvas* canvas, WiFiMapperModel* model) {
    char line[48];

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 10, "Survey Inspector");
    canvas_set_font(canvas, FontSecondary);

    snprintf(
        line,
        sizeof(line),
        "Base %.19s",
        model->inspector_has_baseline ? wifi_mapper_short_session_name(model->baseline_file_name) :
                                        "not set");
    canvas_draw_str(canvas, 0, 20, line);
    snprintf(
        line, sizeof(line), "Now  %.19s", wifi_mapper_short_session_name(model->current_file_name));
    canvas_draw_str(canvas, 0, 30, line);
    snprintf(
        line,
        sizeof(line),
        "New %u  Gone %u  Chg %u",
        (unsigned int)model->inspector_new_count,
        (unsigned int)model->inspector_gone_count,
        (unsigned int)model->inspector_changed_count);
    canvas_draw_str(canvas, 0, 40, line);
    canvas_draw_str(canvas, 0, 48, model->inspector_status);

    elements_button_left(canvas, "Pin");
    elements_button_center(canvas, "Changes");
    elements_button_right(canvas, "All");
}

static void wifi_mapper_draw_inspector_list(Canvas* canvas, WiFiMapperModel* model) {
    char line[48];

    canvas_set_font(canvas, FontPrimary);
    snprintf(
        line,
        sizeof(line),
        "%s %u/%u",
        wifi_mapper_inspector_category_label(model->inspector_category),
        model->inspector_has_item ? (unsigned int)model->inspector_index + 1U : 0U,
        (unsigned int)model->inspector_item_count);
    canvas_draw_str(canvas, 0, 10, line);
    canvas_set_font(canvas, FontSecondary);

    if(model->inspector_has_item) {
        snprintf(
            line,
            sizeof(line),
            "%.20s",
            model->inspector_item.ssid[0] ? model->inspector_item.ssid : "Hidden SSID");
        canvas_draw_str(canvas, 0, 20, line);
        canvas_draw_str(canvas, 0, 30, model->inspector_item.bssid);
        snprintf(
            line,
            sizeof(line),
            "Ch %u  %.12s",
            model->inspector_item.channel,
            model->inspector_item.auth[0] ? model->inspector_item.auth : "Unknown");
        canvas_draw_str(canvas, 0, 40, line);
        snprintf(
            line,
            sizeof(line),
            "best %ld  last %ld dBm",
            (long)model->inspector_item.best_rssi,
            (long)model->inspector_item.last_rssi);
        canvas_draw_str(canvas, 0, 48, line);
    } else {
        canvas_draw_str(canvas, 0, 25, "No entries in category");
        canvas_draw_str(canvas, 0, 38, "Up/Down changes view");
    }

    elements_button_left(canvas, "Prev");
    if(model->inspector_has_item &&
       wifi_mapper_inspector_category_is_locatable(model->inspector_category)) {
        elements_button_center(canvas, "Locate");
    }
    elements_button_right(canvas, "Next");
}

static void wifi_mapper_draw_locator(Canvas* canvas, WiFiMapperModel* model) {
    char line[48];
    const uint8_t strength = wifi_mapper_locator_strength_percent(model->locator_rssi);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 10, "RSSI Locator");
    canvas_set_font(canvas, FontSecondary);
    const char* state = model->locator_running ? "RUN" : "IDLE";
    canvas_draw_str(canvas, 128 - canvas_string_width(canvas, state), 10, state);

    snprintf(
        line,
        sizeof(line),
        "%.20s",
        model->inspector_item.ssid[0] ? model->inspector_item.ssid : "Hidden SSID");
    canvas_draw_str(canvas, 0, 20, line);
    canvas_draw_str(canvas, 0, 29, model->inspector_item.bssid);
    if(model->locator_seen) {
        snprintf(
            line,
            sizeof(line),
            "%ld dBm  peak %ld  %s",
            (long)model->locator_rssi,
            (long)model->locator_peak_rssi,
            wifi_mapper_locator_trend_label(model->locator_trend));
    } else {
        strlcpy(
            line,
            model->locator_running ? "Waiting for selected AP" : "Ready to scan",
            sizeof(line));
    }
    canvas_draw_str(canvas, 0, 39, line);
    elements_progress_bar(canvas, 0, 42, 128, (float)strength / 100.0f);

    elements_button_left(canvas, "Back");
    elements_button_center(canvas, model->locator_running ? "Stop" : "Start");
}

static void wifi_mapper_draw_callback(Canvas* canvas, void* context) {
    WiFiMapperModel* model = context;
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    if(model->screen == WiFiMapperScreenSession) {
        wifi_mapper_draw_session(canvas, model);
    } else if(model->screen == WiFiMapperScreenInspector) {
        wifi_mapper_draw_inspector(canvas, model);
    } else if(model->screen == WiFiMapperScreenInspectorList) {
        wifi_mapper_draw_inspector_list(canvas, model);
    } else if(model->screen == WiFiMapperScreenLocator) {
        wifi_mapper_draw_locator(canvas, model);
    } else if(model->screen == WiFiMapperScreenAbout) {
        wifi_mapper_draw_about(canvas);
    } else if(model->screen == WiFiMapperScreenInsights) {
        wifi_mapper_draw_insights(canvas, model);
    } else {
        wifi_mapper_draw_live(canvas, model);
    }
}

static void wifi_mapper_change_session_page(WiFiMapperApp* app, int direction) {
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    int page = (int)app->session_page + direction;
    if(page < 0) {
        page = (int)WiFiMapperSessionPageCount - 1;
    } else if(page >= (int)WiFiMapperSessionPageCount) {
        page = 0;
    }
    app->session_page = (WiFiMapperSessionPage)page;
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
    wifi_mapper_update_model(app);
}

static bool wifi_mapper_input_callback(InputEvent* event, void* context) {
    WiFiMapperApp* app = context;
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    const WiFiMapperScreen screen = app->screen;
    const bool logging = app->logging;
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);

    if(screen == WiFiMapperScreenLocator) {
        if(event->type != InputTypeShort) return false;
        if(event->key == InputKeyOk) {
            furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
            const bool running = app->locator_running;
            furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
            if(running) {
                wifi_mapper_locator_stop(app);
            } else {
                wifi_mapper_locator_start(app);
            }
            return true;
        } else if((event->key == InputKeyBack) || (event->key == InputKeyLeft)) {
            wifi_mapper_locator_stop(app);
            wifi_mapper_switch_screen(app, WiFiMapperScreenInspectorList);
            return true;
        }
        return false;
    }

    if(screen == WiFiMapperScreenInspectorList) {
        if(event->type != InputTypeShort) return false;
        if(event->key == InputKeyLeft) {
            wifi_mapper_inspector_change_item(app, -1);
            return true;
        } else if(event->key == InputKeyRight) {
            wifi_mapper_inspector_change_item(app, 1);
            return true;
        } else if(event->key == InputKeyUp) {
            wifi_mapper_inspector_change_category(app, -1);
            return true;
        } else if(event->key == InputKeyDown) {
            wifi_mapper_inspector_change_category(app, 1);
            return true;
        } else if(event->key == InputKeyOk) {
            wifi_mapper_locator_open(app);
            return true;
        } else if(event->key == InputKeyBack) {
            wifi_mapper_switch_screen(app, WiFiMapperScreenInspector);
            return true;
        }
        return false;
    }

    if(screen == WiFiMapperScreenInspector) {
        if(event->type != InputTypeShort) return false;
        if(event->key == InputKeyLeft) {
            wifi_mapper_pin_current_baseline(app);
            return true;
        } else if(event->key == InputKeyOk) {
            wifi_mapper_open_first_inspector_change(app);
            return true;
        } else if(event->key == InputKeyRight) {
            wifi_mapper_open_inspector_list(app, WiFiMapperInspectorCategoryCurrent);
            return true;
        } else if(event->key == InputKeyBack) {
            wifi_mapper_switch_screen(app, WiFiMapperScreenSession);
            return true;
        }
        return false;
    }

    if(screen == WiFiMapperScreenSession) {
        if((event->type == InputTypeLong) && (event->key == InputKeyOk)) {
            wifi_mapper_toggle_export_mode(app);
            return true;
        }
        if(event->type != InputTypeShort) {
            return false;
        }
        if(event->key == InputKeyUp) {
            wifi_mapper_change_session_page(app, -1);
            return true;
        } else if(event->key == InputKeyDown) {
            wifi_mapper_change_session_page(app, 1);
            return true;
        } else if(event->key == InputKeyOk) {
            furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
            const WiFiMapperSessionPage session_page = app->session_page;
            furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
            if(session_page == WiFiMapperSessionPageBaseline) {
                wifi_mapper_open_inspector(app);
            } else {
                wifi_mapper_export_latest_session(app);
            }
            return true;
        } else if(event->key == InputKeyLeft) {
            wifi_mapper_select_adjacent_session(app, -1);
            return true;
        } else if(event->key == InputKeyRight) {
            wifi_mapper_select_adjacent_session(app, 1);
            return true;
        } else if(event->key == InputKeyBack) {
            wifi_mapper_switch_screen(app, WiFiMapperScreenLive);
            return true;
        }

        return false;
    }

    if(screen == WiFiMapperScreenAbout) {
        if((event->type == InputTypeShort) &&
           ((event->key == InputKeyBack) || (event->key == InputKeyLeft))) {
            wifi_mapper_switch_screen(app, WiFiMapperScreenInsights);
            return true;
        }
        return false;
    }

    if(screen == WiFiMapperScreenInsights) {
        if(event->type != InputTypeShort) {
            return false;
        }
        if((event->key == InputKeyBack) || (event->key == InputKeyLeft)) {
            wifi_mapper_switch_screen(app, WiFiMapperScreenLive);
            return true;
        } else if(event->key == InputKeyRight) {
            if(logging) {
                wifi_mapper_set_status(app, "Stop first");
                wifi_mapper_switch_screen(app, WiFiMapperScreenLive);
            } else {
                wifi_mapper_analyze_latest_session(app);
            }
            return true;
        } else if((event->key == InputKeyOk) || (event->key == InputKeyDown)) {
            wifi_mapper_switch_screen(app, WiFiMapperScreenAbout);
            return true;
        }
        return false;
    }

    if((event->type == InputTypeLong) && (event->key == InputKeyOk)) {
        if(logging) {
            wifi_mapper_set_status(app, "Stop first");
        } else {
            wifi_mapper_analyze_latest_session(app);
        }
        return true;
    }

    if((event->type == InputTypeLong) && (event->key == InputKeyDown)) {
        wifi_mapper_toggle_ble_relay(app);
        return true;
    }

    if(event->type != InputTypeShort) {
        return false;
    }

    if(event->key == InputKeyOk) {
        if(logging) {
            wifi_mapper_stop_logging(app);
        } else {
            wifi_mapper_start_logging(app);
        }
        return true;
    } else if(event->key == InputKeyRight) {
        wifi_mapper_switch_screen(app, WiFiMapperScreenInsights);
        return true;
    } else if(event->key == InputKeyLeft) {
        if(logging) {
            wifi_mapper_set_status(app, "Stop first");
        } else {
            wifi_mapper_previous_scan_mode(app);
        }
        return true;
    } else if(event->key == InputKeyUp) {
        if(logging) {
            wifi_mapper_set_status(app, "Stop first");
        } else {
            wifi_mapper_next_scan_mode(app);
        }
        return true;
    }

    return false;
}

static void wifi_mapper_process_byte(WiFiMapperApp* app, char data) {
    if((data == '\r') || (data == '\n')) {
        if(app->line_len > 0U) {
            app->line[app->line_len] = '\0';
            wifi_mapper_log_line(app, app->line);
            app->line_len = 0U;
            wifi_mapper_update_model(app);
        }
        return;
    }

    if((data >= ' ') && (data <= '~')) {
        if(app->line_len < (sizeof(app->line) - 1U)) {
            app->line[app->line_len++] = data;
        }
    }
}

static void
    wifi_mapper_on_irq_cb(FuriHalSerialHandle* handle, FuriHalSerialRxEvent event, void* context) {
    UNUSED(handle);
    WiFiMapperApp* app = context;
    WiFiMapperEvent flags = 0;

    if(event & FuriHalSerialRxEventData) {
        uint8_t data = furi_hal_serial_async_rx(app->serial_handle);
        furi_stream_buffer_send(app->rx_stream, &data, 1, 0);
        flags |= WiFiMapperEventRxData;
    }

    if(event & FuriHalSerialRxEventIdle) {
        flags |= WiFiMapperEventRxIdle;
    }

    if(event & (FuriHalSerialRxEventFrameError | FuriHalSerialRxEventNoiseError |
                FuriHalSerialRxEventOverrunError | FuriHalSerialRxEventParityError)) {
        flags |= WiFiMapperEventRxError;
    }

    if(flags) {
        furi_thread_flags_set(furi_thread_get_id(app->worker_thread), flags);
    }
}

static int32_t wifi_mapper_worker(void* context) {
    WiFiMapperApp* app = context;

    while(true) {
        const uint32_t events = furi_thread_flags_wait(
            WIFI_MAPPER_WORKER_EVENTS, FuriFlagWaitAny, furi_ms_to_ticks(250U));
        if(events == FuriFlagErrorTimeout) {
            wifi_mapper_check_gps_timeout(app);
            continue;
        }
        furi_check((events & FuriFlagError) == 0);

        if(events & WiFiMapperEventStop) {
            break;
        }

        if(events & WiFiMapperEventRxData) {
            uint8_t data[64];
            size_t length = 0;
            do {
                length = furi_stream_buffer_receive(app->rx_stream, data, sizeof(data), 0);
                for(size_t i = 0; i < length; i++) {
                    wifi_mapper_process_byte(app, (char)data[i]);
                }
            } while(length > 0U);
        }

        if(events & WiFiMapperEventRxError) {
            furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
            app->errors++;
            strlcpy(app->status, "UART error", sizeof(app->status));
            furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
            notification_message(app->notification, &sequence_wifi_mapper_error);
            wifi_mapper_update_model(app);
        }

        if(events & WiFiMapperEventBridge) {
            BtAppBridgeEvent bridge_event;
            while(furi_message_queue_get(app->bridge_queue, &bridge_event, 0U) == FuriStatusOk) {
                wifi_mapper_handle_gps_response(app, &bridge_event);
            }
        }

        wifi_mapper_check_gps_timeout(app);
    }

    return 0;
}

static WiFiMapperApp* wifi_mapper_alloc(void) {
    WiFiMapperApp* app = malloc(sizeof(WiFiMapperApp));
    memset(app, 0, sizeof(WiFiMapperApp));
    app->scan_mode = WiFiMapperScanModeAll;
    strlcpy(app->status, "Scan All", sizeof(app->status));
    strlcpy(app->inspector_status, "Open a saved session", sizeof(app->inspector_status));
    app->locator_rssi = -100;
    app->locator_peak_rssi = -100;
    app->locator_previous_rssi = -100;
    app->locator_trend = WiFiMapperLocatorTrendWaiting;
    app->gps_state = WiFiMapperGpsIdle;
    app->gps_request_id_next = furi_get_tick();

    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->rx_stream = furi_stream_buffer_alloc(WIFI_MAPPER_RX_BUFFER_SIZE, 1);
    app->bridge_queue = furi_message_queue_alloc(8U, sizeof(BtAppBridgeEvent));
    app->storage = furi_record_open(RECORD_STORAGE);
    app->notification = furi_record_open(RECORD_NOTIFICATION);
    app->gui = furi_record_open(RECORD_GUI);
    app->bt = furi_record_open(RECORD_BT);
    app->expansion = furi_record_open(RECORD_EXPANSION);

    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    app->view = view_alloc();
    view_allocate_model(app->view, ViewModelTypeLocking, sizeof(WiFiMapperModel));
    view_set_draw_callback(app->view, wifi_mapper_draw_callback);
    view_set_input_callback(app->view, wifi_mapper_input_callback);
    view_set_previous_callback(app->view, wifi_mapper_exit);
    view_set_context(app->view, app);
    view_dispatcher_add_view(app->view_dispatcher, 0, app->view);
    view_dispatcher_switch_to_view(app->view_dispatcher, 0);

    app->worker_thread = furi_thread_alloc_ex("WiFiMapperRx", 3 * 1024, wifi_mapper_worker, app);
    furi_thread_start(app->worker_thread);
    app->bridge_pubsub = bt_app_bridge_get_pubsub(app->bt);
    app->bridge_sub = furi_pubsub_subscribe(app->bridge_pubsub, wifi_mapper_bridge_callback, app);

    expansion_disable(app->expansion);
    app->expansion_disabled = true;

    app->serial_handle = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
    if(app->serial_handle) {
        furi_hal_serial_init(app->serial_handle, WIFI_MAPPER_BAUDRATE);
        furi_hal_serial_configure_framing(
            app->serial_handle,
            FuriHalSerialDataBits8,
            FuriHalSerialParityNone,
            FuriHalSerialStopBits1);
        furi_hal_serial_async_rx_start(app->serial_handle, wifi_mapper_on_irq_cb, app, true);
        app->uart_ready = true;
    } else {
        expansion_enable(app->expansion);
        app->expansion_disabled = false;
        app->uart_ready = false;
        strlcpy(app->status, "UART busy", sizeof(app->status));
    }

    wifi_mapper_update_model(app);
    return app;
}

static void wifi_mapper_free(WiFiMapperApp* app) {
    furi_assert(app);

    wifi_mapper_locator_stop(app);
    wifi_mapper_stop_logging(app);

    if(app->bridge_sub) {
        furi_pubsub_unsubscribe(app->bridge_pubsub, app->bridge_sub);
        app->bridge_sub = NULL;
    }

    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    wifi_mapper_relay_flush_locked(app);
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);

    if(app->serial_handle) {
        furi_hal_serial_async_rx_stop(app->serial_handle);
        furi_hal_serial_deinit(app->serial_handle);
        furi_hal_serial_control_release(app->serial_handle);
        app->serial_handle = NULL;
    }

    if(app->expansion_disabled) {
        expansion_enable(app->expansion);
        app->expansion_disabled = false;
    }

    furi_thread_flags_set(furi_thread_get_id(app->worker_thread), WiFiMapperEventStop);
    furi_thread_join(app->worker_thread);
    furi_thread_free(app->worker_thread);

    view_dispatcher_remove_view(app->view_dispatcher, 0);
    view_free(app->view);
    view_dispatcher_free(app->view_dispatcher);

    furi_stream_buffer_free(app->rx_stream);
    furi_message_queue_free(app->bridge_queue);
    furi_mutex_free(app->mutex);

    free(app->inspector);

    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_BT);
    furi_record_close(RECORD_EXPANSION);

    free(app);
}

int32_t wifi_mapper_app(void* p) {
    UNUSED(p);

    WiFiMapperApp* app = wifi_mapper_alloc();
    view_dispatcher_run(app->view_dispatcher);
    wifi_mapper_free(app);
    return 0;
}
