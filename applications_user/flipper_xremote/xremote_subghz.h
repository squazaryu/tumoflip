#pragma once

#include <furi.h>
#include <storage/storage.h>

#include "xremote_device_profile.h"

typedef enum {
    XRemoteSubGhzStatusOk,
    XRemoteSubGhzStatusMissingFile,
    XRemoteSubGhzStatusInvalidFile,
    XRemoteSubGhzStatusUnsupportedPreset,
    XRemoteSubGhzStatusChangingCodeBlocked,
    XRemoteSubGhzStatusRegionBlocked,
    XRemoteSubGhzStatusRadioBusy,
    XRemoteSubGhzStatusExternalUnavailable,
    XRemoteSubGhzStatusRadioError,
    XRemoteSubGhzStatusTimeout,
} XRemoteSubGhzStatus;

typedef struct {
    char protocol[XREMOTE_DEVICE_PROTOCOL_MAX + 1U];
    uint32_t frequency_hz;
    uint32_t bit_count;
    uint64_t key;
    uint8_t original_variant;
    XRemoteDeviceAdapter adapter;
    bool changing_code;
} XRemoteSubGhzInfo;

XRemoteSubGhzStatus
    xremote_subghz_inspect(Storage* storage, const char* path, XRemoteSubGhzInfo* info);
XRemoteSubGhzStatus xremote_subghz_send(
    Storage* storage,
    const char* path,
    XRemoteDeviceRadio radio,
    XRemoteDeviceAdapter adapter,
    uint8_t variant);

const char* xremote_subghz_status_name(XRemoteSubGhzStatus status);
const char* xremote_subghz_variant_name(uint8_t variant);
