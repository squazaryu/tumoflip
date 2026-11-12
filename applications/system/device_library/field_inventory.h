#pragma once
#include "field_io.h"

typedef enum {
    FieldInventoryUnchanged,
    FieldInventoryChanged,
    FieldInventoryMissing,
    FieldInventoryUnreadable,
    FieldInventoryAdded,
} FieldInventoryState;
typedef struct {
    uint32_t unchanged, changed, missing, unreadable, added;
} FieldInventoryStats;
typedef void (*FieldInventoryVisit)(void* context, const char* path, FieldInventoryState state);
bool field_inventory_create(
    Storage* storage,
    const char* path,
    FieldProgress progress,
    void* context);
bool field_inventory_compare(
    Storage* storage,
    const char* path,
    FieldInventoryStats* stats,
    FieldInventoryVisit visit,
    FieldProgress progress,
    void* context);
