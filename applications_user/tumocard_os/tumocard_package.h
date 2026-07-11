#pragma once

#include "tumocard_router.h"

#include <storage/storage.h>

#define TUMOCARD_DATA_DIR    "/ext/apps_data/tumocard_os"
#define TUMOCARD_APPLETS_DIR TUMOCARD_DATA_DIR "/applets"
#define TUMOCARD_PATH_MAX    128U

typedef enum {
    TumoCardPackageResultOk,
    TumoCardPackageResultCreated,
    TumoCardPackageResultPartial,
    TumoCardPackageResultIo,
    TumoCardPackageResultNoValidApplet,
} TumoCardPackageResult;

typedef struct {
    char directories[TUMOCARD_APPLET_MAX][TUMOCARD_PATH_MAX];
} TumoCardPackageStore;

TumoCardPackageResult tumocard_package_load(
    Storage* storage,
    TumoCardRegistry* registry,
    TumoCardPackageStore* store);
bool tumocard_package_save_state(
    Storage* storage,
    const TumoCardPackageStore* store,
    size_t index,
    const TumoCardApplet* applet);
bool tumocard_package_save_enabled(
    Storage* storage,
    const TumoCardPackageStore* store,
    size_t index,
    bool enabled);
const char* tumocard_package_result_name(TumoCardPackageResult result);
