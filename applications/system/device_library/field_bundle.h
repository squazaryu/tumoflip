#pragma once
#include "field_io.h"

bool field_bundle_export(
    Storage* storage,
    const DeviceCard* card,
    const char* path,
    FieldProgress progress,
    void* context);
bool field_bundle_inspect(
    Storage* storage,
    const char* path,
    DeviceCard* card,
    FieldProgress progress,
    void* context);
// A receipt binds an explicit UI preview/confirmation to this exact manifest
// and every payload digest, not merely to the filename or displayed card name.
bool field_bundle_inspect_receipt(
    Storage* storage,
    const char* path,
    DeviceCard* card,
    uint8_t receipt[32],
    FieldProgress progress,
    void* context);
bool field_bundle_import(
    Storage* storage,
    const char* path,
    const char* new_name,
    FieldProgress progress,
    void* context);
bool field_bundle_import_receipt(
    Storage* storage,
    const char* path,
    const char* new_name,
    const uint8_t receipt[32],
    FieldProgress progress,
    void* context);
