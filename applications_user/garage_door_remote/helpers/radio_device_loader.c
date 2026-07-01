// helpers/radio_device_loader.c
#include "radio_device_loader.h"

#include <applications/drivers/subghz/cc1101_ext/cc1101_ext_interconnect.h>
#include <lib/subghz/devices/cc1101_int/cc1101_int_interconnect.h>
#include <furi.h>
#include "../defines.h"

#define TAG "RadioDeviceLoader"

static bool radio_device_loader_power_on(
    SubGhzRadioBroker* broker,
    const SubGhzRadioBrokerLease* lease) {
    return subghz_radio_broker_external_power_on(broker, lease);
}

static void radio_device_loader_power_off(
    SubGhzRadioBroker* broker,
    const SubGhzRadioBrokerLease* lease) {
    subghz_radio_broker_external_power_off(broker, lease);
}

bool radio_device_loader_is_connect_external(
    const char* name,
    SubGhzRadioBroker* broker,
    const SubGhzRadioBrokerLease* lease) {
    bool is_connect = false;
    const bool powered = radio_device_loader_power_on(broker, lease);

    const SubGhzDevice* device = powered ? subghz_devices_get_by_name(name) : NULL;
    if(device) {
        is_connect = subghz_devices_is_connect(device);
        FURI_LOG_D(TAG, "External device '%s' connect check: %s", name, is_connect ? "YES" : "NO");
    } else {
        FURI_LOG_W(TAG, "Could not get device by name: %s", name);
    }

    radio_device_loader_power_off(broker, lease);
    return is_connect;
}

const SubGhzDevice* radio_device_loader_set(
    const SubGhzDevice* current_radio_device,
    SubGhzRadioDeviceType radio_device_type,
    SubGhzRadioBroker* broker,
    const SubGhzRadioBrokerLease* lease) {
    const SubGhzDevice* target_radio_device = NULL;

    // Decide the target device first (external if requested+present, else internal)
    if(radio_device_type == SubGhzRadioDeviceTypeExternalCC1101 &&
       radio_device_loader_is_connect_external(SUBGHZ_DEVICE_CC1101_EXT_NAME, broker, lease)) {
        radio_device_loader_power_on(broker, lease);
        target_radio_device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_EXT_NAME);
        if(!target_radio_device) {
            FURI_LOG_E(TAG, "Failed to get external CC1101 device, falling back to internal");
        }
    }

    if(!target_radio_device) {
        target_radio_device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
        if(!target_radio_device) {
            FURI_LOG_E(TAG, "Failed to get internal CC1101 device");
            return NULL;
        }
    }

    // If we’re already on the target device, don’t reload
    if(current_radio_device == target_radio_device) {
        if(target_radio_device == subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_EXT_NAME)) {
            FURI_LOG_I(TAG, "External CC1101 already selected");
        } else {
            FURI_LOG_I(TAG, "Internal CC1101 already selected");
        }
        return target_radio_device;
    }

    // Cleanly stop the current device before switching
    if(current_radio_device) {
        radio_device_loader_end(current_radio_device, broker, lease);
    }

    // Start the target device
    subghz_devices_begin(target_radio_device);

    // Log what we ended up with
    if(target_radio_device == subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_EXT_NAME)) {
        FURI_LOG_I(TAG, "Switched to external CC1101");
    } else {
        if(radio_device_type == SubGhzRadioDeviceTypeExternalCC1101) {
            FURI_LOG_I(TAG, "External requested but unavailable; switched to internal CC1101");
        } else {
            FURI_LOG_I(TAG, "Switched to internal CC1101");
        }
    }

    return target_radio_device;
}

bool radio_device_loader_is_external(const SubGhzDevice* radio_device) {
    if(!radio_device) {
        FURI_LOG_W(TAG, "is_external called with NULL device");
        return false;
    }

    const SubGhzDevice* internal_device =
        subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
    bool is_external = (radio_device != internal_device);

    FURI_LOG_D(
        TAG,
        "is_external check: device=%p, internal=%p, result=%s",
        radio_device,
        internal_device,
        is_external ? "EXTERNAL" : "INTERNAL");

    return is_external;
}

void radio_device_loader_end(
    const SubGhzDevice* radio_device,
    SubGhzRadioBroker* broker,
    const SubGhzRadioBrokerLease* lease) {
    furi_check(radio_device);

    if(radio_device != subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME)) {
        subghz_devices_end(radio_device);
        FURI_LOG_I(TAG, "External radio device ended");
    } else {
        FURI_LOG_D(TAG, "Internal radio device - no cleanup needed");
    }
    radio_device_loader_power_off(broker, lease);
}
