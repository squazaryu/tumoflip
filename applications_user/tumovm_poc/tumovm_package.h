#pragma once

#include "tumovm_core.h"

#include <storage/storage.h>

#define TUMOVM_DATA_DIR     "/ext/apps_data/tumovm_poc"
#define TUMOVM_PACKAGE_DIR  TUMOVM_DATA_DIR "/packages/shared_object"
#define TUMOVM_PROGRAM_PATH TUMOVM_PACKAGE_DIR "/program.tvm"
#define TUMOVM_STATE_PATH   TUMOVM_PACKAGE_DIR "/state.tvs"

typedef enum {
    TumoVmPackageResultOk,
    TumoVmPackageResultCreated,
    TumoVmPackageResultIo,
    TumoVmPackageResultInvalidProgram,
    TumoVmPackageResultInvalidState,
} TumoVmPackageResult;

TumoVmPackageResult
    tumovm_package_load(Storage* storage, TumoVmProgram* program, uint8_t state[TUMOVM_STATE_MAX]);

bool tumovm_package_save_state(
    Storage* storage,
    const TumoVmProgram* program,
    const uint8_t state[TUMOVM_STATE_MAX]);

const char* tumovm_package_result_name(TumoVmPackageResult result);
