#pragma once

#include "tumovm_peripheral_core.h"

#include <storage/storage.h>

#define TUMOVM_PERIPHERAL_DATA_DIR    "/ext/apps_data/tumovm_peripherals"
#define TUMOVM_PERIPHERAL_PACKAGE_DIR TUMOVM_PERIPHERAL_DATA_DIR "/packages"
#define TUMOVM_PERIPHERAL_STATE_DIR   TUMOVM_PERIPHERAL_DATA_DIR "/state"

typedef struct {
    char filename[64];
    TumoVmPeripheralManifest manifest;
    TumoVmPeripheralManifestStatus status;
} TumoVmPeripheralPackage;

size_t tumovm_peripheral_packages_load(
    Storage* storage,
    uint32_t current_api,
    TumoVmPeripheralPackage* packages,
    size_t capacity);

bool tumovm_peripheral_state_load(
    Storage* storage,
    const TumoVmPeripheralManifest* manifest,
    uint8_t state[TumoVmPeripheralStateMaximum]);
bool tumovm_peripheral_state_save(
    Storage* storage,
    const TumoVmPeripheralManifest* manifest,
    const uint8_t state[TumoVmPeripheralStateMaximum]);
