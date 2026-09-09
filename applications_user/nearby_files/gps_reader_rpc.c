#include "gps_reader.h"

#include <notification/notification_messages.h>

#include <stdlib.h>

#define RPC_POLL_PERIOD_MS 1000U
#define RPC_RESPONSE_TIMEOUT_MS 5000U

static bool gps_reader_parse_coordinate(const char* value, float* result) {
    if(!value || !result || !value[0]) return false;

    char* end = NULL;
    const float parsed = strtof(value, &end);
    if(end == value || *end != '\0') return false;
    if(parsed < -180.0f || parsed > 180.0f) return false;
    *result = parsed;
    return true;
}

static void gps_reader_rpc_callback(
    const TumoflipDeviceServicesResult* result,
    void* context) {
    GpsReader* gps_reader = context;
    if(!gps_reader || !result ||
       result->request != TumoflipDeviceServicesRequestLocation) {
        return;
    }

    const bool available = result->code != TumoflipDeviceServicesResultUnavailable;
    float latitude = 0.0f;
    float longitude = 0.0f;
    const bool got_fix = result->code == TumoflipDeviceServicesResultOk &&
                         gps_reader_parse_coordinate(result->value.location.latitude, &latitude) &&
                         gps_reader_parse_coordinate(result->value.location.longitude, &longitude) &&
                         latitude >= -90.0f && latitude <= 90.0f;

    furi_mutex_acquire(gps_reader->mutex, FuriWaitForever);
    gps_reader->rpc_last_rx = furi_get_tick();
    gps_reader->coordinates.module_detected = available;
    gps_reader->coordinates.valid = got_fix;
    if(got_fix) {
        gps_reader->coordinates.latitude = latitude;
        gps_reader->coordinates.longitude = longitude;
        gps_reader->coordinates.satellite_count = 0;
    } else {
        gps_reader->coordinates.latitude = 0.0f;
        gps_reader->coordinates.longitude = 0.0f;
        gps_reader->coordinates.satellite_count = 0;
    }
    furi_mutex_release(gps_reader->mutex);

    if(gps_reader->notifications) {
        notification_message(
            gps_reader->notifications,
            got_fix ? &sequence_blink_green_10 : &sequence_blink_red_10);
    }
}

static void gps_reader_rpc_poll(void* context) {
    GpsReader* gps_reader = context;
    if(!gps_reader || !gps_reader->device_services) return;

    const uint32_t now = furi_get_tick();
    furi_mutex_acquire(gps_reader->mutex, FuriWaitForever);
    const uint32_t last_rx = gps_reader->rpc_last_rx;
    furi_mutex_release(gps_reader->mutex);
    if(!tumoflip_device_services_client_busy(gps_reader->device_services) &&
       now - last_rx >= furi_ms_to_ticks(RPC_RESPONSE_TIMEOUT_MS)) {
        // The companion contract is deliberately one-shot. Re-requesting a fix
        // after a timeout keeps Refresh List useful without requiring a continuous
        // upstream GPS stream or a public gps_* ABI.
        tumoflip_device_services_client_request_location(
            gps_reader->device_services, "nearby_files");
    }
}

void gps_reader_start_rpc(GpsReader* gps_reader) {
    if(!gps_reader) return;

    gps_reader->rpc_last_rx = furi_get_tick();
    gps_reader->device_services = tumoflip_device_services_client_alloc(
        gps_reader_rpc_callback, gps_reader);
    if(!gps_reader->device_services) return;

    gps_reader->rpc_timer = furi_timer_alloc(
        gps_reader_rpc_poll, FuriTimerTypePeriodic, gps_reader);
    furi_timer_start(gps_reader->rpc_timer, furi_ms_to_ticks(RPC_POLL_PERIOD_MS));

    tumoflip_device_services_client_request_location(
        gps_reader->device_services, "nearby_files");
}

void gps_reader_stop_rpc(GpsReader* gps_reader) {
    if(!gps_reader) return;

    if(gps_reader->rpc_timer) {
        furi_timer_stop(gps_reader->rpc_timer);
        furi_timer_free(gps_reader->rpc_timer);
        gps_reader->rpc_timer = NULL;
    }
    if(gps_reader->device_services) {
        tumoflip_device_services_client_cancel(gps_reader->device_services);
        tumoflip_device_services_client_free(gps_reader->device_services);
        gps_reader->device_services = NULL;
    }
}
