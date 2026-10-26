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
bool field_bundle_import(
    Storage* storage,
    const char* path,
    const char* new_name,
    FieldProgress progress,
    void* context);
