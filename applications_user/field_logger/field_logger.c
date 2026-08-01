#include <furi.h>
#include <furi_hal_power.h>
#include <furi_hal_rtc.h>
#include <gui/gui.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_box.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>
#include <tumoflip_device_services/tumoflip_device_services.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FIELD_LOGGER_DATA_DIR     EXT_PATH("apps_data/field_logger")
#define FIELD_LOGGER_SESSIONS_DIR EXT_PATH("apps_data/field_logger/sessions")
#define FIELD_LOGGER_RF_NOTEBOOK_CSV \
    EXT_PATH("apps_data/arf_subghz_full/notebook/observations.csv")
#define FIELD_LOGGER_SENSOR_SESSIONS_DIR EXT_PATH("apps_data/module_one_sensor_logger/sessions")

#define FIELD_LOGGER_PATH_SIZE         128U
#define FIELD_LOGGER_STATUS_SIZE       48U
#define FIELD_LOGGER_SESSION_NAME_SIZE 48U
#define FIELD_LOGGER_TICK_MS           250U
#define FIELD_LOGGER_RF_LINE_SIZE      192U
#define FIELD_LOGGER_EVENT_PHONE_GPS   1U

typedef enum {
    FieldLoggerViewMenu,
    FieldLoggerViewRun,
    FieldLoggerViewText,
} FieldLoggerView;

typedef enum {
    FieldLoggerMenuStart,
    FieldLoggerMenuInterval,
    FieldLoggerMenuImportRf,
    FieldLoggerMenuStatus,
    FieldLoggerMenuAbout,
} FieldLoggerMenu;

typedef struct {
    bool valid;
    char source[16];
    char frequency_hz[16];
    char rssi_dbm[16];
    char trigger_dbm[16];
    char radio[16];
    char rx_count[12];
    char note[32];
} FieldLoggerRfObservation;

typedef struct {
    bool logging;
    uint32_t interval_ms;
    uint32_t next_sample_tick;
    uint32_t record_count;
    uint32_t rf_count;
    uint32_t write_errors;
    uint8_t battery_pct;
    char status[FIELD_LOGGER_STATUS_SIZE];
    char session_name[FIELD_LOGGER_SESSION_NAME_SIZE];
    char csv_path[FIELD_LOGGER_PATH_SIZE];
    char jsonl_path[FIELD_LOGGER_PATH_SIZE];
    char gpx_path[FIELD_LOGGER_PATH_SIZE];
    char last_rf[64];
} FieldLoggerModel;

typedef struct {
    Gui* gui;
    Storage* storage;
    NotificationApp* notification;
    ViewDispatcher* view_dispatcher;
    Submenu* submenu;
    TextBox* text_box;
    View* run_view;
    FuriString* text;
    FuriString* line;
    File* csv_file;
    File* jsonl_file;
    File* gpx_file;
    TumoflipDeviceServicesClient* device_services;
    FuriMutex* device_services_mutex;
    TumoflipDeviceLocation phone_location;
    bool phone_location_valid;
    uint32_t interval_index;
} FieldLoggerApp;

static const uint32_t field_logger_intervals_ms[] = {
    1000U,
    5000U,
    10000U,
    30000U,
    60000U,
};

static const NotificationSequence field_logger_sequence_ok = {
    &message_display_backlight_on,
    &message_green_255,
    &message_delay_10,
    NULL,
};

static const NotificationSequence field_logger_sequence_error = {
    &message_display_backlight_on,
    &message_red_255,
    &message_delay_10,
    NULL,
};

static uint32_t field_logger_tick_ms(void) {
    return (furi_get_tick() * 1000U) / furi_kernel_get_tick_frequency();
}

static bool field_logger_mkdir(Storage* storage, const char* path) {
    const FS_Error error = storage_common_mkdir(storage, path);
    return (error == FSE_OK) || (error == FSE_EXIST);
}

static bool field_logger_path_exists(Storage* storage, const char* path) {
    return storage_common_stat(storage, path, NULL) == FSE_OK;
}

static bool field_logger_write_all(File* file, const char* text) {
    const size_t size = strlen(text);
    return storage_file_write(file, text, size) == size;
}

static void field_logger_format_filename_timestamp(char* output, size_t output_size) {
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

static void field_logger_append_iso_timestamp(FuriString* output) {
    DateTime now;
    furi_hal_rtc_get_datetime(&now);
    furi_string_cat_printf(
        output,
        "%04u-%02u-%02uT%02u:%02u:%02u",
        now.year,
        now.month,
        now.day,
        now.hour,
        now.minute,
        now.second);
}

static void field_logger_csv_escape(FuriString* output, const char* value) {
    furi_string_push_back(output, '"');
    for(const char* cursor = value; cursor && *cursor; cursor++) {
        if(*cursor == '"') {
            furi_string_push_back(output, '"');
        }
        furi_string_push_back(output, *cursor);
    }
    furi_string_push_back(output, '"');
}

static void field_logger_json_escape(FuriString* output, const char* value) {
    for(const char* cursor = value; cursor && *cursor; cursor++) {
        switch(*cursor) {
        case '\\':
            furi_string_cat(output, "\\\\");
            break;
        case '"':
            furi_string_cat(output, "\\\"");
            break;
        case '\n':
            furi_string_cat(output, "\\n");
            break;
        case '\r':
            break;
        default:
            furi_string_push_back(output, *cursor);
            break;
        }
    }
}

static void field_logger_make_paths(FieldLoggerApp* app) {
    char timestamp[32];
    field_logger_format_filename_timestamp(timestamp, sizeof(timestamp));

    with_view_model(
        app->run_view,
        FieldLoggerModel * model,
        {
            snprintf(model->session_name, sizeof(model->session_name), "field_%s", timestamp);
            snprintf(
                model->csv_path,
                sizeof(model->csv_path),
                FIELD_LOGGER_SESSIONS_DIR "/%s.csv",
                model->session_name);
            snprintf(
                model->jsonl_path,
                sizeof(model->jsonl_path),
                FIELD_LOGGER_SESSIONS_DIR "/%s.jsonl",
                model->session_name);
            snprintf(
                model->gpx_path,
                sizeof(model->gpx_path),
                FIELD_LOGGER_SESSIONS_DIR "/%s.gpx",
                model->session_name);
        },
        false);
}

static void field_logger_sync_files(FieldLoggerApp* app) {
    if(app->csv_file && storage_file_is_open(app->csv_file)) storage_file_sync(app->csv_file);
    if(app->jsonl_file && storage_file_is_open(app->jsonl_file))
        storage_file_sync(app->jsonl_file);
    if(app->gpx_file && storage_file_is_open(app->gpx_file)) storage_file_sync(app->gpx_file);
}

static void field_logger_csv_read_field(const char** cursor, char* output, size_t output_size) {
    size_t index = 0;
    while(**cursor && (**cursor != ',') && (**cursor != '\n') && (**cursor != '\r')) {
        if(index + 1U < output_size) {
            output[index++] = **cursor;
        }
        (*cursor)++;
    }
    output[index] = '\0';
    if(**cursor == ',') {
        (*cursor)++;
    }
}

static bool field_logger_parse_rf_line(const char* line, FieldLoggerRfObservation* observation) {
    memset(observation, 0, sizeof(FieldLoggerRfObservation));
    if(!line || !line[0] || strncmp(line, "timestamp,", 10) == 0) {
        return false;
    }

    char ignored[32];
    const char* cursor = line;
    field_logger_csv_read_field(&cursor, ignored, sizeof(ignored));
    field_logger_csv_read_field(&cursor, observation->source, sizeof(observation->source));
    field_logger_csv_read_field(
        &cursor, observation->frequency_hz, sizeof(observation->frequency_hz));
    field_logger_csv_read_field(&cursor, observation->rssi_dbm, sizeof(observation->rssi_dbm));
    field_logger_csv_read_field(
        &cursor, observation->trigger_dbm, sizeof(observation->trigger_dbm));
    field_logger_csv_read_field(&cursor, ignored, sizeof(ignored));
    field_logger_csv_read_field(&cursor, observation->radio, sizeof(observation->radio));
    field_logger_csv_read_field(&cursor, observation->rx_count, sizeof(observation->rx_count));
    field_logger_csv_read_field(&cursor, ignored, sizeof(ignored));
    field_logger_csv_read_field(&cursor, ignored, sizeof(ignored));
    field_logger_csv_read_field(&cursor, ignored, sizeof(ignored));
    field_logger_csv_read_field(&cursor, observation->note, sizeof(observation->note));

    observation->valid = observation->frequency_hz[0] != '\0';
    return observation->valid;
}

static bool field_logger_read_latest_rf_observation(
    FieldLoggerApp* app,
    FieldLoggerRfObservation* observation) {
    File* file = storage_file_alloc(app->storage);
    bool found = false;
    char current[FIELD_LOGGER_RF_LINE_SIZE];
    char latest[FIELD_LOGGER_RF_LINE_SIZE];
    size_t current_len = 0;
    current[0] = '\0';
    latest[0] = '\0';

    if(storage_file_open(file, FIELD_LOGGER_RF_NOTEBOOK_CSV, FSAM_READ, FSOM_OPEN_EXISTING)) {
        char ch;
        while(storage_file_read(file, &ch, 1) == 1) {
            if((ch == '\n') || (ch == '\r')) {
                current[current_len] = '\0';
                if(current_len > 0U && strncmp(current, "timestamp,", 10) != 0) {
                    strlcpy(latest, current, sizeof(latest));
                    found = true;
                }
                current_len = 0;
                current[0] = '\0';
            } else if(current_len + 1U < sizeof(current)) {
                current[current_len++] = ch;
            }
        }
        if(current_len > 0U) {
            current[current_len] = '\0';
            if(strncmp(current, "timestamp,", 10) != 0) {
                strlcpy(latest, current, sizeof(latest));
                found = true;
            }
        }
    }

    storage_file_close(file);
    storage_file_free(file);
    return found && field_logger_parse_rf_line(latest, observation);
}

static void field_logger_write_record(
    FieldLoggerApp* app,
    const char* kind,
    const char* source,
    const FieldLoggerRfObservation* rf,
    const char* note) {
    FuriString* timestamp = app->text;
    FuriString* line = app->line;
    furi_string_reset(timestamp);
    field_logger_append_iso_timestamp(timestamp);

    uint32_t record = 0;
    with_view_model(
        app->run_view,
        FieldLoggerModel * model,
        {
            model->record_count++;
            model->battery_pct = furi_hal_power_get_pct();
            record = model->record_count;
        },
        false);

    const uint32_t uptime_ms = field_logger_tick_ms();
    const uint8_t battery_pct = furi_hal_power_get_pct();
    const float battery_v = furi_hal_power_get_battery_voltage(FuriHalPowerICFuelGauge);
    const float usb_v = furi_hal_power_get_usb_voltage();
    const bool charging = furi_hal_power_is_charging() || furi_hal_power_is_charging_done();
    const char* rf_frequency = (rf && rf->valid) ? rf->frequency_hz : "";
    const char* rf_rssi = (rf && rf->valid) ? rf->rssi_dbm : "";
    const char* rf_trigger = (rf && rf->valid) ? rf->trigger_dbm : "";
    const char* rf_radio = (rf && rf->valid) ? rf->radio : "";
    const char* rf_rx_count = (rf && rf->valid) ? rf->rx_count : "";
    TumoflipDeviceLocation phone_location;
    furi_check(furi_mutex_acquire(app->device_services_mutex, FuriWaitForever) == FuriStatusOk);
    const bool phone_location_valid = app->phone_location_valid;
    phone_location = app->phone_location;
    furi_check(furi_mutex_release(app->device_services_mutex) == FuriStatusOk);
    const char* latitude = phone_location_valid ? phone_location.latitude : "";
    const char* longitude = phone_location_valid ? phone_location.longitude : "";
    const char* altitude = phone_location_valid ? phone_location.altitude : "";
    const char* accuracy = phone_location_valid ? phone_location.accuracy : "";
    char gps_timestamp[16] = "";
    if(phone_location_valid) {
        snprintf(
            gps_timestamp, sizeof(gps_timestamp), "%lu", (unsigned long)phone_location.unix_time);
    }

    furi_string_printf(
        line,
        "1,%s,%lu,%lu,%s,%s,%u,%.2f,%u,%.2f,%s,",
        furi_string_get_cstr(timestamp),
        (unsigned long)uptime_ms,
        (unsigned long)record,
        kind,
        source,
        battery_pct,
        (double)battery_v,
        charging ? 1U : 0U,
        (double)usb_v,
        phone_location_valid ? "true" : "false");
    furi_string_cat_printf(
        line, "%s,%s,%s,%s,%s,,,,", latitude, longitude, altitude, accuracy, gps_timestamp);
    furi_string_cat_printf(
        line, "%s,%s,%s,%s,%s,", rf_frequency, rf_rssi, rf_trigger, rf_radio, rf_rx_count);
    field_logger_csv_escape(line, note ? note : "");
    furi_string_push_back(line, '\n');
    bool ok = field_logger_write_all(app->csv_file, furi_string_get_cstr(line));

    furi_string_printf(
        line,
        "{\"schema\":1,\"timestamp\":\"%s\",\"uptime_ms\":%lu,\"record\":%lu,"
        "\"kind\":\"%s\",\"source\":\"%s\",\"battery_pct\":%u,\"battery_voltage_v\":%.2f,"
        "\"charging\":%s,\"usb_voltage_v\":%.2f,\"gps_fix\":%s,",
        furi_string_get_cstr(timestamp),
        (unsigned long)uptime_ms,
        (unsigned long)record,
        kind,
        source,
        battery_pct,
        (double)battery_v,
        charging ? "true" : "false",
        (double)usb_v,
        phone_location_valid ? "true" : "false");
    if(phone_location_valid) {
        furi_string_cat_printf(
            line,
            "\"latitude\":%s,\"longitude\":%s,\"altitude_m\":%s,"
            "\"gps_accuracy_m\":%s,\"gps_timestamp\":%lu,",
            latitude,
            longitude,
            altitude,
            phone_location.accuracy,
            (unsigned long)phone_location.unix_time);
    } else {
        furi_string_cat(
            line,
            "\"latitude\":null,\"longitude\":null,\"altitude_m\":null,"
            "\"gps_accuracy_m\":null,\"gps_timestamp\":null,");
    }
    furi_string_cat(
        line, "\"temperature_c\":null,\"pressure_hpa\":null,\"humidity_percent\":null,");
    if(rf && rf->valid) {
        furi_string_cat_printf(
            line,
            "\"rf\":{\"frequency_hz\":%s,\"rssi_dbm\":%s,\"trigger_dbm\":%s,"
            "\"radio\":\"%s\",\"rx_count\":%s},",
            rf_frequency,
            rf_rssi[0] ? rf_rssi : "null",
            rf_trigger[0] ? rf_trigger : "null",
            rf_radio,
            rf_rx_count[0] ? rf_rx_count : "0");
    } else {
        furi_string_cat(line, "\"rf\":null,");
    }
    furi_string_cat(line, "\"note\":\"");
    field_logger_json_escape(line, note ? note : "");
    furi_string_cat(line, "\"}\n");
    ok = ok && field_logger_write_all(app->jsonl_file, furi_string_get_cstr(line));
    if(phone_location_valid && app->gpx_file && storage_file_is_open(app->gpx_file)) {
        furi_string_printf(
            line,
            "<trkpt lat=\"%s\" lon=\"%s\"><ele>%s</ele><name>%s %lu</name></trkpt>\n",
            latitude,
            longitude,
            altitude,
            kind,
            (unsigned long)record);
        ok = ok && field_logger_write_all(app->gpx_file, furi_string_get_cstr(line));
    }
    field_logger_sync_files(app);

    with_view_model(
        app->run_view,
        FieldLoggerModel * model,
        {
            if(!ok) {
                model->write_errors++;
                strlcpy(model->status, "Write error", sizeof(model->status));
            } else if(rf && rf->valid) {
                model->rf_count++;
                snprintf(
                    model->last_rf,
                    sizeof(model->last_rf),
                    "%sHz %sdBm",
                    rf->frequency_hz,
                    rf->rssi_dbm);
                strlcpy(model->status, "RF imported", sizeof(model->status));
            } else {
                strlcpy(model->status, "Sample saved", sizeof(model->status));
            }
        },
        true);
}

static bool field_logger_open_files(FieldLoggerApp* app) {
    field_logger_mkdir(app->storage, FIELD_LOGGER_DATA_DIR);
    field_logger_mkdir(app->storage, FIELD_LOGGER_SESSIONS_DIR);
    field_logger_make_paths(app);

    FieldLoggerModel snapshot;
    with_view_model(app->run_view, FieldLoggerModel * model, { snapshot = *model; }, false);

    app->csv_file = storage_file_alloc(app->storage);
    app->jsonl_file = storage_file_alloc(app->storage);
    app->gpx_file = storage_file_alloc(app->storage);

    if(!storage_file_open(app->csv_file, snapshot.csv_path, FSAM_WRITE, FSOM_CREATE_ALWAYS) ||
       !storage_file_open(app->jsonl_file, snapshot.jsonl_path, FSAM_WRITE, FSOM_CREATE_ALWAYS) ||
       !storage_file_open(app->gpx_file, snapshot.gpx_path, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        return false;
    }

    const char* csv_header =
        "schema,timestamp,uptime_ms,record,kind,source,battery_pct,battery_voltage_v,"
        "charging,usb_voltage_v,gps_fix,latitude,longitude,altitude_m,gps_accuracy_m,"
        "gps_timestamp,temperature_c,pressure_hpa,humidity_percent,rf_frequency_hz,"
        "rf_rssi_dbm,rf_trigger_dbm,rf_radio,rf_rx_count,note\n";
    field_logger_write_all(app->csv_file, csv_header);

    furi_string_reset(app->line);
    furi_string_cat_printf(
        app->line,
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<gpx version=\"1.1\" creator=\"Tumoflip Field Logger\" "
        "xmlns=\"http://www.topografix.com/GPX/1/1\">\n"
        "<trk><name>%s</name><trkseg>\n",
        snapshot.session_name);
    field_logger_write_all(app->gpx_file, furi_string_get_cstr(app->line));
    return true;
}

static void field_logger_close_files(FieldLoggerApp* app) {
    if(app->gpx_file) {
        if(storage_file_is_open(app->gpx_file)) {
            field_logger_write_all(app->gpx_file, "</trkseg></trk>\n</gpx>\n");
            storage_file_sync(app->gpx_file);
            storage_file_close(app->gpx_file);
        }
        storage_file_free(app->gpx_file);
        app->gpx_file = NULL;
    }
    if(app->jsonl_file) {
        if(storage_file_is_open(app->jsonl_file)) {
            storage_file_sync(app->jsonl_file);
            storage_file_close(app->jsonl_file);
        }
        storage_file_free(app->jsonl_file);
        app->jsonl_file = NULL;
    }
    if(app->csv_file) {
        if(storage_file_is_open(app->csv_file)) {
            storage_file_sync(app->csv_file);
            storage_file_close(app->csv_file);
        }
        storage_file_free(app->csv_file);
        app->csv_file = NULL;
    }
}

static void field_logger_take_sample(FieldLoggerApp* app, const char* note) {
    if(!app->csv_file || !storage_file_is_open(app->csv_file)) {
        with_view_model(
            app->run_view,
            FieldLoggerModel * model,
            { strlcpy(model->status, "Start first", sizeof(model->status)); },
            true);
        return;
    }

    field_logger_write_record(app, "sample", "field_logger", NULL, note);
}

static void field_logger_import_rf(FieldLoggerApp* app) {
    if(!app->csv_file || !storage_file_is_open(app->csv_file)) {
        with_view_model(
            app->run_view,
            FieldLoggerModel * model,
            { strlcpy(model->status, "Start first", sizeof(model->status)); },
            true);
        return;
    }

    FieldLoggerRfObservation observation;
    if(field_logger_read_latest_rf_observation(app, &observation)) {
        field_logger_write_record(
            app, "rf_observation", "arf_frequency_analyzer", &observation, observation.note);
        notification_message(app->notification, &field_logger_sequence_ok);
    } else {
        field_logger_write_record(
            app,
            "rf_missing",
            "arf_frequency_analyzer",
            NULL,
            "No ARF Frequency Analyzer notebook observation");
        notification_message(app->notification, &field_logger_sequence_error);
    }
}

static bool field_logger_start_session(FieldLoggerApp* app) {
    tumoflip_device_services_client_cancel(app->device_services);
    furi_check(furi_mutex_acquire(app->device_services_mutex, FuriWaitForever) == FuriStatusOk);
    app->phone_location_valid = false;
    memset(&app->phone_location, 0, sizeof(app->phone_location));
    furi_check(furi_mutex_release(app->device_services_mutex) == FuriStatusOk);

    with_view_model(
        app->run_view,
        FieldLoggerModel * model,
        {
            memset(model, 0, sizeof(FieldLoggerModel));
            model->interval_ms = field_logger_intervals_ms[app->interval_index];
            model->next_sample_tick = field_logger_tick_ms() + model->interval_ms;
            model->battery_pct = furi_hal_power_get_pct();
            strlcpy(model->status, "Opening files", sizeof(model->status));
        },
        true);

    if(!field_logger_open_files(app)) {
        field_logger_close_files(app);
        with_view_model(
            app->run_view,
            FieldLoggerModel * model,
            { strlcpy(model->status, "File open failed", sizeof(model->status)); },
            true);
        notification_message(app->notification, &field_logger_sequence_error);
        return false;
    }

    with_view_model(
        app->run_view,
        FieldLoggerModel * model,
        {
            model->logging = true;
            strlcpy(model->status, "Logging", sizeof(model->status));
        },
        true);
    field_logger_write_record(app, "session_start", "field_logger", NULL, "Session started");
    with_view_model(
        app->run_view,
        FieldLoggerModel * model,
        { strlcpy(model->status, "Getting phone GPS", sizeof(model->status)); },
        true);
    if(!tumoflip_device_services_client_request_location(app->device_services, "journal")) {
        with_view_model(
            app->run_view,
            FieldLoggerModel * model,
            { strlcpy(model->status, "Phone GPS unavailable", sizeof(model->status)); },
            true);
    }
    notification_message(app->notification, &field_logger_sequence_ok);
    return true;
}

static void field_logger_stop_session(FieldLoggerApp* app) {
    tumoflip_device_services_client_cancel(app->device_services);
    bool was_logging = false;
    with_view_model(
        app->run_view,
        FieldLoggerModel * model,
        {
            was_logging = model->logging;
            model->logging = false;
        },
        false);

    if(was_logging && app->csv_file && storage_file_is_open(app->csv_file)) {
        field_logger_write_record(app, "session_stop", "field_logger", NULL, "Session stopped");
    }
    field_logger_close_files(app);

    if(was_logging) {
        with_view_model(
            app->run_view,
            FieldLoggerModel * model,
            {
                snprintf(
                    model->status,
                    sizeof(model->status),
                    "Stopped %lu",
                    (unsigned long)model->record_count);
            },
            true);
        notification_message(app->notification, &field_logger_sequence_ok);
    }
}

static void field_logger_device_services_callback(
    const TumoflipDeviceServicesResult* result,
    void* context) {
    FieldLoggerApp* app = context;
    if(result->request != TumoflipDeviceServicesRequestLocation ||
       result->code == TumoflipDeviceServicesResultCancelled) {
        return;
    }

    furi_check(furi_mutex_acquire(app->device_services_mutex, FuriWaitForever) == FuriStatusOk);
    app->phone_location_valid = result->code == TumoflipDeviceServicesResultOk;
    if(app->phone_location_valid) app->phone_location = result->value.location;
    furi_check(furi_mutex_release(app->device_services_mutex) == FuriStatusOk);
    view_dispatcher_send_custom_event(app->view_dispatcher, FIELD_LOGGER_EVENT_PHONE_GPS);
}

static bool field_logger_custom_event_callback(void* context, uint32_t event) {
    FieldLoggerApp* app = context;
    if(event != FIELD_LOGGER_EVENT_PHONE_GPS) return false;

    furi_check(furi_mutex_acquire(app->device_services_mutex, FuriWaitForever) == FuriStatusOk);
    const bool location_valid = app->phone_location_valid;
    furi_check(furi_mutex_release(app->device_services_mutex) == FuriStatusOk);
    bool logging = false;
    with_view_model(
        app->run_view,
        FieldLoggerModel * model,
        {
            logging = model->logging;
            if(logging) {
                strlcpy(
                    model->status,
                    location_valid ? "Phone GPS ready" : "Phone GPS unavailable",
                    sizeof(model->status));
            }
        },
        true);
    if(logging && location_valid) {
        field_logger_write_record(app, "gps_fix", "iphone", NULL, "Session phone GPS fix");
    }
    return true;
}

static void field_logger_draw_callback(Canvas* canvas, void* context) {
    FieldLoggerModel* model = context;
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 10, "Field Logger");

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 0, 21, model->status);
    canvas_draw_str(canvas, 0, 31, model->session_name[0] ? model->session_name : "No session");

    char line[64];
    snprintf(
        line,
        sizeof(line),
        "Int %lus Rec %lu RF %lu Err %lu",
        (unsigned long)(model->interval_ms / 1000U),
        (unsigned long)model->record_count,
        (unsigned long)model->rf_count,
        (unsigned long)model->write_errors);
    canvas_draw_str(canvas, 0, 41, line);

    snprintf(
        line,
        sizeof(line),
        "Bat %u%% %.40s",
        model->battery_pct,
        model->last_rf[0] ? model->last_rf : "RF optional");
    canvas_draw_str(canvas, 0, 51, line);
    canvas_draw_str(canvas, 0, 61, "OK Sample  > RF");
}

static bool field_logger_input_callback(InputEvent* event, void* context) {
    FieldLoggerApp* app = context;
    if(event->type != InputTypeShort) return false;

    switch(event->key) {
    case InputKeyOk:
        field_logger_take_sample(app, "Manual sample");
        return true;
    case InputKeyRight:
        field_logger_import_rf(app);
        return true;
    case InputKeyBack:
        field_logger_stop_session(app);
        view_dispatcher_switch_to_view(app->view_dispatcher, FieldLoggerViewMenu);
        return true;
    default:
        break;
    }

    return false;
}

static uint32_t field_logger_run_previous_callback(void* context) {
    field_logger_stop_session(context);
    return FieldLoggerViewMenu;
}

static uint32_t field_logger_text_previous_callback(void* context) {
    UNUSED(context);
    return FieldLoggerViewMenu;
}

static void field_logger_tick_callback(void* context) {
    FieldLoggerApp* app = context;
    bool should_sample = false;
    with_view_model(
        app->run_view,
        FieldLoggerModel * model,
        {
            model->battery_pct = furi_hal_power_get_pct();
            if(model->logging) {
                const uint32_t now = field_logger_tick_ms();
                if((int32_t)(now - model->next_sample_tick) >= 0) {
                    should_sample = true;
                    model->next_sample_tick = now + model->interval_ms;
                }
            }
        },
        false);

    if(should_sample) {
        field_logger_take_sample(app, "Interval sample");
    }
}

static void field_logger_update_menu_labels(FieldLoggerApp* app) {
    char label[40];
    snprintf(
        label,
        sizeof(label),
        "Interval: %lus",
        (unsigned long)(field_logger_intervals_ms[app->interval_index] / 1000U));
    submenu_change_item_label(app->submenu, FieldLoggerMenuInterval, label);
}

static void field_logger_show_text(FieldLoggerApp* app) {
    text_box_set_text(app->text_box, furi_string_get_cstr(app->text));
    view_dispatcher_switch_to_view(app->view_dispatcher, FieldLoggerViewText);
}

static void field_logger_build_status(FieldLoggerApp* app) {
    FieldLoggerModel snapshot;
    with_view_model(app->run_view, FieldLoggerModel * model, { snapshot = *model; }, false);

    furi_string_reset(app->text);
    furi_string_cat_printf(
        app->text,
        "Field Logger Status\n\n"
        "Session: %s\n"
        "Records: %lu\n"
        "RF imports: %lu\n"
        "Errors: %lu\n"
        "Interval: %lu s\n\n"
        "Battery: %u%% %.2fV\n"
        "Charging: %s\n"
        "USB: %.2fV\n\n"
        "Sources\n"
        "Sensor Logger sessions: %s\n"
        "ARF RF notebook: %s\n\n"
        "Current outputs:\n%s\n%s\n%s\n",
        snapshot.session_name[0] ? snapshot.session_name : "none",
        (unsigned long)snapshot.record_count,
        (unsigned long)snapshot.rf_count,
        (unsigned long)snapshot.write_errors,
        (unsigned long)(snapshot.interval_ms / 1000U),
        furi_hal_power_get_pct(),
        (double)furi_hal_power_get_battery_voltage(FuriHalPowerICFuelGauge),
        furi_hal_power_is_charging() ? "yes" : "no",
        (double)furi_hal_power_get_usb_voltage(),
        field_logger_path_exists(app->storage, FIELD_LOGGER_SENSOR_SESSIONS_DIR) ? "present" :
                                                                                   "missing",
        field_logger_path_exists(app->storage, FIELD_LOGGER_RF_NOTEBOOK_CSV) ? "present" :
                                                                               "missing",
        snapshot.csv_path[0] ? snapshot.csv_path : "-",
        snapshot.jsonl_path[0] ? snapshot.jsonl_path : "-",
        snapshot.gpx_path[0] ? snapshot.gpx_path : "-");
}

static void field_logger_build_about(FieldLoggerApp* app) {
    furi_string_reset(app->text);
    furi_string_cat(
        app->text,
        "Field Logger\n\n"
        "Writes a unified field timeline to CSV, JSONL, and GPX.\n\n"
        "This first increment is receive-only and does not take CC1101 ownership. RF rows import the latest ARF Frequency Analyzer notebook observation when one exists.\n\n"
        "A one-shot iPhone GPS fix is added when available. BME280 data remains delegated to Sensor Logger; missing sources never block a session.");
}

static void field_logger_menu_callback(void* context, uint32_t index) {
    FieldLoggerApp* app = context;

    switch(index) {
    case FieldLoggerMenuStart:
        if(field_logger_start_session(app)) {
            view_dispatcher_switch_to_view(app->view_dispatcher, FieldLoggerViewRun);
        } else {
            furi_string_reset(app->text);
            furi_string_cat(app->text, "Cannot start Field Logger.\n\nCheck SD card and retry.");
            field_logger_show_text(app);
        }
        break;
    case FieldLoggerMenuInterval:
        app->interval_index = (app->interval_index + 1U) % COUNT_OF(field_logger_intervals_ms);
        field_logger_update_menu_labels(app);
        submenu_set_selected_item(app->submenu, FieldLoggerMenuInterval);
        break;
    case FieldLoggerMenuImportRf:
        if(!app->csv_file || !storage_file_is_open(app->csv_file)) {
            furi_string_reset(app->text);
            furi_string_cat(
                app->text,
                "RF import needs an active Field Logger session.\n\nStart a session, then press Right on the run screen.");
            field_logger_show_text(app);
        } else {
            field_logger_import_rf(app);
        }
        break;
    case FieldLoggerMenuStatus:
        field_logger_build_status(app);
        field_logger_show_text(app);
        break;
    case FieldLoggerMenuAbout:
        field_logger_build_about(app);
        field_logger_show_text(app);
        break;
    default:
        break;
    }
}

static bool field_logger_back_callback(void* context) {
    FieldLoggerApp* app = context;
    field_logger_stop_session(app);
    view_dispatcher_stop(app->view_dispatcher);
    return true;
}

static FieldLoggerApp* field_logger_alloc(void) {
    FieldLoggerApp* app = malloc(sizeof(FieldLoggerApp));
    memset(app, 0, sizeof(FieldLoggerApp));
    app->interval_index = 1;
    app->text = furi_string_alloc();
    app->line = furi_string_alloc();
    app->gui = furi_record_open(RECORD_GUI);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->notification = furi_record_open(RECORD_NOTIFICATION);
    app->view_dispatcher = view_dispatcher_alloc();
    app->device_services_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->device_services =
        tumoflip_device_services_client_alloc(field_logger_device_services_callback, app);

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, field_logger_back_callback);
    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher, field_logger_custom_event_callback);
    view_dispatcher_set_tick_event_callback(
        app->view_dispatcher, field_logger_tick_callback, FIELD_LOGGER_TICK_MS);

    app->submenu = submenu_alloc();
    submenu_set_header(app->submenu, "Field Logger");
    submenu_add_item(
        app->submenu, "Start Session", FieldLoggerMenuStart, field_logger_menu_callback, app);
    submenu_add_item(
        app->submenu, "Interval: 5s", FieldLoggerMenuInterval, field_logger_menu_callback, app);
    submenu_add_item(
        app->submenu, "Import Last RF", FieldLoggerMenuImportRf, field_logger_menu_callback, app);
    submenu_add_item(
        app->submenu, "Status", FieldLoggerMenuStatus, field_logger_menu_callback, app);
    submenu_add_item(app->submenu, "About", FieldLoggerMenuAbout, field_logger_menu_callback, app);
    field_logger_update_menu_labels(app);

    app->text_box = text_box_alloc();
    text_box_set_font(app->text_box, TextBoxFontText);
    text_box_set_focus(app->text_box, TextBoxFocusStart);
    view_set_previous_callback(
        text_box_get_view(app->text_box), field_logger_text_previous_callback);

    app->run_view = view_alloc();
    view_allocate_model(app->run_view, ViewModelTypeLocking, sizeof(FieldLoggerModel));
    view_set_draw_callback(app->run_view, field_logger_draw_callback);
    view_set_input_callback(app->run_view, field_logger_input_callback);
    view_set_previous_callback(app->run_view, field_logger_run_previous_callback);
    view_set_context(app->run_view, app);
    with_view_model(
        app->run_view,
        FieldLoggerModel * model,
        {
            memset(model, 0, sizeof(FieldLoggerModel));
            model->interval_ms = field_logger_intervals_ms[app->interval_index];
            model->battery_pct = furi_hal_power_get_pct();
            strlcpy(model->status, "Ready", sizeof(model->status));
        },
        true);

    view_dispatcher_add_view(
        app->view_dispatcher, FieldLoggerViewMenu, submenu_get_view(app->submenu));
    view_dispatcher_add_view(app->view_dispatcher, FieldLoggerViewRun, app->run_view);
    view_dispatcher_add_view(
        app->view_dispatcher, FieldLoggerViewText, text_box_get_view(app->text_box));
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_switch_to_view(app->view_dispatcher, FieldLoggerViewMenu);
    return app;
}

static void field_logger_free(FieldLoggerApp* app) {
    furi_assert(app);
    field_logger_stop_session(app);
    tumoflip_device_services_client_free(app->device_services);
    furi_mutex_free(app->device_services_mutex);
    view_dispatcher_remove_view(app->view_dispatcher, FieldLoggerViewText);
    view_dispatcher_remove_view(app->view_dispatcher, FieldLoggerViewRun);
    view_dispatcher_remove_view(app->view_dispatcher, FieldLoggerViewMenu);
    view_free(app->run_view);
    text_box_free(app->text_box);
    submenu_free(app->submenu);
    view_dispatcher_free(app->view_dispatcher);
    furi_string_free(app->line);
    furi_string_free(app->text);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_GUI);
    free(app);
}

int32_t field_logger_app(void* context) {
    UNUSED(context);
    FieldLoggerApp* app = field_logger_alloc();
    view_dispatcher_run(app->view_dispatcher);
    field_logger_free(app);
    return 0;
}
