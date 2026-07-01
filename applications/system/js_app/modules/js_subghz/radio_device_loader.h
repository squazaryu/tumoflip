#pragma once

#include <lib/subghz/devices/devices.h>
#include <subghz_radio_broker/subghz_radio_broker.h>

/** SubGhzRadioDeviceType */
typedef enum {
    SubGhzRadioDeviceTypeInternal,
    SubGhzRadioDeviceTypeExternalCC1101,
} SubGhzRadioDeviceType;

const SubGhzDevice* radio_device_loader_set(
    const SubGhzDevice* current_radio_device,
    SubGhzRadioDeviceType radio_device_type,
    SubGhzRadioBroker* broker,
    const SubGhzRadioBrokerLease* lease);

void radio_device_loader_end(
    const SubGhzDevice* radio_device,
    SubGhzRadioBroker* broker,
    const SubGhzRadioBrokerLease* lease);

bool radio_device_loader_is_external(const SubGhzDevice* radio_device);
