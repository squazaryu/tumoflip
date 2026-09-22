#pragma once
#include "library_model.h"
#include <storage/storage.h>
#define LIBRARY_ROOT  EXT_PATH("apps_data/device_library")
#define LIBRARY_CARDS LIBRARY_ROOT "/cards"
bool library_card_load(Storage* storage, const char* name, DeviceCard* card);
bool library_card_save(Storage* storage, const DeviceCard* card);
