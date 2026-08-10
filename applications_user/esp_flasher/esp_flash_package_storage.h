#pragma once

#include "esp_flash_package_plan.h"

#include <storage/storage.h>

#define ESP_FLASH_PACKAGE_ROOT EXT_PATH("apps_data/esp_flasher")
#define ESP_FLASH_PACKAGE_MAX_DIRECTORIES 16U
#define ESP_FLASH_PACKAGE_DIRECTORY_MAX 96U
#define ESP_FLASH_PACKAGE_PATH_MAX 256U

size_t esp_flash_package_list_directories(
    Storage* storage,
    char directories[][ESP_FLASH_PACKAGE_DIRECTORY_MAX],
    size_t capacity);

bool esp_flash_package_load_verified(
    Storage* storage,
    const char* directory_name,
    EspFlashPackagePlan* plan,
    char* package_path,
    size_t package_path_size,
    char* error,
    size_t error_size);
