#pragma once
#include "capture_model.h"
#include <storage/storage.h>

typedef bool (*CiCancelCallback)(void* context);
typedef enum {
    CiLoadOk,
    CiLoadIoError,
    CiLoadBadPath,
    CiLoadParseError,
    CiLoadCancelled
} CiLoadStatus;

bool ci_capture_path_valid(const char* path);
CiLoadStatus ci_capture_load(
    Storage* storage,
    const char* path,
    CiSnapshot* snapshot,
    CiCancelCallback cancel,
    void* context);
