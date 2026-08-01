#pragma once

#include <datetime/datetime.h>
#include <storage/storage.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TUMOFLIP_DEVICE_SERVICES_TIME_STATE_PATH \
    INT_PATH(".tumoflip/time-sync.txt")

typedef struct TumoflipDeviceServicesClient TumoflipDeviceServicesClient;

typedef enum {
    TumoflipDeviceServicesRequestTime,
    TumoflipDeviceServicesRequestLocation,
} TumoflipDeviceServicesRequest;

typedef enum {
    TumoflipDeviceServicesResultOk,
    TumoflipDeviceServicesResultUnavailable,
    TumoflipDeviceServicesResultTimeout,
    TumoflipDeviceServicesResultInvalid,
    TumoflipDeviceServicesResultRemoteError,
    TumoflipDeviceServicesResultCancelled,
} TumoflipDeviceServicesResultCode;

typedef struct {
    uint32_t unix_time;
    int32_t utc_offset_seconds;
    DateTime local_datetime;
} TumoflipDeviceTime;

typedef struct {
    char latitude[20];
    char longitude[20];
    char altitude[20];
    char accuracy[20];
    uint32_t unix_time;
} TumoflipDeviceLocation;

typedef struct {
    TumoflipDeviceServicesRequest request;
    TumoflipDeviceServicesResultCode code;
    char error[24];
    union {
        TumoflipDeviceTime time;
        TumoflipDeviceLocation location;
    } value;
} TumoflipDeviceServicesResult;

typedef void (*TumoflipDeviceServicesCallback)(
    const TumoflipDeviceServicesResult* result,
    void* context);

TumoflipDeviceServicesClient* tumoflip_device_services_client_alloc(
    TumoflipDeviceServicesCallback callback,
    void* context);

void tumoflip_device_services_client_free(TumoflipDeviceServicesClient* client);

bool tumoflip_device_services_client_request_time(
    TumoflipDeviceServicesClient* client,
    const char* purpose);

bool tumoflip_device_services_client_request_location(
    TumoflipDeviceServicesClient* client,
    const char* purpose);

bool tumoflip_device_services_client_busy(TumoflipDeviceServicesClient* client);

void tumoflip_device_services_client_cancel(TumoflipDeviceServicesClient* client);

bool tumoflip_device_services_apply_time(
    Storage* storage,
    const TumoflipDeviceTime* phone_time);

bool tumoflip_device_services_read_time(
    Storage* storage,
    TumoflipDeviceTime* phone_time);

bool tumoflip_device_services_write_sidecar(
    Storage* storage,
    const char* source_path,
    const char* capture_kind,
    const TumoflipDeviceLocation* location);

#ifdef __cplusplus
}
#endif
