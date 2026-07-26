#pragma once

#include "tumotag_compare.h"

#include <storage/storage.h>

#define TUMOTAG_DATA_DIR          EXT_PATH("apps_data/tumotag_verify")
#define TUMOTAG_REPORTS_DIR       TUMOTAG_DATA_DIR "/reports"
#define TUMOTAG_NFC_OBSERVED_PATH TUMOTAG_DATA_DIR "/observed.nfc"
#define TUMOTAG_NFC_LAUNCH_ARG    "tumotag_verify_capture"

typedef enum {
    TumoTagOperationVerify,
    TumoTagOperationReadBack,
    TumoTagOperationFind,
} TumoTagOperation;

typedef struct {
    TumoTagOperation operation;
    TumoTagMedium medium;
    char expected_path[256];
} TumoTagRoute;

bool tumotag_storage_prepare(Storage* storage);
bool tumotag_route_prepare(Storage* storage, const TumoTagRoute* route);
bool tumotag_route_resume(Storage* storage, TumoTagRoute* route);
void tumotag_route_cancel(Storage* storage);

bool tumotag_report_save(
    Storage* storage,
    TumoTagOperation operation,
    const TumoTagResult* result,
    char* report_path,
    size_t report_path_size);
bool tumotag_report_load_last(Storage* storage, TumoTagOperation* operation, TumoTagResult* result);

const char* tumotag_operation_name(TumoTagOperation operation);
const char* tumotag_medium_root(TumoTagMedium medium);
const char* tumotag_medium_extension(TumoTagMedium medium);
