// Flipper Companion receives BLE App Bridge commands from TumoCompanion and
// exposes opt-in iPhone GPS, named network services, capture sidecars, and a
// field journal. Flipper never supplies arbitrary service hosts or secrets.
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
#include <dialogs/dialogs.h>
#include <storage/storage.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>
#include <bt/bt_service/bt.h>

#include "companion_subghz.h"
#include "companion_emulate.h"

#include <errno.h>
#include <stdlib.h>
#include <strings.h>

#define TAG                            "FlipperCompanion"
#define SUBGHZ_TX_DURATION_MS          500U
#define EMULATE_DURATION_MS            10000U
#define DEVICE_SERVICES_APP_ID         "device_services"
#define DEVICE_SERVICES_TIMEOUT_MS     12000U
#define DEVICE_SERVICES_RESPONSE_MAX   512U
#define DEVICE_SERVICES_HTTPS_TEST_URL "https://api.github.com/zen"
#define COMPANION_SOURCE_PATH_MAX      256U

typedef enum {
    CompanionMenuGps,
    CompanionMenuWeather,
    CompanionMenuPlace,
    CompanionMenuRelease,
    CompanionMenuTagFile,
    CompanionMenuJournal,
    CompanionMenuHttpsTest,
    CompanionMenuCount,
} CompanionMenuItem;

static const char* const companion_menu_labels[CompanionMenuCount] = {
    [CompanionMenuGps] = "GPS position",
    [CompanionMenuWeather] = "Weather now",
    [CompanionMenuPlace] = "Place name",
    [CompanionMenuRelease] = "Latest release",
    [CompanionMenuTagFile] = "Tag saved file",
    [CompanionMenuJournal] = "Field journal",
    [CompanionMenuHttpsTest] = "HTTPS test",
};

typedef enum {
    CompanionRequestNone,
    CompanionRequestGps,
    CompanionRequestWeather,
    CompanionRequestPlace,
    CompanionRequestRelease,
    CompanionRequestTagFile,
    CompanionRequestJournal,
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
    DialogsApp* dialogs;
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
    CompanionMenuItem selected_menu;
    char selected_source[COMPANION_SOURCE_PATH_MAX];
    bool busy;
} Companion;

static void companion_draw_callback(Canvas* canvas, void* ctx) {
    Companion* app = ctx;
    furi_mutex_acquire(app->mutex, FuriWaitForever);

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 11, "Field Services");
    canvas_set_font(canvas, FontSecondary);
    char menu_line[48];
    if(app->busy) {
        strlcpy(menu_line, "Requesting iPhone...", sizeof(menu_line));
    } else {
        snprintf(
            menu_line,
            sizeof(menu_line),
            "%u/%u  %s",
            (unsigned int)app->selected_menu + 1U,
            (unsigned int)CompanionMenuCount,
            companion_menu_labels[app->selected_menu]);
    }
    canvas_draw_str(canvas, 2, 23, menu_line);
    canvas_draw_line(canvas, 0, 27, 128, 27);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 40, furi_string_get_cstr(app->line1));
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 52, furi_string_get_cstr(app->line2));

    canvas_draw_str(canvas, 2, 63, app->busy ? "Back: Exit" : "Up/Down  OK: Run");

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

static bool companion_number_in_range(const char* value, double minimum, double maximum) {
    if(!value || !value[0]) return false;
    errno = 0;
    char* end = NULL;
    const double parsed = strtod(value, &end);
    return (errno == 0) && end && (end != value) && (*end == '\0') && (parsed >= minimum) &&
           (parsed <= maximum);
}

static bool companion_timestamp_valid(const char* value) {
    if(!value || !value[0]) return false;
    errno = 0;
    char* end = NULL;
    const unsigned long long parsed = strtoull(value, &end, 10);
    return (errno == 0) && end && (end != value) && (*end == '\0') && (parsed > 0ULL);
}

static const char* companion_capture_kind(const char* path) {
    const char* extension = strrchr(path, '.');
    if(!extension) return NULL;
    if(strcasecmp(extension, ".sub") == 0) return "subghz";
    if(strcasecmp(extension, ".nfc") == 0) return "nfc";
    if(strcasecmp(extension, ".rfid") == 0) return "lf_rfid";
    return NULL;
}

static bool companion_verify_file(Storage* storage, const char* path, const char* expected) {
    File* file = storage_file_alloc(storage);
    bool valid = false;
    const size_t expected_size = strlen(expected);
    if(storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING) &&
       (storage_file_size(file) == expected_size)) {
        char* contents = malloc(expected_size + 1U);
        if(contents) {
            const size_t read = storage_file_read(file, contents, expected_size);
            contents[read] = '\0';
            valid = (read == expected_size) && (memcmp(contents, expected, expected_size) == 0);
            free(contents);
        }
    }
    if(storage_file_is_open(file)) storage_file_close(file);
    storage_file_free(file);
    return valid;
}

static bool companion_write_sidecar(
    Companion* app,
    const char* response,
    char* detail,
    size_t detail_size) {
    const char* kind = companion_capture_kind(app->selected_source);
    if(!kind || !storage_file_exists(app->storage, app->selected_source)) {
        strlcpy(detail, "Source unavailable", detail_size);
        return false;
    }

    char latitude[20];
    char longitude[20];
    char altitude[20];
    char accuracy[20];
    char timestamp[24];
    char schema[4];
    const bool valid = companion_copy_field(response, "schema", schema, sizeof(schema)) &&
                       (strcmp(schema, "1") == 0) &&
                       companion_copy_field(response, "lat", latitude, sizeof(latitude)) &&
                       companion_copy_field(response, "lon", longitude, sizeof(longitude)) &&
                       companion_copy_field(response, "alt", altitude, sizeof(altitude)) &&
                       companion_copy_field(response, "acc", accuracy, sizeof(accuracy)) &&
                       companion_copy_field(response, "ts", timestamp, sizeof(timestamp)) &&
                       companion_number_in_range(latitude, -90.0, 90.0) &&
                       companion_number_in_range(longitude, -180.0, 180.0) &&
                       companion_number_in_range(altitude, -2000.0, 100000.0) &&
                       companion_number_in_range(accuracy, 0.0, 100000.0) &&
                       companion_timestamp_valid(timestamp);
    if(!valid) {
        strlcpy(detail, "Invalid GPS response", detail_size);
        return false;
    }

    char json[256];
    const int json_size = snprintf(
        json,
        sizeof(json),
        "{\"schema\":1,\"provider\":\"TumoCompanion/iPhone\",\"capture\":\"%s\","
        "\"lat\":%s,\"lon\":%s,\"alt\":%s,\"accuracy\":%s,\"timestamp\":%s}\n",
        kind,
        latitude,
        longitude,
        altitude,
        accuracy,
        timestamp);
    if((json_size <= 0) || ((size_t)json_size >= sizeof(json))) {
        strlcpy(detail, "Sidecar too large", detail_size);
        return false;
    }

    FuriString* destination = furi_string_alloc_printf("%s.tumoflip.json", app->selected_source);
    FuriString* temporary = furi_string_alloc_printf("%s.part", furi_string_get_cstr(destination));
    FuriString* backup = furi_string_alloc_printf("%s.bak", furi_string_get_cstr(destination));
    const char* destination_path = furi_string_get_cstr(destination);
    const char* temporary_path = furi_string_get_cstr(temporary);
    const char* backup_path = furi_string_get_cstr(backup);

    storage_common_remove(app->storage, temporary_path);
    File* file = storage_file_alloc(app->storage);
    bool success = storage_file_open(file, temporary_path, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    if(success) {
        success = (storage_file_write(file, json, (size_t)json_size) == (size_t)json_size) &&
                  storage_file_sync(file);
    }
    if(storage_file_is_open(file)) storage_file_close(file);
    storage_file_free(file);

    const bool had_destination = storage_file_exists(app->storage, destination_path);
    bool moved_destination = false;
    bool installed_new = false;
    storage_common_remove(app->storage, backup_path);
    if(success && had_destination) {
        success = storage_common_rename(app->storage, destination_path, backup_path) == FSE_OK;
        moved_destination = success;
    }
    if(success) {
        success = storage_common_rename(app->storage, temporary_path, destination_path) == FSE_OK;
        installed_new = success;
    }
    if(success) {
        success = companion_verify_file(app->storage, destination_path, json);
    }
    if(!success) {
        storage_common_remove(app->storage, temporary_path);
        if(installed_new) storage_common_remove(app->storage, destination_path);
        if(moved_destination) {
            storage_common_rename(app->storage, backup_path, destination_path);
        }
        strlcpy(detail, "Write rolled back", detail_size);
    } else {
        storage_common_remove(app->storage, backup_path);
        strlcpy(detail, "Sidecar saved", detail_size);
    }

    furi_string_free(backup);
    furi_string_free(temporary);
    furi_string_free(destination);
    return success;
}

static bool companion_select_source_file(Companion* app) {
    FuriString* path = furi_string_alloc_set(EXT_PATH(""));
    DialogsFileBrowserOptions browser;
    dialog_file_browser_set_basic_options(&browser, ".sub|.nfc|.rfid", NULL);
    browser.base_path = EXT_PATH("");
    browser.hide_ext = false;
    view_port_enabled_set(app->view_port, false);
    const bool selected = dialog_file_browser_show(app->dialogs, path, path, &browser);
    view_port_enabled_set(app->view_port, true);
    view_port_update(app->view_port);
    if(!selected) {
        furi_string_free(path);
        companion_set_lines(app, "Tag cancelled", "No file changed");
        return false;
    }

    const char* source = furi_string_get_cstr(path);
    const size_t source_size = strlen(source);
    const bool valid = (source_size > 0U) && (source_size < sizeof(app->selected_source)) &&
                       companion_capture_kind(source) && storage_file_exists(app->storage, source);
    if(valid) {
        strlcpy(app->selected_source, source, sizeof(app->selected_source));
    } else {
        companion_set_lines(app, "Unsupported file", "Choose SUB/NFC/RFID");
    }
    furi_string_free(path);
    return valid;
}

static bool companion_response_schema_valid(const char* payload) {
    char schema[4];
    return companion_copy_field(payload, "schema", schema, sizeof(schema)) &&
           (strcmp(schema, "1") == 0);
}

static void companion_show_device_response(Companion* app) {
    app->response[app->response_size] = '\0';
    char line1[48];
    char line2[48];
    const char* payload = (const char*)app->response;

    if((app->pending_request == CompanionRequestGps) ||
       (app->pending_request == CompanionRequestTagFile)) {
        char latitude[20];
        char longitude[20];
        if(companion_response_schema_valid(payload) &&
           companion_copy_field(payload, "lat", latitude, sizeof(latitude)) &&
           companion_copy_field(payload, "lon", longitude, sizeof(longitude)) &&
           companion_number_in_range(latitude, -90.0, 90.0) &&
           companion_number_in_range(longitude, -180.0, 180.0)) {
            if(app->pending_request == CompanionRequestTagFile) {
                const bool written = companion_write_sidecar(app, payload, line2, sizeof(line2));
                companion_finish_device_request(
                    app, written ? "File geotagged" : "Tag failed", line2, written);
                return;
            }
            snprintf(line1, sizeof(line1), "Lat %s", latitude);
            snprintf(line2, sizeof(line2), "Lon %s", longitude);
            companion_finish_device_request(app, line1, line2, true);
        } else {
            companion_finish_device_request(app, "GPS error", "Invalid response", false);
        }
        return;
    }

    if(app->pending_request == CompanionRequestWeather) {
        char temperature[16];
        char feels[16];
        char wind[16];
        if(companion_response_schema_valid(payload) &&
           companion_copy_field(payload, "temp", temperature, sizeof(temperature)) &&
           companion_copy_field(payload, "feels", feels, sizeof(feels)) &&
           companion_copy_field(payload, "wind", wind, sizeof(wind))) {
            snprintf(line1, sizeof(line1), "%s C  feels %s", temperature, feels);
            snprintf(line2, sizeof(line2), "Wind %s km/h", wind);
            companion_finish_device_request(app, line1, line2, true);
        } else {
            companion_finish_device_request(app, "Weather error", "Invalid response", false);
        }
        return;
    }

    if(app->pending_request == CompanionRequestPlace) {
        char place[42];
        char country[42];
        if(companion_response_schema_valid(payload) &&
           companion_copy_field(payload, "place", place, sizeof(place)) &&
           companion_copy_field(payload, "country", country, sizeof(country))) {
            snprintf(line1, sizeof(line1), "%.19s", place);
            snprintf(line2, sizeof(line2), "%.24s", country);
            companion_finish_device_request(app, line1, line2, true);
        } else {
            companion_finish_device_request(app, "Place error", "Invalid response", false);
        }
        return;
    }

    if(app->pending_request == CompanionRequestRelease) {
        char tag[34];
        char name[50];
        if(companion_response_schema_valid(payload) &&
           companion_copy_field(payload, "tag", tag, sizeof(tag))) {
            if(!companion_copy_field(payload, "name", name, sizeof(name))) {
                strlcpy(name, "Stable firmware", sizeof(name));
            }
            snprintf(line2, sizeof(line2), "%.24s", name);
            companion_finish_device_request(app, tag, line2, true);
        } else {
            companion_finish_device_request(app, "Release error", "Invalid response", false);
        }
        return;
    }

    if(app->pending_request == CompanionRequestJournal) {
        char stored[4];
        char delivery[20];
        if(companion_response_schema_valid(payload) &&
           companion_copy_field(payload, "stored", stored, sizeof(stored)) &&
           (strcmp(stored, "1") == 0)) {
            if(!companion_copy_field(payload, "delivery", delivery, sizeof(delivery))) {
                strlcpy(delivery, "local", sizeof(delivery));
            }
            snprintf(line2, sizeof(line2), "Delivery: %s", delivery);
            companion_finish_device_request(app, "Journal saved", line2, true);
        } else {
            companion_finish_device_request(app, "Journal error", "Not stored", false);
        }
        return;
    }

    const char* body = strchr(payload, '\n');
    size_t header_size = body ? (size_t)(body - payload) : strlen(payload);
    if(header_size >= sizeof(line1)) header_size = sizeof(line1) - 1U;
    memcpy(line1, payload, header_size);
    line1[header_size] = '\0';
    if(body) body++;
    snprintf(line2, sizeof(line2), "%.38s", body ? body : "No body");
    companion_finish_device_request(app, line1, line2, true);
}

static const char* companion_expected_command(CompanionRequest request) {
    switch(request) {
    case CompanionRequestGps:
    case CompanionRequestTagFile:
        return "gps_once";
    case CompanionRequestWeather:
        return "weather_now";
    case CompanionRequestPlace:
        return "place_once";
    case CompanionRequestRelease:
        return "release_latest";
    case CompanionRequestJournal:
        return "journal_append";
    case CompanionRequestHttps:
        return "https_get";
    case CompanionRequestNone:
        return NULL;
    }
    return NULL;
}

static void companion_handle_device_response(Companion* app, const BtAppBridgeEvent* event) {
    if((app->pending_request == CompanionRequestNone) ||
       (event->request_id != app->pending_request_id)) {
        return;
    }
    const char* expected_command = companion_expected_command(app->pending_request);
    if(!expected_command || (strcmp(event->command, expected_command) != 0)) {
        companion_finish_device_request(app, "Protocol error", "Wrong response type", false);
        return;
    }

    if((event->flags & BtAppBridgeFlagError) != 0U) {
        char error[48];
        size_t size = event->payload_len;
        if(size >= sizeof(error)) size = sizeof(error) - 1U;
        memcpy(error, event->payload, size);
        error[size] = '\0';
        companion_finish_device_request(
            app, "Service unavailable", error[0] ? error : "Request failed", false);
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
    const char* command = NULL;
    const char* payload = "schema=1";
    const char* title = "Requesting service";
    const char* detail = "Keep iPhone nearby";
    switch(request) {
    case CompanionRequestGps:
        command = "gps_once";
        payload = "schema=1;purpose=manual";
        title = "Requesting GPS";
        detail = "Allow on iPhone";
        break;
    case CompanionRequestWeather:
        command = "weather_now";
        title = "Requesting weather";
        break;
    case CompanionRequestPlace:
        command = "place_once";
        title = "Requesting place";
        break;
    case CompanionRequestRelease:
        command = "release_latest";
        title = "Checking release";
        break;
    case CompanionRequestTagFile:
        command = "gps_once";
        payload = "schema=1;purpose=sidecar";
        title = "Getting file GPS";
        break;
    case CompanionRequestJournal:
        command = "journal_append";
        payload = "schema=1;kind=field;note=Marked on Flipper";
        title = "Saving journal";
        break;
    case CompanionRequestHttps:
        command = "https_get";
        payload = DEVICE_SERVICES_HTTPS_TEST_URL;
        title = "Requesting HTTPS";
        detail = "api.github.com/zen";
        break;
    case CompanionRequestNone:
        return;
    }

    app->pending_request = request;
    app->pending_request_id = request_id;
    app->request_started_at = furi_get_tick();
    app->response_chunk_count = 0U;
    app->response_next_chunk = 0U;
    app->response_size = 0U;
    companion_set_busy(app, true);
    companion_set_lines(app, title, detail);

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

static void companion_run_selected(Companion* app) {
    CompanionRequest request = CompanionRequestNone;
    switch(app->selected_menu) {
    case CompanionMenuGps:
        request = CompanionRequestGps;
        break;
    case CompanionMenuWeather:
        request = CompanionRequestWeather;
        break;
    case CompanionMenuPlace:
        request = CompanionRequestPlace;
        break;
    case CompanionMenuRelease:
        request = CompanionRequestRelease;
        break;
    case CompanionMenuTagFile:
        if(companion_select_source_file(app)) request = CompanionRequestTagFile;
        break;
    case CompanionMenuJournal:
        request = CompanionRequestJournal;
        break;
    case CompanionMenuHttpsTest:
        request = CompanionRequestHttps;
        break;
    case CompanionMenuCount:
        break;
    }
    companion_start_device_request(app, request);
}

int32_t flipper_companion_app(void* p) {
    UNUSED(p);
    Companion* app = malloc(sizeof(Companion));
    memset(app, 0, sizeof(Companion));

    app->queue = furi_message_queue_alloc(16, sizeof(CompanionEvent));
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->line1 = furi_string_alloc_set("Ready");
    app->line2 = furi_string_alloc_set("Choose a named service");
    app->request_id_next = furi_get_tick();

    app->storage = furi_record_open(RECORD_STORAGE);
    app->dialogs = furi_record_open(RECORD_DIALOGS);
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
            } else if(
                (event.input.type == InputTypeShort) && !app->busy &&
                (event.input.key == InputKeyUp)) {
                app->selected_menu = app->selected_menu == CompanionMenuGps ?
                                         (CompanionMenuItem)(CompanionMenuCount - 1U) :
                                         (CompanionMenuItem)(app->selected_menu - 1U);
                view_port_update(app->view_port);
            } else if(
                (event.input.type == InputTypeShort) && !app->busy &&
                (event.input.key == InputKeyDown)) {
                app->selected_menu =
                    (CompanionMenuItem)((app->selected_menu + 1U) % CompanionMenuCount);
                view_port_update(app->view_port);
            } else if(
                (event.input.type == InputTypeShort) && !app->busy &&
                (event.input.key == InputKeyOk)) {
                companion_run_selected(app);
            } else if(
                (event.input.type == InputTypeShort) && !app->busy &&
                (event.input.key == InputKeyLeft)) {
                app->selected_menu = CompanionMenuGps;
                companion_run_selected(app);
            } else if(
                (event.input.type == InputTypeShort) && !app->busy &&
                (event.input.key == InputKeyRight)) {
                app->selected_menu = CompanionMenuHttpsTest;
                companion_run_selected(app);
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
    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_STORAGE);
    free(app);
    return 0;
}
