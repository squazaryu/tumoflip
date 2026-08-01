#include "tumoflip_device_services.h"

#include <bt/bt_service/bt.h>
#include <furi.h>
#include <furi_hal_rtc.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAG "TumoDeviceServices"

#define TUMOFLIP_DEVICE_SERVICES_APP_ID          "device_services"
#define TUMOFLIP_DEVICE_SERVICES_RESPONSE_MAX    512U
#define TUMOFLIP_DEVICE_SERVICES_TIMEOUT_MS      12000U
#define TUMOFLIP_DEVICE_SERVICES_TIME_STATE_DIR  INT_PATH(".tumoflip")
#define TUMOFLIP_DEVICE_SERVICES_TIME_STATE_TEMP \
    INT_PATH(".tumoflip/time-sync.txt.part")
#define TUMOFLIP_DEVICE_SERVICES_TIME_STATE_BAK \
    INT_PATH(".tumoflip/time-sync.txt.bak")

typedef struct {
    bool stop;
    BtAppBridgeEvent bridge;
} TumoflipDeviceServicesQueueEvent;

struct TumoflipDeviceServicesClient {
    Bt* bt;
    FuriPubSub* pubsub;
    FuriPubSubSubscription* subscription;
    FuriMessageQueue* queue;
    FuriThread* worker;
    FuriMutex* mutex;
    TumoflipDeviceServicesCallback callback;
    void* callback_context;
    TumoflipDeviceServicesRequest pending_request;
    uint32_t next_request_id;
    uint32_t pending_request_id;
    char pending_command[BT_APP_BRIDGE_COMMAND_LEN_MAX + 1U];
    uint32_t request_started_at;
    uint8_t response_chunk_count;
    uint8_t response_next_chunk;
    size_t response_size;
    uint8_t response[TUMOFLIP_DEVICE_SERVICES_RESPONSE_MAX + 1U];
};

static bool tumoflip_device_services_copy_field(
    const char* payload,
    const char* key,
    char* output,
    size_t output_size) {
    if(!payload || !key || !output || output_size == 0U) return false;

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

static bool tumoflip_device_services_parse_u32(
    const char* value,
    uint32_t minimum,
    uint32_t maximum,
    uint32_t* parsed) {
    if(!value || !value[0] || !parsed) return false;
    uint32_t result = 0U;
    for(const char* cursor = value; *cursor; cursor++) {
        if(*cursor < '0' || *cursor > '9') return false;
        const uint32_t digit = (uint32_t)(*cursor - '0');
        if(result > (UINT32_MAX - digit) / 10U) return false;
        result = result * 10U + digit;
    }
    if(result < minimum || result > maximum) return false;
    *parsed = result;
    return true;
}

static bool tumoflip_device_services_parse_i32(
    const char* value,
    int32_t minimum,
    int32_t maximum,
    int32_t* parsed) {
    if(!value || !value[0] || !parsed) return false;
    bool negative = value[0] == '-';
    const char* digits = negative ? value + 1U : value;
    if(!digits[0]) return false;
    uint32_t magnitude = 0U;
    if(!tumoflip_device_services_parse_u32(digits, 0U, INT32_MAX, &magnitude)) return false;
    const int32_t result = negative ? -(int32_t)magnitude : (int32_t)magnitude;
    if(result < minimum || result > maximum) return false;
    *parsed = result;
    return true;
}

static bool tumoflip_device_services_local_timestamp(
    uint32_t unix_time,
    int32_t offset,
    uint32_t* local_timestamp) {
    if(offset >= 0) {
        const uint32_t positive = (uint32_t)offset;
        if(unix_time > UINT32_MAX - positive) return false;
        *local_timestamp = unix_time + positive;
    } else {
        const uint32_t negative = (uint32_t)(-offset);
        if(unix_time < negative) return false;
        *local_timestamp = unix_time - negative;
    }
    return *local_timestamp >= 946684800U && *local_timestamp <= 4102444799U;
}

static bool tumoflip_device_services_number_in_range(
    const char* value,
    float minimum,
    float maximum) {
    if(!value || !value[0]) return false;
    errno = 0;
    char* end = NULL;
    const float parsed = strtof(value, &end);
    return errno == 0 && end != value && *end == '\0' && parsed >= minimum && parsed <= maximum;
}

static bool tumoflip_device_services_parse_time(
    const char* payload,
    TumoflipDeviceTime* time) {
    char schema[4];
    char unix_value[24];
    char offset_value[16];
    uint32_t unix_time = 0U;
    int32_t offset = 0;
    if(!tumoflip_device_services_copy_field(payload, "schema", schema, sizeof(schema)) ||
       strcmp(schema, "1") != 0 ||
       !tumoflip_device_services_copy_field(payload, "unix", unix_value, sizeof(unix_value)) ||
       !tumoflip_device_services_copy_field(
           payload, "offset", offset_value, sizeof(offset_value)) ||
       !tumoflip_device_services_parse_u32(
           unix_value, 946684800U, 4102444799U, &unix_time) ||
       !tumoflip_device_services_parse_i32(offset_value, -50400, 50400, &offset)) {
        return false;
    }

    uint32_t local_timestamp = 0U;
    if(!tumoflip_device_services_local_timestamp(unix_time, offset, &local_timestamp)) return false;

    time->unix_time = unix_time;
    time->utc_offset_seconds = offset;
    datetime_timestamp_to_datetime(local_timestamp, &time->local_datetime);
    return datetime_validate_datetime(&time->local_datetime);
}

static bool tumoflip_device_services_parse_location(
    const char* payload,
    TumoflipDeviceLocation* location) {
    char schema[4];
    char timestamp[24];
    uint32_t unix_time = 0U;
    const bool valid =
        tumoflip_device_services_copy_field(payload, "schema", schema, sizeof(schema)) &&
        strcmp(schema, "1") == 0 &&
        tumoflip_device_services_copy_field(
            payload, "lat", location->latitude, sizeof(location->latitude)) &&
        tumoflip_device_services_copy_field(
            payload, "lon", location->longitude, sizeof(location->longitude)) &&
        tumoflip_device_services_copy_field(
            payload, "alt", location->altitude, sizeof(location->altitude)) &&
        tumoflip_device_services_copy_field(
            payload, "acc", location->accuracy, sizeof(location->accuracy)) &&
        tumoflip_device_services_copy_field(payload, "ts", timestamp, sizeof(timestamp)) &&
        tumoflip_device_services_number_in_range(location->latitude, -90.0f, 90.0f) &&
        tumoflip_device_services_number_in_range(location->longitude, -180.0f, 180.0f) &&
        tumoflip_device_services_number_in_range(location->altitude, -2000.0f, 100000.0f) &&
        tumoflip_device_services_number_in_range(location->accuracy, 0.0f, 100000.0f) &&
        tumoflip_device_services_parse_u32(timestamp, 946684800U, 4102444799U, &unix_time);
    if(valid) location->unix_time = unix_time;
    return valid;
}

static void tumoflip_device_services_reset_locked(TumoflipDeviceServicesClient* client) {
    client->pending_request_id = 0U;
    client->pending_command[0] = '\0';
    client->response_chunk_count = 0U;
    client->response_next_chunk = 0U;
    client->response_size = 0U;
}

static void tumoflip_device_services_complete(
    TumoflipDeviceServicesClient* client,
    uint32_t expected_request_id,
    TumoflipDeviceServicesResultCode code,
    const char* error) {
    char payload[TUMOFLIP_DEVICE_SERVICES_RESPONSE_MAX + 1U];
    TumoflipDeviceServicesResult result = {.code = code};

    furi_mutex_acquire(client->mutex, FuriWaitForever);
    if(client->pending_request_id == 0U || client->pending_request_id != expected_request_id) {
        furi_mutex_release(client->mutex);
        return;
    }
    result.request = client->pending_request;
    memcpy(payload, client->response, client->response_size);
    payload[client->response_size] = '\0';
    tumoflip_device_services_reset_locked(client);
    furi_mutex_release(client->mutex);

    if(error) {
        strlcpy(result.error, error, sizeof(result.error));
    } else if(code == TumoflipDeviceServicesResultRemoteError) {
        strlcpy(result.error, payload, sizeof(result.error));
    }

    if(code == TumoflipDeviceServicesResultOk) {
        const bool parsed = result.request == TumoflipDeviceServicesRequestTime ?
                                tumoflip_device_services_parse_time(payload, &result.value.time) :
                                tumoflip_device_services_parse_location(
                                    payload, &result.value.location);
        if(!parsed) {
            result.code = TumoflipDeviceServicesResultInvalid;
            strlcpy(result.error, "invalid_response", sizeof(result.error));
        }
    }

    if(client->callback) client->callback(&result, client->callback_context);
}

static void tumoflip_device_services_handle_bridge(
    TumoflipDeviceServicesClient* client,
    const BtAppBridgeEvent* event) {
    furi_mutex_acquire(client->mutex, FuriWaitForever);
    if(client->pending_request_id == 0U || event->request_id != client->pending_request_id ||
       strcmp(event->command, client->pending_command) != 0) {
        furi_mutex_release(client->mutex);
        return;
    }

    const bool invalid = event->chunk_count == 0U ||
                         (client->response_chunk_count != 0U &&
                          client->response_chunk_count != event->chunk_count) ||
                         event->chunk_index != client->response_next_chunk ||
                         client->response_size + event->payload_len >
                             TUMOFLIP_DEVICE_SERVICES_RESPONSE_MAX;
    if(invalid) {
        furi_mutex_release(client->mutex);
        tumoflip_device_services_complete(
            client,
            event->request_id,
            TumoflipDeviceServicesResultInvalid,
            "invalid_frame");
        return;
    }

    if(client->response_chunk_count == 0U) client->response_chunk_count = event->chunk_count;
    memcpy(&client->response[client->response_size], event->payload, event->payload_len);
    client->response_size += event->payload_len;
    client->response_next_chunk++;
    const bool complete = client->response_next_chunk == client->response_chunk_count;
    const bool remote_error = (event->flags & BtAppBridgeFlagError) != 0U;
    furi_mutex_release(client->mutex);

    if(complete) {
        if(remote_error) {
            tumoflip_device_services_complete(
                client,
                event->request_id,
                TumoflipDeviceServicesResultRemoteError,
                NULL);
        } else {
            tumoflip_device_services_complete(
                client, event->request_id, TumoflipDeviceServicesResultOk, NULL);
        }
    }
}

static void tumoflip_device_services_bridge_callback(const void* message, void* context) {
    TumoflipDeviceServicesClient* client = context;
    const BtAppBridgeEvent* event = message;
    if(event->protocol_version != 2U || (event->flags & BtAppBridgeFlagResponse) == 0U ||
       strcmp(event->app_id, TUMOFLIP_DEVICE_SERVICES_APP_ID) != 0) {
        return;
    }

    TumoflipDeviceServicesQueueEvent queued = {.bridge = *event};
    furi_message_queue_put(client->queue, &queued, 0U);
}

static int32_t tumoflip_device_services_worker(void* context) {
    TumoflipDeviceServicesClient* client = context;
    TumoflipDeviceServicesQueueEvent event;
    while(true) {
        const FuriStatus status =
            furi_message_queue_get(client->queue, &event, furi_ms_to_ticks(250U));
        if(status == FuriStatusOk) {
            if(event.stop) break;
            tumoflip_device_services_handle_bridge(client, &event.bridge);
        }

        furi_mutex_acquire(client->mutex, FuriWaitForever);
        const uint32_t timed_out_request_id = client->pending_request_id;
        const bool timed_out = timed_out_request_id != 0U &&
                               furi_get_tick() - client->request_started_at >=
                                   furi_ms_to_ticks(TUMOFLIP_DEVICE_SERVICES_TIMEOUT_MS);
        furi_mutex_release(client->mutex);
        if(timed_out) {
            tumoflip_device_services_complete(
                client,
                timed_out_request_id,
                TumoflipDeviceServicesResultTimeout,
                "timeout");
        }
    }
    return 0;
}

TumoflipDeviceServicesClient* tumoflip_device_services_client_alloc(
    TumoflipDeviceServicesCallback callback,
    void* context) {
    TumoflipDeviceServicesClient* client = malloc(sizeof(TumoflipDeviceServicesClient));
    if(!client) return NULL;
    memset(client, 0, sizeof(TumoflipDeviceServicesClient));
    client->callback = callback;
    client->callback_context = context;
    client->next_request_id = furi_get_tick();
    client->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    client->queue = furi_message_queue_alloc(8U, sizeof(TumoflipDeviceServicesQueueEvent));
    client->bt = furi_record_open(RECORD_BT);
    client->pubsub = bt_app_bridge_get_pubsub(client->bt);
    client->subscription =
        furi_pubsub_subscribe(client->pubsub, tumoflip_device_services_bridge_callback, client);
    client->worker = furi_thread_alloc_ex(
        "TumoDeviceSvc", 2048U, tumoflip_device_services_worker, client);
    furi_thread_start(client->worker);
    return client;
}

void tumoflip_device_services_client_free(TumoflipDeviceServicesClient* client) {
    if(!client) return;
    furi_pubsub_unsubscribe(client->pubsub, client->subscription);
    TumoflipDeviceServicesQueueEvent event = {.stop = true};
    furi_message_queue_put(client->queue, &event, FuriWaitForever);
    furi_thread_join(client->worker);
    furi_thread_free(client->worker);
    furi_record_close(RECORD_BT);
    furi_message_queue_free(client->queue);
    furi_mutex_free(client->mutex);
    free(client);
}

static bool tumoflip_device_services_request(
    TumoflipDeviceServicesClient* client,
    TumoflipDeviceServicesRequest request,
    const char* command,
    const char* payload) {
    if(!client || !command || !payload) return false;

    furi_mutex_acquire(client->mutex, FuriWaitForever);
    if(client->pending_request_id != 0U) {
        furi_mutex_release(client->mutex);
        return false;
    }
    client->next_request_id++;
    if(client->next_request_id == 0U) client->next_request_id++;
    client->pending_request = request;
    client->pending_request_id = client->next_request_id;
    strlcpy(client->pending_command, command, sizeof(client->pending_command));
    client->request_started_at = furi_get_tick();
    client->response_chunk_count = 0U;
    client->response_next_chunk = 0U;
    client->response_size = 0U;
    const uint32_t request_id = client->pending_request_id;
    furi_mutex_release(client->mutex);

    const bool sent = bt_app_bridge_send_v2(
        client->bt,
        TUMOFLIP_DEVICE_SERVICES_APP_ID,
        command,
        request_id,
        BtAppBridgeFlagAckRequested,
        0U,
        1U,
        (const uint8_t*)payload,
        strlen(payload));
    if(!sent) {
        tumoflip_device_services_complete(
            client,
            request_id,
            TumoflipDeviceServicesResultUnavailable,
            "unavailable");
    }
    return sent;
}

bool tumoflip_device_services_client_request_time(
    TumoflipDeviceServicesClient* client,
    const char* purpose) {
    if(!purpose || !purpose[0] || strchr(purpose, ';') || strchr(purpose, '=')) return false;
    char payload[80];
    const int length = snprintf(payload, sizeof(payload), "schema=1;purpose=%s", purpose);
    if(length <= 0 || (size_t)length >= sizeof(payload)) return false;
    return tumoflip_device_services_request(
        client, TumoflipDeviceServicesRequestTime, "time_once", payload);
}

bool tumoflip_device_services_client_request_location(
    TumoflipDeviceServicesClient* client,
    const char* purpose) {
    if(!purpose || !purpose[0] || strchr(purpose, ';') || strchr(purpose, '=')) return false;
    char payload[80];
    const int length = snprintf(payload, sizeof(payload), "schema=1;purpose=%s", purpose);
    if(length <= 0 || (size_t)length >= sizeof(payload)) return false;
    return tumoflip_device_services_request(
        client, TumoflipDeviceServicesRequestLocation, "gps_once", payload);
}

bool tumoflip_device_services_client_busy(TumoflipDeviceServicesClient* client) {
    if(!client) return false;
    furi_mutex_acquire(client->mutex, FuriWaitForever);
    const bool busy = client->pending_request_id != 0U;
    furi_mutex_release(client->mutex);
    return busy;
}

void tumoflip_device_services_client_cancel(TumoflipDeviceServicesClient* client) {
    if(!client) return;
    furi_mutex_acquire(client->mutex, FuriWaitForever);
    const bool pending = client->pending_request_id != 0U;
    const TumoflipDeviceServicesRequest request = client->pending_request;
    tumoflip_device_services_reset_locked(client);
    furi_mutex_release(client->mutex);
    if(pending && client->callback) {
        const TumoflipDeviceServicesResult result = {
            .request = request,
            .code = TumoflipDeviceServicesResultCancelled,
            .error = "cancelled",
        };
        client->callback(&result, client->callback_context);
    }
}

static bool tumoflip_device_services_write_verified(
    Storage* storage,
    const char* path,
    const char* temporary,
    const char* backup,
    const char* contents) {
    storage_common_remove(storage, temporary);
    File* file = storage_file_alloc(storage);
    const size_t size = strlen(contents);
    bool success = storage_file_open(file, temporary, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    if(success) {
        success = storage_file_write(file, contents, size) == size && storage_file_sync(file);
    }
    if(storage_file_is_open(file)) storage_file_close(file);
    storage_file_free(file);

    const bool had_destination = storage_file_exists(storage, path);
    bool moved_destination = false;
    bool installed_new = false;
    storage_common_remove(storage, backup);
    if(success && had_destination) {
        success = storage_common_rename(storage, path, backup) == FSE_OK;
        moved_destination = success;
    }
    if(success) {
        success = storage_common_rename(storage, temporary, path) == FSE_OK;
        installed_new = success;
    }
    if(success) {
        file = storage_file_alloc(storage);
        success = storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING) &&
                  storage_file_size(file) == size;
        if(success) {
            char* readback = malloc(size + 1U);
            success = readback && storage_file_read(file, readback, size) == size &&
                      memcmp(readback, contents, size) == 0;
            free(readback);
        }
        if(storage_file_is_open(file)) storage_file_close(file);
        storage_file_free(file);
    }
    if(!success) {
        storage_common_remove(storage, temporary);
        if(installed_new) storage_common_remove(storage, path);
        if(moved_destination) storage_common_rename(storage, backup, path);
    } else {
        storage_common_remove(storage, backup);
    }
    return success;
}

bool tumoflip_device_services_apply_time(
    Storage* storage,
    const TumoflipDeviceTime* phone_time) {
    if(!phone_time) return false;
    DateTime local_datetime = phone_time->local_datetime;
    if(!datetime_validate_datetime(&local_datetime)) return false;
    furi_hal_rtc_set_datetime(&local_datetime);
    if(!storage) return true;

    const FS_Error mkdir_result = storage_common_mkdir(storage, TUMOFLIP_DEVICE_SERVICES_TIME_STATE_DIR);
    if(mkdir_result != FSE_OK && mkdir_result != FSE_EXIST) {
        FURI_LOG_W(TAG, "RTC set but time sync state directory is unavailable");
        return true;
    }

    char contents[128];
    const uint32_t local_timestamp =
        datetime_datetime_to_timestamp(&local_datetime);
    const int length = snprintf(
        contents,
        sizeof(contents),
        "schema=1;unix=%lu;offset=%ld;local=%lu\n",
        (unsigned long)phone_time->unix_time,
        (long)phone_time->utc_offset_seconds,
        (unsigned long)local_timestamp);
    if(length <= 0 || (size_t)length >= sizeof(contents)) return true;
    const bool persisted = tumoflip_device_services_write_verified(
        storage,
        TUMOFLIP_DEVICE_SERVICES_TIME_STATE_PATH,
        TUMOFLIP_DEVICE_SERVICES_TIME_STATE_TEMP,
        TUMOFLIP_DEVICE_SERVICES_TIME_STATE_BAK,
        contents);
    if(!persisted) FURI_LOG_W(TAG, "RTC set but trusted time state was not persisted");
    return true;
}

bool tumoflip_device_services_read_time(
    Storage* storage,
    TumoflipDeviceTime* phone_time) {
    if(!storage || !phone_time) return false;
    File* file = storage_file_alloc(storage);
    char contents[128];
    bool success = storage_file_open(
        file, TUMOFLIP_DEVICE_SERVICES_TIME_STATE_PATH, FSAM_READ, FSOM_OPEN_EXISTING);
    size_t size = success ? storage_file_size(file) : 0U;
    success = success && size > 0U && size < sizeof(contents) &&
              storage_file_read(file, contents, size) == size;
    if(storage_file_is_open(file)) storage_file_close(file);
    storage_file_free(file);
    if(!success) return false;
    contents[size] = '\0';

    char schema[4];
    char unix_value[24];
    char offset_value[16];
    char local_value[24];
    uint32_t unix_time = 0U;
    int32_t offset = 0;
    uint32_t local = 0U;
    uint32_t expected_local = 0U;
    success = tumoflip_device_services_copy_field(contents, "schema", schema, sizeof(schema)) &&
              strcmp(schema, "1") == 0 &&
              tumoflip_device_services_copy_field(
                  contents, "unix", unix_value, sizeof(unix_value)) &&
              tumoflip_device_services_copy_field(
                  contents, "offset", offset_value, sizeof(offset_value)) &&
              tumoflip_device_services_copy_field(
                  contents, "local", local_value, sizeof(local_value)) &&
              tumoflip_device_services_parse_u32(
                  unix_value, 946684800U, 4102444799U, &unix_time) &&
              tumoflip_device_services_parse_i32(offset_value, -50400, 50400, &offset) &&
              tumoflip_device_services_parse_u32(
                  local_value, 946684800U, 4102444799U, &local) &&
              tumoflip_device_services_local_timestamp(unix_time, offset, &expected_local) &&
              local == expected_local;
    if(!success) return false;
    phone_time->unix_time = unix_time;
    phone_time->utc_offset_seconds = offset;
    datetime_timestamp_to_datetime(local, &phone_time->local_datetime);
    return datetime_validate_datetime(&phone_time->local_datetime);
}

bool tumoflip_device_services_write_sidecar(
    Storage* storage,
    const char* source_path,
    const char* capture_kind,
    const TumoflipDeviceLocation* location) {
    if(!storage || !source_path || !capture_kind || !capture_kind[0] || !location ||
       strchr(capture_kind, '"') || strchr(capture_kind, '\\') ||
       !tumoflip_device_services_number_in_range(location->latitude, -90.0f, 90.0f) ||
       !tumoflip_device_services_number_in_range(location->longitude, -180.0f, 180.0f) ||
       !tumoflip_device_services_number_in_range(location->altitude, -2000.0f, 100000.0f) ||
       !tumoflip_device_services_number_in_range(location->accuracy, 0.0f, 100000.0f) ||
       location->unix_time < 946684800U || location->unix_time > 4102444799U ||
       !storage_file_exists(storage, source_path)) {
        return false;
    }

    char json[256];
    const int json_size = snprintf(
        json,
        sizeof(json),
        "{\"schema\":1,\"provider\":\"TumoCompanion/iPhone\",\"capture\":\"%s\","
        "\"lat\":%s,\"lon\":%s,\"alt\":%s,\"accuracy\":%s,\"timestamp\":%lu}\n",
        capture_kind,
        location->latitude,
        location->longitude,
        location->altitude,
        location->accuracy,
        (unsigned long)location->unix_time);
    if(json_size <= 0 || (size_t)json_size >= sizeof(json)) return false;

    FuriString* path = furi_string_alloc_printf("%s.tumoflip.json", source_path);
    FuriString* temporary = furi_string_alloc_printf("%s.part", furi_string_get_cstr(path));
    FuriString* backup = furi_string_alloc_printf("%s.bak", furi_string_get_cstr(path));
    const bool success = tumoflip_device_services_write_verified(
        storage,
        furi_string_get_cstr(path),
        furi_string_get_cstr(temporary),
        furi_string_get_cstr(backup),
        json);
    furi_string_free(backup);
    furi_string_free(temporary);
    furi_string_free(path);
    return success;
}
