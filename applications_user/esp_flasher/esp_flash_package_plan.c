#include "esp_flash_package_plan.h"

#include <frozen.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool package_error(char* error, size_t error_size, const char* message) {
    if(error && error_size) snprintf(error, error_size, "%s", message);
    return false;
}

static bool package_copy(
    char* destination,
    size_t destination_size,
    const char* source,
    char* error,
    size_t error_size) {
    if(!source || !source[0] || strlen(source) >= destination_size) {
        return package_error(error, error_size, "Missing or oversized manifest text");
    }
    snprintf(destination, destination_size, "%s", source);
    return true;
}

static bool package_hex_decode(
    const char* text,
    uint8_t* output,
    size_t output_size,
    char* error,
    size_t error_size) {
    if(!text || strlen(text) != output_size * 2U) {
        return package_error(error, error_size, "Invalid digest length");
    }

    for(size_t i = 0; i < output_size; ++i) {
        if((text[i * 2U] >= 'A' && text[i * 2U] <= 'F') ||
           (text[i * 2U + 1U] >= 'A' && text[i * 2U + 1U] <= 'F')) {
            return package_error(error, error_size, "Digest must be lowercase hex");
        }
        const int high = isdigit((unsigned char)text[i * 2U]) ? text[i * 2U] - '0' :
                         isxdigit((unsigned char)text[i * 2U]) ?
                             (tolower((unsigned char)text[i * 2U]) - 'a' + 10) :
                             -1;
        const int low = isdigit((unsigned char)text[i * 2U + 1U]) ? text[i * 2U + 1U] - '0' :
                        isxdigit((unsigned char)text[i * 2U + 1U]) ?
                            (tolower((unsigned char)text[i * 2U + 1U]) - 'a' + 10) :
                            -1;
        if(high < 0 || low < 0) {
            return package_error(error, error_size, "Digest is not hexadecimal");
        }
        output[i] = (uint8_t)((high << 4) | low);
    }
    return true;
}

static bool package_file_name_allowed(const char* name) {
    if(!name || !name[0] || name[0] == '.' || strlen(name) >= ESP_FLASH_PACKAGE_NAME_MAX) {
        return false;
    }
    const size_t length = strlen(name);
    if(length < 5U || strcmp(name + length - 4U, ".bin") != 0) return false;

    for(const char* cursor = name; *cursor; ++cursor) {
        if(!(isalnum((unsigned char)*cursor) || *cursor == '.' || *cursor == '_' ||
             *cursor == '-')) {
            return false;
        }
    }
    return strstr(name, "..") == NULL;
}

static bool package_identifier_allowed(const char* value) {
    if(!value || !value[0]) return false;
    for(const char* cursor = value; *cursor; ++cursor) {
        if(!(isalnum((unsigned char)*cursor) || *cursor == '.' || *cursor == '_' ||
             *cursor == '-')) {
            return false;
        }
    }
    return true;
}

static bool package_parse_u32_token(const struct json_token* token, uint32_t* output) {
    if(!token || !output || token->type != JSON_TYPE_NUMBER || token->len <= 0 ||
       token->len > 10) {
        return false;
    }

    uint64_t value = 0U;
    for(int i = 0; i < token->len; ++i) {
        const unsigned char character = (unsigned char)token->ptr[i];
        if(!isdigit(character)) return false;
        value = value * 10U + (uint64_t)(character - '0');
        if(value > UINT32_MAX) return false;
    }
    *output = (uint32_t)value;
    return true;
}

static bool package_iso8601_utc_allowed(const char* value) {
    if(!value) return false;
    const size_t length = strlen(value);
    if((length != 20U && length != 24U) || value[4] != '-' || value[7] != '-' ||
       value[10] != 'T' ||
       value[13] != ':' || value[16] != ':' || value[length - 1U] != 'Z') {
        return false;
    }
    for(size_t i = 0; i < length; ++i) {
        if(i == 4U || i == 7U || i == 10U || i == 13U || i == 16U || i == length - 1U) {
            continue;
        }
        if(i == 19U && length > 20U) {
            if(value[i] != '.') return false;
        } else if(!isdigit((unsigned char)value[i])) {
            return false;
        }
    }
    const unsigned month = (unsigned)(value[5] - '0') * 10U + (unsigned)(value[6] - '0');
    const unsigned day = (unsigned)(value[8] - '0') * 10U + (unsigned)(value[9] - '0');
    const unsigned hour = (unsigned)(value[11] - '0') * 10U + (unsigned)(value[12] - '0');
    const unsigned minute = (unsigned)(value[14] - '0') * 10U + (unsigned)(value[15] - '0');
    const unsigned second = (unsigned)(value[17] - '0') * 10U + (unsigned)(value[18] - '0');
    const unsigned year = (unsigned)(value[0] - '0') * 1000U +
                          (unsigned)(value[1] - '0') * 100U +
                          (unsigned)(value[2] - '0') * 10U + (unsigned)(value[3] - '0');
    static const uint8_t days_per_month[] = {31U, 28U, 31U, 30U, 31U, 30U,
                                             31U, 31U, 30U, 31U, 30U, 31U};
    if(month < 1U || month > 12U || hour > 23U || minute > 59U || second > 59U) {
        return false;
    }
    unsigned maximum_day = days_per_month[month - 1U];
    const bool leap_year = (year % 4U == 0U) && ((year % 100U != 0U) || (year % 400U == 0U));
    if(month == 2U && leap_year) maximum_day++;
    return day >= 1U && day <= maximum_day;
}

bool esp_flash_package_directory_name_allowed(const char* name) {
    if(!name || !name[0] || name[0] == '.' || name[0] == '_' || strchr(name, '/') ||
       strchr(name, '\\')) {
        return false;
    }
    const size_t length = strlen(name);
    if(length < 8U || strcmp(name + length - 7U, "_manual") != 0) return false;

    for(const char* cursor = name; *cursor; ++cursor) {
        if(!(isalnum((unsigned char)*cursor) || *cursor == '.' || *cursor == '_' ||
             *cursor == '-')) {
            return false;
        }
    }

    return strstr(name, ".partial") == NULL && strstr(name, ".staging") == NULL &&
           strstr(name, ".replacement") == NULL && strstr(name, ".backup") == NULL &&
           strstr(name, ".bak") == NULL && strstr(name, "..") == NULL;
}

bool esp_flash_package_validate_bin_names(
    const EspFlashPackagePlan* plan,
    const char* const* names,
    size_t name_count) {
    if(!plan || !names || name_count != plan->segment_count ||
       name_count > ESP_FLASH_PACKAGE_MAX_SEGMENTS) {
        return false;
    }
    bool seen[ESP_FLASH_PACKAGE_MAX_SEGMENTS] = {false};
    for(size_t name_index = 0; name_index < name_count; ++name_index) {
        bool matched = false;
        for(size_t segment_index = 0; segment_index < plan->segment_count; ++segment_index) {
            if(strcmp(names[name_index], plan->segments[segment_index].file_name) == 0) {
                if(seen[segment_index]) return false;
                seen[segment_index] = true;
                matched = true;
                break;
            }
        }
        if(!matched) return false;
    }
    return true;
}

const char* esp_flash_package_role_name(EspFlashPackageRole role) {
    switch(role) {
    case EspFlashPackageRoleBootloader:
        return "bootloader";
    case EspFlashPackageRolePartitionTable:
        return "partition-table";
    case EspFlashPackageRoleOtaData:
        return "ota-data";
    case EspFlashPackageRoleApplication:
        return "application";
    default:
        return "unknown";
    }
}

static bool package_parse_role(const char* text, EspFlashPackageRole* role) {
    if(strcmp(text, "bootloader") == 0) {
        *role = EspFlashPackageRoleBootloader;
    } else if(strcmp(text, "partition-table") == 0) {
        *role = EspFlashPackageRolePartitionTable;
    } else if(strcmp(text, "ota-data") == 0) {
        *role = EspFlashPackageRoleOtaData;
    } else if(strcmp(text, "application") == 0) {
        *role = EspFlashPackageRoleApplication;
    } else {
        return false;
    }
    return true;
}

static bool package_validate_profile(
    EspFlashPackagePlan* plan,
    char* error,
    size_t error_size) {
    const EspFlashPackageRole c5_roles[] = {
        EspFlashPackageRoleBootloader,
        EspFlashPackageRolePartitionTable,
        EspFlashPackageRoleApplication,
    };
    const uint32_t c5_offsets[] = {0x2000U, 0x8000U, 0x10000U};
    const EspFlashPackageRole v61_roles[] = {
        EspFlashPackageRoleBootloader,
        EspFlashPackageRolePartitionTable,
        EspFlashPackageRoleOtaData,
        EspFlashPackageRoleApplication,
    };
    const uint32_t v61_offsets[] = {0x1000U, 0x8000U, 0xE000U, 0x10000U};

    const EspFlashPackageRole* roles = NULL;
    const uint32_t* offsets = NULL;
    size_t count = 0;

    if(strcmp(plan->board_key, "esp32c5devkitc1") == 0 &&
       strcmp(plan->model_id, "esp32-c5-devkitc-1") == 0 &&
       strcmp(plan->chip_family, "esp32c5") == 0 &&
       strcmp(plan->recipe_id, "c5-compat-v1") == 0 &&
       strcmp(plan->recipe_status, "hardware-accepted") == 0) {
        plan->target = EspFlashPackageTargetEsp32C5;
        roles = c5_roles;
        offsets = c5_offsets;
        count = 3U;
    } else if(
        strcmp(plan->board_key, "v6_1") == 0 &&
        strcmp(plan->model_id, "marauder-v6-1") == 0 &&
        strcmp(plan->chip_family, "esp32") == 0 &&
        strcmp(plan->recipe_id, "upstream-factory-v1") == 0 &&
        strcmp(plan->recipe_status, "authoritative") == 0) {
        plan->target = EspFlashPackageTargetEsp32;
        roles = v61_roles;
        offsets = v61_offsets;
        count = 4U;
    } else {
        return package_error(error, error_size, "Unknown board or flash recipe");
    }

    if(plan->segment_count != count) {
        return package_error(error, error_size, "Incomplete board recipe");
    }
    for(size_t i = 0; i < count; ++i) {
        if(plan->segments[i].role != roles[i] || plan->segments[i].offset != offsets[i]) {
            return package_error(error, error_size, "Wrong segment role or offset order");
        }
    }
    if(plan->target == EspFlashPackageTargetEsp32C5) {
        static const uint8_t accepted_c5_bootloader_sha256[32] = {
            0x3e, 0x2b, 0x92, 0xa7, 0x4c, 0xf4, 0x06, 0x74, 0x5d, 0xdd, 0xc8,
            0x8e, 0xcb, 0x51, 0x93, 0xfd, 0x44, 0x6f, 0x4b, 0x26, 0x9b, 0x96,
            0xd2, 0xb9, 0x99, 0x1d, 0x84, 0xf4, 0x1c, 0x81, 0x06, 0x11,
        };
        if(strcmp(plan->segments[0].file_name, "bootloader_0x2000.bin") != 0 ||
           plan->segments[0].size != 20464U ||
           memcmp(
               plan->segments[0].sha256,
               accepted_c5_bootloader_sha256,
               sizeof(accepted_c5_bootloader_sha256)) != 0) {
            return package_error(error, error_size, "Unaccepted C5 bootloader");
        }
    }
    return true;
}

bool esp_flash_package_parse_manifest(
    const char* json,
    size_t json_size,
    EspFlashPackagePlan* plan,
    char* error,
    size_t error_size) {
    if(!json || !json_size || !plan || json_size > 16384U) {
        return package_error(error, error_size, "Manifest is empty or too large");
    }
    if(error && error_size) error[0] = '\0';
    memset(plan, 0, sizeof(*plan));

    int schema_version = 0;
    char* package_kind = NULL;
    char* board_key = NULL;
    char* model_id = NULL;
    char* display_name = NULL;
    char* chip_family = NULL;
    char* firmware_version = NULL;
    char* source_repository = NULL;
    char* source_release = NULL;
    char* recipe_id = NULL;
    char* recipe_status = NULL;
    char* erase_policy = NULL;
    char* created_at = NULL;
    char* created_by_application = NULL;
    char* created_by_version = NULL;
    struct json_token segments_token = JSON_INVALID_TOKEN;

    const int scanned = json_scanf(
        json,
        (int)json_size,
        "{schemaVersion:%d,packageKind:%Q,board:{key:%Q,modelId:%Q,displayName:%Q,chipFamily:%Q},firmware:{version:%Q,sourceRepository:%Q,sourceRelease:%Q},recipe:{id:%Q,status:%Q},erasePolicy:%Q,segments:%T,createdAt:%Q,createdBy:{application:%Q,version:%Q}}",
        &schema_version,
        &package_kind,
        &board_key,
        &model_id,
        &display_name,
        &chip_family,
        &firmware_version,
        &source_repository,
        &source_release,
        &recipe_id,
        &recipe_status,
        &erase_policy,
        &segments_token,
        &created_at,
        &created_by_application,
        &created_by_version);

    bool valid = true;
    if(scanned != 16) {
        valid = false;
        if(error && error_size) snprintf(error, error_size, "Manifest fields: %d/16", scanned);
    } else if(schema_version != 1 || !package_kind ||
              strcmp(package_kind, ESP_FLASH_PACKAGE_KIND) != 0) {
        valid = package_error(error, error_size, "Unsupported manifest kind or schema");
    } else if(!erase_policy || strcmp(erase_policy, "segments") != 0) {
        valid = package_error(error, error_size, "Unsupported erase policy");
    } else if(segments_token.type != JSON_TYPE_ARRAY_END) {
        valid = package_error(error, error_size, "Segments must be an array");
    } else if(!source_repository ||
              strcmp(source_repository, "justcallmekoko/ESP32Marauder") != 0 ||
              !package_identifier_allowed(firmware_version) || !source_release) {
        valid = package_error(error, error_size, "Untrusted package source");
    } else {
        char expected_release[160];
        const int written = snprintf(
            expected_release,
            sizeof(expected_release),
            "https://github.com/justcallmekoko/ESP32Marauder/releases/tag/%s",
            firmware_version);
        if(written <= 0 || (size_t)written >= sizeof(expected_release) ||
           strcmp(source_release, expected_release) != 0) {
            valid = package_error(error, error_size, "Untrusted package source");
        }
    }
    if(valid &&
       (!package_iso8601_utc_allowed(created_at) || !created_by_application ||
              strcmp(created_by_application, "TumoCompanion") != 0 || !created_by_version ||
              !package_identifier_allowed(created_by_version))) {
        valid = package_error(error, error_size, "Invalid package provenance");
    }

    if(valid) {
        valid = package_copy(
                    plan->board_key,
                    sizeof(plan->board_key),
                    board_key,
                    error,
                    error_size) &&
                package_copy(
                    plan->model_id,
                    sizeof(plan->model_id),
                    model_id,
                    error,
                    error_size) &&
                package_copy(
                    plan->display_name,
                    sizeof(plan->display_name),
                    display_name,
                    error,
                    error_size) &&
                package_copy(
                    plan->chip_family,
                    sizeof(plan->chip_family),
                    chip_family,
                    error,
                    error_size) &&
                package_copy(
                    plan->firmware_version,
                    sizeof(plan->firmware_version),
                    firmware_version,
                    error,
                    error_size) &&
                package_copy(
                    plan->recipe_id,
                    sizeof(plan->recipe_id),
                    recipe_id,
                    error,
                    error_size) &&
                package_copy(
                    plan->recipe_status,
                    sizeof(plan->recipe_status),
                    recipe_status,
                    error,
                    error_size);
    }

    for(size_t i = 0; valid && i <= ESP_FLASH_PACKAGE_MAX_SEGMENTS; ++i) {
        struct json_token token = JSON_INVALID_TOKEN;
        const int found =
            json_scanf_array_elem(json, (int)json_size, ".segments", (int)i, &token);
        if(found < 0) break;
        if(i == ESP_FLASH_PACKAGE_MAX_SEGMENTS || token.type != JSON_TYPE_OBJECT_END) {
            valid = package_error(error, error_size, "Too many or invalid segments");
            break;
        }

        char* role_text = NULL;
        char* file_name = NULL;
        char* sha256_text = NULL;
        char* md5_text = NULL;
        struct json_token offset_token = JSON_INVALID_TOKEN;
        struct json_token size_token = JSON_INVALID_TOKEN;
        const int segment_scanned = json_scanf(
            token.ptr,
            token.len,
            "{role:%Q,fileName:%Q,offset:%T,size:%T,sha256:%Q,md5:%Q}",
            &role_text,
            &file_name,
            &offset_token,
            &size_token,
            &sha256_text,
            &md5_text);

        EspFlashPackageSegment* segment = &plan->segments[i];
        uint32_t offset = 0U;
        uint32_t size = 0U;
        valid = segment_scanned == 6 && role_text && file_name && sha256_text && md5_text &&
                package_parse_u32_token(&offset_token, &offset) &&
                package_parse_u32_token(&size_token, &size) && size > 0U &&
                package_parse_role(role_text, &segment->role) &&
                package_file_name_allowed(file_name) &&
                package_copy(
                    segment->file_name,
                    sizeof(segment->file_name),
                    file_name,
                    error,
                    error_size) &&
                package_hex_decode(
                    sha256_text, segment->sha256, sizeof(segment->sha256), error, error_size) &&
                package_hex_decode(md5_text, segment->md5, sizeof(segment->md5), error, error_size);
        if(valid) {
            segment->offset = offset;
            segment->size = size;
            if(UINT32_MAX - plan->total_size < segment->size) {
                valid = package_error(error, error_size, "Package byte count overflow");
            } else {
                plan->total_size += segment->size;
                plan->segment_count++;
            }
        } else if(error && error_size && !error[0]) {
            package_error(error, error_size, "Invalid segment entry");
        }

        free(role_text);
        free(file_name);
        free(sha256_text);
        free(md5_text);
    }

    for(size_t i = 0; valid && i < plan->segment_count; ++i) {
        if(UINT32_MAX - plan->segments[i].offset < plan->segments[i].size) {
            valid = package_error(error, error_size, "Segment address overflow");
            break;
        }
        for(size_t j = i + 1U; j < plan->segment_count; ++j) {
            if(plan->segments[i].role == plan->segments[j].role ||
               plan->segments[i].offset == plan->segments[j].offset ||
               strcmp(plan->segments[i].file_name, plan->segments[j].file_name) == 0) {
                valid = package_error(error, error_size, "Duplicate segment role, file or offset");
            }
        }
        if(valid && i + 1U < plan->segment_count &&
           plan->segments[i].offset + plan->segments[i].size > plan->segments[i + 1U].offset) {
            valid = package_error(error, error_size, "Overlapping flash segments");
        }
    }
    if(valid) valid = package_validate_profile(plan, error, error_size);

    free(package_kind);
    free(board_key);
    free(model_id);
    free(display_name);
    free(chip_family);
    free(firmware_version);
    free(source_repository);
    free(source_release);
    free(recipe_id);
    free(recipe_status);
    free(erase_policy);
    free(created_at);
    free(created_by_application);
    free(created_by_version);
    return valid;
}
