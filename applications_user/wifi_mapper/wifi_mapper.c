#include <furi.h>
#include <furi_hal.h>

#include <gui/gui.h>
#include <gui/elements.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>

#include <notification/notification.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAG "WiFiMapper"

#define WIFI_MAPPER_BAUDRATE       115200
#define WIFI_MAPPER_RX_BUFFER_SIZE 2048U
#define WIFI_MAPPER_LINE_SIZE      160U
#define WIFI_MAPPER_PATH_SIZE      128U
#define WIFI_MAPPER_SESSION_LINE_SIZE 256U
#define WIFI_MAPPER_LAST_LINE_SIZE 48U
#define WIFI_MAPPER_BSSID_SIZE     18U
#define WIFI_MAPPER_SSID_SIZE      64U
#define WIFI_MAPPER_FILE_NAME_SIZE 32U
#define WIFI_MAPPER_MAX_UNIQUE     64U
#define WIFI_MAPPER_CHANNEL_BUCKETS 15U

#define WIFI_MAPPER_DATA_DIR     EXT_PATH("apps_data/wifi_mapper")
#define WIFI_MAPPER_SESSIONS_DIR EXT_PATH("apps_data/wifi_mapper/sessions")
#define WIFI_MAPPER_SCAN_ALL_COMMAND "scanall\r\n"
#define WIFI_MAPPER_SCAN_AP_COMMAND  "scanap\r\n"
#define WIFI_MAPPER_STOP_COMMAND "stopscan\r\n"

typedef enum {
    WiFiMapperEventReserved = (1 << 0),
    WiFiMapperEventStop = (1 << 1),
    WiFiMapperEventRxData = (1 << 2),
    WiFiMapperEventRxIdle = (1 << 3),
    WiFiMapperEventRxError = (1 << 4),
} WiFiMapperEvent;

#define WIFI_MAPPER_WORKER_EVENTS \
    (WiFiMapperEventStop | WiFiMapperEventRxData | WiFiMapperEventRxIdle | WiFiMapperEventRxError)

typedef enum {
    WiFiMapperScanModeAll,
    WiFiMapperScanModeAp,
    WiFiMapperScanModeCount,
} WiFiMapperScanMode;

typedef enum {
    WiFiMapperScreenLive,
    WiFiMapperScreenSession,
} WiFiMapperScreen;

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
} WiFiMapperRecord;

typedef struct {
    bool loaded;
    char status[24];
    char file_name[WIFI_MAPPER_FILE_NAME_SIZE];
    uint32_t rows;
    uint32_t aps;
    uint32_t unique;
    int32_t best_rssi;
    int32_t avg_rssi;
    uint8_t top_channel;
    uint32_t top_channel_count;
} WiFiMapperSessionStats;

static const WiFiMapperScanModeConfig wifi_mapper_scan_modes[WiFiMapperScanModeCount] = {
    [WiFiMapperScanModeAll] = {
        .label = "Scan All",
        .command = WIFI_MAPPER_SCAN_ALL_COMMAND,
        .sent_status = "scanall sent",
    },
    [WiFiMapperScanModeAp] = {
        .label = "Scan AP",
        .command = WIFI_MAPPER_SCAN_AP_COMMAND,
        .sent_status = "scanap sent",
    },
};

typedef struct {
    WiFiMapperScreen screen;
    bool logging;
    bool uart_ready;
    WiFiMapperScanMode scan_mode;
    WiFiMapperSessionStats session;
    uint32_t lines;
    uint32_t wifi_records;
    uint32_t errors;
    char status[24];
    char file_name[WIFI_MAPPER_FILE_NAME_SIZE];
    char last_line[WIFI_MAPPER_LAST_LINE_SIZE];
} WiFiMapperModel;

typedef struct {
    Gui* gui;
    Storage* storage;
    NotificationApp* notification;
    ViewDispatcher* view_dispatcher;
    View* view;
    FuriThread* worker_thread;
    FuriStreamBuffer* rx_stream;
    FuriMutex* mutex;
    FuriHalSerialHandle* serial_handle;
    File* log_file;
    char log_path[WIFI_MAPPER_PATH_SIZE];
    char line[WIFI_MAPPER_LINE_SIZE];
    size_t line_len;
    WiFiMapperScreen screen;
    bool logging;
    bool uart_ready;
    WiFiMapperScanMode scan_mode;
    WiFiMapperSessionStats session;
    uint32_t lines;
    uint32_t wifi_records;
    uint32_t errors;
    char status[24];
    char file_name[WIFI_MAPPER_FILE_NAME_SIZE];
    char last_line[WIFI_MAPPER_LAST_LINE_SIZE];
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

static void wifi_mapper_update_model(WiFiMapperApp* app) {
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    const WiFiMapperScreen screen = app->screen;
    const bool logging = app->logging;
    const bool uart_ready = app->uart_ready;
    const WiFiMapperScanMode scan_mode = app->scan_mode;
    const WiFiMapperSessionStats session = app->session;
    const uint32_t lines = app->lines;
    const uint32_t wifi_records = app->wifi_records;
    const uint32_t errors = app->errors;
    char status[sizeof(app->status)];
    char file_name[sizeof(app->file_name)];
    char last_line[sizeof(app->last_line)];
    strlcpy(status, app->status, sizeof(status));
    strlcpy(file_name, app->file_name, sizeof(file_name));
    strlcpy(last_line, app->last_line, sizeof(last_line));
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);

    with_view_model(
        app->view,
        WiFiMapperModel * model,
        {
            model->screen = screen;
            model->logging = logging;
            model->uart_ready = uart_ready;
            model->scan_mode = scan_mode;
            model->session = session;
            model->lines = lines;
            model->wifi_records = wifi_records;
            model->errors = errors;
            strlcpy(model->status, status, sizeof(model->status));
            strlcpy(model->file_name, file_name, sizeof(model->file_name));
            strlcpy(model->last_line, last_line, sizeof(model->last_line));
        },
        true);
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
    const bool return_to_live = app->screen == WiFiMapperScreenSession;
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

static const WiFiMapperScanModeConfig* wifi_mapper_get_scan_mode_config(
    WiFiMapperScanMode scan_mode) {
    if(scan_mode >= WiFiMapperScanModeCount) {
        scan_mode = WiFiMapperScanModeAll;
    }

    return &wifi_mapper_scan_modes[scan_mode];
}

static void wifi_mapper_set_status(WiFiMapperApp* app, const char* status) {
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    strlcpy(app->status, status, sizeof(app->status));
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
    snprintf(
        app->file_name,
        sizeof(app->file_name),
        "wifi_%02u%02u_%02u%02u%02u.csv",
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
    if(!storage_file_open(app->log_file, app->log_path, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        storage_file_free(app->log_file);
        app->log_file = NULL;
        return false;
    }

    const char* header = "tick_ms,type,rssi,channel,bssid,ssid,raw\n";
    storage_file_write(app->log_file, header, strlen(header));
    return true;
}

static void wifi_mapper_close_log(WiFiMapperApp* app) {
    if(app->log_file) {
        storage_file_close(app->log_file);
        storage_file_free(app->log_file);
        app->log_file = NULL;
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

static bool wifi_mapper_is_mac(const char* cursor) {
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

static bool wifi_mapper_parse_marauder_ap_line(
    const char* line,
    WiFiMapperRecord* record) {
    const char* cursor = line;
    int32_t rssi = 0;
    int32_t channel = 0;

    if(!wifi_mapper_parse_i32(&cursor, &rssi)) {
        return false;
    }

    cursor = wifi_mapper_skip_spaces(cursor);
    if(strncmp(cursor, "Ch:", 3) != 0) {
        return false;
    }
    cursor += 3;

    if(!wifi_mapper_parse_i32(&cursor, &channel) || (channel < 0) ||
       (channel > UINT8_MAX)) {
        return false;
    }

    cursor = wifi_mapper_skip_spaces(cursor);
    if(!wifi_mapper_is_mac(cursor)) {
        return false;
    }
    strlcpy(record->bssid, cursor, sizeof(record->bssid));
    cursor += 17;

    cursor = wifi_mapper_skip_spaces(cursor);
    if(strncmp(cursor, "ESSID:", 6) != 0) {
        return false;
    }
    cursor += 6;

    record->is_wifi = true;
    record->type = "ap";
    record->rssi = rssi;
    record->channel = (uint8_t)channel;
    strlcpy(record->ssid, wifi_mapper_skip_spaces(cursor), sizeof(record->ssid));
    return true;
}

static WiFiMapperRecord wifi_mapper_parse_record(const char* line) {
    WiFiMapperRecord record = {
        .is_wifi = false,
        .type = "raw",
    };

    if(strncmp(line, "WIFI,", 5) == 0) {
        record.is_wifi = true;
        record.type = "wifi";
        return record;
    }

    wifi_mapper_parse_marauder_ap_line(line, &record);
    return record;
}

static bool wifi_mapper_name_ends_with(const char* name, const char* suffix) {
    const size_t name_len = strlen(name);
    const size_t suffix_len = strlen(suffix);
    return (name_len >= suffix_len) &&
           (strcmp(name + name_len - suffix_len, suffix) == 0);
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

static bool wifi_mapper_csv_read_quoted(
    const char** cursor,
    char* value,
    size_t value_size) {
    size_t offset = 0;
    if(**cursor != '"') {
        return false;
    }
    (*cursor)++;

    while(**cursor) {
        if(**cursor == '"') {
            if((*cursor)[1] == '"') {
                if(offset < (value_size - 1U)) {
                    value[offset++] = '"';
                }
                *cursor += 2;
                continue;
            }

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

static bool wifi_mapper_parse_csv_ap_row(
    const char* line,
    int32_t* rssi,
    uint8_t* channel,
    char* bssid,
    size_t bssid_size) {
    const char* cursor = strchr(line, ',');
    if(!cursor) {
        return false;
    }
    cursor++;

    if(strncmp(cursor, "ap,", 3) != 0) {
        return false;
    }
    cursor += 3;

    if(!wifi_mapper_parse_i32(&cursor, rssi) || (*cursor != ',')) {
        return false;
    }
    cursor++;

    int32_t channel_value = 0;
    if(!wifi_mapper_parse_i32(&cursor, &channel_value) || (*cursor != ',') ||
       (channel_value < 0) || (channel_value > UINT8_MAX)) {
        return false;
    }
    cursor++;

    if(!wifi_mapper_csv_read_quoted(&cursor, bssid, bssid_size) ||
       !wifi_mapper_is_mac(bssid)) {
        return false;
    }

    *channel = (uint8_t)channel_value;
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
    char unique_bssids[WIFI_MAPPER_MAX_UNIQUE][WIFI_MAPPER_BSSID_SIZE],
    uint32_t channel_counts[WIFI_MAPPER_CHANNEL_BUCKETS],
    int32_t* rssi_sum,
    const char* line) {
    if(strncmp(line, "tick_ms,", 8) == 0) {
        return;
    }

    stats->rows++;

    int32_t rssi = 0;
    uint8_t channel = 0;
    char bssid[WIFI_MAPPER_BSSID_SIZE];
    if(!wifi_mapper_parse_csv_ap_row(line, &rssi, &channel, bssid, sizeof(bssid))) {
        return;
    }

    stats->aps++;
    *rssi_sum += rssi;
    if((stats->aps == 1U) || (rssi > stats->best_rssi)) {
        stats->best_rssi = rssi;
    }

    if(channel < WIFI_MAPPER_CHANNEL_BUCKETS) {
        channel_counts[channel]++;
    }

    if((stats->unique < WIFI_MAPPER_MAX_UNIQUE) &&
       !wifi_mapper_bssid_seen(unique_bssids, stats->unique, bssid)) {
        strlcpy(unique_bssids[stats->unique], bssid, WIFI_MAPPER_BSSID_SIZE);
        stats->unique++;
    }
}

static void wifi_mapper_store_session_stats(
    WiFiMapperApp* app,
    const WiFiMapperSessionStats* stats) {
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    app->session = *stats;
    app->screen = WiFiMapperScreenSession;
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
    wifi_mapper_update_model(app);
}

static void wifi_mapper_analyze_latest_session(WiFiMapperApp* app) {
    WiFiMapperSessionStats stats = {
        .loaded = false,
    };
    strlcpy(stats.status, "No sessions", sizeof(stats.status));

    char file_name[WIFI_MAPPER_FILE_NAME_SIZE] = "";
    if(!wifi_mapper_find_latest_session(app, file_name, sizeof(file_name))) {
        wifi_mapper_store_session_stats(app, &stats);
        return;
    }

    strlcpy(stats.file_name, file_name, sizeof(stats.file_name));
    char path[WIFI_MAPPER_PATH_SIZE];
    snprintf(path, sizeof(path), WIFI_MAPPER_SESSIONS_DIR "/%s", file_name);

    File* file = storage_file_alloc(app->storage);
    if(!storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        strlcpy(stats.status, "Read error", sizeof(stats.status));
        storage_file_free(file);
        wifi_mapper_store_session_stats(app, &stats);
        return;
    }

    char unique_bssids[WIFI_MAPPER_MAX_UNIQUE][WIFI_MAPPER_BSSID_SIZE] = {};
    uint32_t channel_counts[WIFI_MAPPER_CHANNEL_BUCKETS] = {};
    int32_t rssi_sum = 0;
    char line[WIFI_MAPPER_SESSION_LINE_SIZE];
    size_t line_len = 0;
    uint8_t buffer[64];
    size_t bytes_read = 0;
    while((bytes_read = storage_file_read(file, buffer, sizeof(buffer))) > 0U) {
        for(size_t i = 0; i < bytes_read; i++) {
            const char data = (char)buffer[i];
            if((data == '\r') || (data == '\n')) {
                if(line_len > 0U) {
                    line[line_len] = '\0';
                    wifi_mapper_parse_session_line(
                        &stats, unique_bssids, channel_counts, &rssi_sum, line);
                    line_len = 0U;
                }
            } else if(line_len < (sizeof(line) - 1U)) {
                line[line_len++] = data;
            }
        }
    }
    if(line_len > 0U) {
        line[line_len] = '\0';
        wifi_mapper_parse_session_line(&stats, unique_bssids, channel_counts, &rssi_sum, line);
    }

    storage_file_close(file);
    storage_file_free(file);

    if(stats.aps > 0U) {
        stats.loaded = true;
        stats.avg_rssi = rssi_sum / (int32_t)stats.aps;
        strlcpy(stats.status, "Last session", sizeof(stats.status));
        for(uint8_t channel = 1; channel < WIFI_MAPPER_CHANNEL_BUCKETS; channel++) {
            if(channel_counts[channel] > stats.top_channel_count) {
                stats.top_channel = channel;
                stats.top_channel_count = channel_counts[channel];
            }
        }
    } else {
        strlcpy(stats.status, "No AP rows", sizeof(stats.status));
    }

    wifi_mapper_store_session_stats(app, &stats);
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

static void wifi_mapper_write_log_record(
    File* file,
    uint32_t tick_ms,
    const WiFiMapperRecord* record,
    const char* line) {
    char prefix[48];
    if(record->is_wifi && (strcmp(record->type, "ap") == 0)) {
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
    } else {
        snprintf(
            prefix,
            sizeof(prefix),
            "%lu,%s,,,,,",
            (unsigned long)tick_ms,
            record->type);
        storage_file_write(file, prefix, strlen(prefix));
    }

    wifi_mapper_write_escaped_csv(file, line);
    storage_file_write(file, "\n", 1);
}

static void wifi_mapper_log_line(WiFiMapperApp* app, const char* line) {
    const WiFiMapperRecord record = wifi_mapper_parse_record(line);

    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    if(app->logging && app->log_file) {
        const uint32_t tick_ms = (furi_get_tick() * 1000U) /
                                 furi_kernel_get_tick_frequency();
        wifi_mapper_write_log_record(app->log_file, tick_ms, &record, line);
    }

    app->lines++;
    if(record.is_wifi) {
        app->wifi_records++;
    }
    strlcpy(app->last_line, line, sizeof(app->last_line));
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
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
        app->logging = true;
        strlcpy(app->status, "Logging", sizeof(app->status));
    } else {
        app->logging = false;
        strlcpy(app->status, "Log error", sizeof(app->status));
    }
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);

    if(opened) {
        wifi_mapper_send_active_scan_command(app);
        notification_message(app->notification, &sequence_wifi_mapper_rx);
    } else {
        notification_message(app->notification, &sequence_wifi_mapper_error);
    }
    wifi_mapper_update_model(app);
}

static void wifi_mapper_stop_logging(WiFiMapperApp* app) {
    if(app->serial_handle) {
        wifi_mapper_send_command(app, WIFI_MAPPER_STOP_COMMAND);
    }

    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    wifi_mapper_close_log(app);
    app->logging = false;
    strlcpy(
        app->status,
        app->uart_ready ? "Idle" : "UART not ready",
        sizeof(app->status));
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
    wifi_mapper_update_model(app);
}

static void wifi_mapper_draw_live(Canvas* canvas, WiFiMapperModel* model) {
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 10, "WiFi Mapper");

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 0, 22, model->status);
    canvas_draw_str(canvas, 78, 22, model->uart_ready ? "UART OK" : "UART --");

    char line[48];
    snprintf(
        line,
        sizeof(line),
        "< %s >",
        wifi_mapper_get_scan_mode_config(model->scan_mode)->label);
    canvas_draw_str(canvas, 0, 34, line);

    snprintf(
        line,
        sizeof(line),
        "Lines:%lu WiFi:%lu",
        (unsigned long)model->lines,
        (unsigned long)model->wifi_records);
    canvas_draw_str(canvas, 0, 46, line);
    snprintf(line, sizeof(line), "Err:%lu", (unsigned long)model->errors);
    canvas_draw_str(canvas, 86, 46, line);

    canvas_draw_str(
        canvas,
        0,
        58,
        model->file_name[0] ? model->file_name : "OK start Hold stats");

    elements_button_center(canvas, model->logging ? "Stop" : "Start");
}

static void wifi_mapper_draw_session(Canvas* canvas, WiFiMapperModel* model) {
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 10, "Last Session");

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 0, 22, model->session.status);
    if(model->session.file_name[0]) {
        canvas_draw_str(canvas, 0, 34, model->session.file_name);
    }

    char line[48];
    if(model->session.loaded) {
        snprintf(
            line,
            sizeof(line),
            "AP:%lu Unique:%lu",
            (unsigned long)model->session.aps,
            (unsigned long)model->session.unique);
        canvas_draw_str(canvas, 0, 46, line);

        snprintf(
            line,
            sizeof(line),
            "RSSI best:%ld avg:%ld",
            (long)model->session.best_rssi,
            (long)model->session.avg_rssi);
        canvas_draw_str(canvas, 0, 58, line);

        if(model->session.top_channel_count > 0U) {
            snprintf(
                line,
                sizeof(line),
                "Ch%u:%lu",
                model->session.top_channel,
                (unsigned long)model->session.top_channel_count);
            canvas_draw_str(canvas, 90, 34, line);
        }
    } else {
        canvas_draw_str(canvas, 0, 46, "OK refresh");
        canvas_draw_str(canvas, 0, 58, "Back live");
    }
}

static void wifi_mapper_draw_callback(Canvas* canvas, void* context) {
    WiFiMapperModel* model = context;
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    if(model->screen == WiFiMapperScreenSession) {
        wifi_mapper_draw_session(canvas, model);
    } else {
        wifi_mapper_draw_live(canvas, model);
    }
}

static bool wifi_mapper_input_callback(InputEvent* event, void* context) {
    WiFiMapperApp* app = context;
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    const WiFiMapperScreen screen = app->screen;
    const bool logging = app->logging;
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);

    if(screen == WiFiMapperScreenSession) {
        if((event->type == InputTypeShort) && (event->key == InputKeyOk)) {
            wifi_mapper_analyze_latest_session(app);
            return true;
        } else if((event->type == InputTypeShort) && (event->key == InputKeyBack)) {
            wifi_mapper_switch_screen(app, WiFiMapperScreenLive);
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
    } else if(event->key == InputKeyUp) {
        wifi_mapper_send_active_scan_command(app);
        return true;
    } else if(event->key == InputKeyDown) {
        wifi_mapper_send_command(app, WIFI_MAPPER_STOP_COMMAND);
        wifi_mapper_set_status(app, "stopscan sent");
        return true;
    } else if(event->key == InputKeyRight) {
        wifi_mapper_next_scan_mode(app);
        return true;
    } else if(event->key == InputKeyLeft) {
        wifi_mapper_previous_scan_mode(app);
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

static void wifi_mapper_on_irq_cb(
    FuriHalSerialHandle* handle,
    FuriHalSerialRxEvent event,
    void* context) {
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
        const uint32_t events =
            furi_thread_flags_wait(WIFI_MAPPER_WORKER_EVENTS, FuriFlagWaitAny, FuriWaitForever);
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
    }

    return 0;
}

static WiFiMapperApp* wifi_mapper_alloc(void) {
    WiFiMapperApp* app = malloc(sizeof(WiFiMapperApp));
    memset(app, 0, sizeof(WiFiMapperApp));
    app->scan_mode = WiFiMapperScanModeAll;
    strlcpy(app->status, "Scan All", sizeof(app->status));

    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->rx_stream = furi_stream_buffer_alloc(WIFI_MAPPER_RX_BUFFER_SIZE, 1);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->notification = furi_record_open(RECORD_NOTIFICATION);
    app->gui = furi_record_open(RECORD_GUI);

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

    app->worker_thread = furi_thread_alloc_ex("WiFiMapperRx", 1536, wifi_mapper_worker, app);
    furi_thread_start(app->worker_thread);

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
        app->uart_ready = false;
        strlcpy(app->status, "UART busy", sizeof(app->status));
    }

    wifi_mapper_update_model(app);
    return app;
}

static void wifi_mapper_free(WiFiMapperApp* app) {
    furi_assert(app);

    wifi_mapper_stop_logging(app);

    if(app->serial_handle) {
        furi_hal_serial_async_rx_stop(app->serial_handle);
        furi_hal_serial_deinit(app->serial_handle);
        furi_hal_serial_control_release(app->serial_handle);
    }

    furi_thread_flags_set(furi_thread_get_id(app->worker_thread), WiFiMapperEventStop);
    furi_thread_join(app->worker_thread);
    furi_thread_free(app->worker_thread);

    view_dispatcher_remove_view(app->view_dispatcher, 0);
    view_free(app->view);
    view_dispatcher_free(app->view_dispatcher);

    furi_stream_buffer_free(app->rx_stream);
    furi_mutex_free(app->mutex);

    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_STORAGE);

    free(app);
}

int32_t wifi_mapper_app(void* p) {
    UNUSED(p);

    WiFiMapperApp* app = wifi_mapper_alloc();
    view_dispatcher_run(app->view_dispatcher);
    wifi_mapper_free(app);
    return 0;
}
