#include "radio_device_loader.h"

#include <applications/drivers/subghz/cc1101_ext/cc1101_ext_interconnect.h>
#include <lib/subghz/devices/cc1101_int/cc1101_int_interconnect.h>

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

static bool radio_device_loader_is_connect_external(
    const char* name,
    SubGhzRadioBroker* broker,
    const SubGhzRadioBrokerLease* lease) {
    bool is_connect = false;
    const bool powered = radio_device_loader_power_on(broker, lease);

    const SubGhzDevice* device = powered ? subghz_devices_get_by_name(name) : NULL;
    if(device) {
        is_connect = subghz_devices_is_connect(device);
    }

    radio_device_loader_power_off(broker, lease);
    return is_connect;
}

const SubGhzDevice* radio_device_loader_set(
    const SubGhzDevice* current_radio_device,
    SubGhzRadioDeviceType radio_device_type,
    SubGhzRadioBroker* broker,
    const SubGhzRadioBrokerLease* lease) {
    const SubGhzDevice* radio_device;

    if(radio_device_type == SubGhzRadioDeviceTypeExternalCC1101 &&
       radio_device_loader_is_connect_external(SUBGHZ_DEVICE_CC1101_EXT_NAME, broker, lease)) {
        radio_device_loader_power_on(broker, lease);
        radio_device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_EXT_NAME);
        subghz_devices_begin(radio_device);
    } else if(current_radio_device == NULL) {
        radio_device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
    } else {
        radio_device_loader_end(current_radio_device, broker, lease);
        radio_device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
    }

    return radio_device;
}

void radio_device_loader_end(
    const SubGhzDevice* radio_device,
    SubGhzRadioBroker* broker,
    const SubGhzRadioBrokerLease* lease) {
    furi_assert(radio_device);
    if(radio_device != subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME)) {
        subghz_devices_end(radio_device);
    }
    radio_device_loader_power_off(broker, lease);
}

bool radio_device_loader_is_external(const SubGhzDevice* radio_device) {
    return radio_device == subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_EXT_NAME);
}
