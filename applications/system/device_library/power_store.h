#pragma once

#include "power_history.h"
#include <storage/storage.h>

typedef enum {
    PowerStoreEmpty,
    PowerStoreOk,
    PowerStoreError,
} PowerStoreStatus;

PowerStoreStatus power_store_load(Storage* storage, PowerHistory* history);
bool power_store_save(Storage* storage, const PowerHistory* history);
bool power_store_export(Storage* storage, const PowerHistory* history, char* path, size_t capacity);
