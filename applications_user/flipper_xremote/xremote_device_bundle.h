#pragma once

#include "xremote_device_profile.h"

#define XREMOTE_DEVICE_BUNDLE_HEADER    "Tumo XRemote Device Bundle"
#define XREMOTE_DEVICE_BUNDLE_VERSION   1U
#define XREMOTE_DEVICE_BUNDLE_EXTENSION ".tdeck"
#define XREMOTE_DEVICE_BUNDLE_FOLDER    XREMOTE_DEVICE_PROFILE_ROOT "/bundles"
#define XREMOTE_DEVICE_IMPORT_FOLDER    XREMOTE_DEVICE_PROFILE_ROOT "/imports"

typedef enum {
    XRemoteDeviceBundleStatusOk,
    XRemoteDeviceBundleStatusInvalidProfile,
    XRemoteDeviceBundleStatusMissingSource,
    XRemoteDeviceBundleStatusInvalidSource,
    XRemoteDeviceBundleStatusInvalidBundle,
    XRemoteDeviceBundleStatusStorageError,
} XRemoteDeviceBundleStatus;

XRemoteDeviceBundleStatus xremote_device_bundle_export(
    Storage* storage,
    const XRemoteDeviceProfile* profile,
    char* manifest_path,
    size_t manifest_path_size);

XRemoteDeviceBundleStatus xremote_device_bundle_import(
    Storage* storage,
    const char* manifest_path,
    XRemoteDeviceProfile* imported_profile);

const char* xremote_device_bundle_status_name(XRemoteDeviceBundleStatus status);
