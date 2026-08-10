#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ESP_FLASH_PACKAGE_MANIFEST "tumoflip-flash-package.json"
#define ESP_FLASH_PACKAGE_KIND "tumoflip-esp32-flash-package"
#define ESP_FLASH_PACKAGE_MAX_SEGMENTS 4U
#define ESP_FLASH_PACKAGE_NAME_MAX 96U
#define ESP_FLASH_PACKAGE_TEXT_MAX 48U

typedef enum {
    EspFlashPackageTargetEsp32,
    EspFlashPackageTargetEsp32C5,
} EspFlashPackageTarget;

typedef enum {
    EspFlashPackageRoleBootloader,
    EspFlashPackageRolePartitionTable,
    EspFlashPackageRoleOtaData,
    EspFlashPackageRoleApplication,
} EspFlashPackageRole;

typedef struct {
    EspFlashPackageRole role;
    char file_name[ESP_FLASH_PACKAGE_NAME_MAX];
    uint32_t offset;
    uint32_t size;
    uint8_t sha256[32];
    uint8_t md5[16];
} EspFlashPackageSegment;

typedef struct {
    char board_key[ESP_FLASH_PACKAGE_TEXT_MAX];
    char model_id[ESP_FLASH_PACKAGE_TEXT_MAX];
    char display_name[ESP_FLASH_PACKAGE_TEXT_MAX];
    char chip_family[ESP_FLASH_PACKAGE_TEXT_MAX];
    char firmware_version[ESP_FLASH_PACKAGE_TEXT_MAX];
    char recipe_id[ESP_FLASH_PACKAGE_TEXT_MAX];
    char recipe_status[ESP_FLASH_PACKAGE_TEXT_MAX];
    EspFlashPackageTarget target;
    EspFlashPackageSegment segments[ESP_FLASH_PACKAGE_MAX_SEGMENTS];
    size_t segment_count;
    uint32_t total_size;
} EspFlashPackagePlan;

bool esp_flash_package_parse_manifest(
    const char* json,
    size_t json_size,
    EspFlashPackagePlan* plan,
    char* error,
    size_t error_size);

bool esp_flash_package_directory_name_allowed(const char* name);
bool esp_flash_package_validate_bin_names(
    const EspFlashPackagePlan* plan,
    const char* const* names,
    size_t name_count);
const char* esp_flash_package_role_name(EspFlashPackageRole role);
