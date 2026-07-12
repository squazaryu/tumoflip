#pragma once

#include <storage/storage.h>

#include "tumomodule_core.h"

#define TUMOMODULE_PACKAGE_DIR EXT_PATH("apps_data/tumomodule_runtime/modules")

typedef struct {
    TumoModuleManifest manifest;
    TumoModuleManifestStatus status;
    char filename[64];
} TumoModulePackage;

size_t tumomodule_packages_load(
    Storage* storage,
    uint32_t current_api,
    TumoModulePackage* packages,
    size_t capacity);
