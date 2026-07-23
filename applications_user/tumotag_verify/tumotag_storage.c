#include "tumotag_storage.h"

#include <flipper_format/flipper_format.h>
#include <furi_hal_rtc.h>

#include <stdio.h>
#include <string.h>

#define TUMOTAG_ROUTE_PATH       TUMOTAG_DATA_DIR "/route.ff"
#define TUMOTAG_ROUTE_TEMP_PATH  TUMOTAG_DATA_DIR "/route.tmp"
#define TUMOTAG_LAST_RESULT_PATH TUMOTAG_DATA_DIR "/last_result.ff"
#define TUMOTAG_LAST_TEMP_PATH   TUMOTAG_DATA_DIR "/last_result.tmp"
#define TUMOTAG_ROUTE_FILETYPE   "TumoTag Verify Route"
#define TUMOTAG_RESULT_FILETYPE  "TumoTag Verify Result"
#define TUMOTAG_FORMAT_VERSION   1U

static bool tumotag_mkdir(Storage* storage, const char* path) {
    const FS_Error result = storage_common_mkdir(storage, path);
    return result == FSE_OK || result == FSE_EXIST;
}

bool tumotag_storage_prepare(Storage* storage) {
    furi_check(storage);
    return tumotag_mkdir(storage, EXT_PATH("apps_data")) &&
           tumotag_mkdir(storage, TUMOTAG_DATA_DIR) && tumotag_mkdir(storage, TUMOTAG_REPORTS_DIR);
}

const char* tumotag_operation_name(TumoTagOperation operation) {
    switch(operation) {
    case TumoTagOperationVerify:
        return "Verify saved";
    case TumoTagOperationReadBack:
        return "Verify write";
    case TumoTagOperationFind:
        return "Find token";
    default:
        return "Unknown";
    }
}

const char* tumotag_medium_root(TumoTagMedium medium) {
    switch(medium) {
    case TumoTagMediumNfc:
        return EXT_PATH("nfc");
    case TumoTagMediumLfRfid:
        return EXT_PATH("lfrfid");
    case TumoTagMediumIButton:
        return EXT_PATH("ibutton");
    default:
        return EXT_PATH("");
    }
}

const char* tumotag_medium_extension(TumoTagMedium medium) {
    switch(medium) {
    case TumoTagMediumNfc:
        return ".nfc";
    case TumoTagMediumLfRfid:
        return ".rfid";
    case TumoTagMediumIButton:
        return ".ibtn";
    default:
        return "";
    }
}

static bool tumotag_replace_file(Storage* storage, const char* temporary, const char* path) {
    if(storage_file_exists(storage, path)) storage_common_remove(storage, path);
    if(storage_common_rename(storage, temporary, path) != FSE_OK) {
        storage_common_remove(storage, temporary);
        return false;
    }
    return true;
}

bool tumotag_route_prepare(Storage* storage, const TumoTagRoute* route) {
    furi_check(storage);
    furi_check(route);
    if(route->operation > TumoTagOperationFind || route->medium > TumoTagMediumIButton ||
       (route->operation != TumoTagOperationFind && route->expected_path[0] == '\0') ||
       !tumotag_storage_prepare(storage)) {
        return false;
    }

    storage_common_remove(storage, TUMOTAG_ROUTE_TEMP_PATH);
    FlipperFormat* format = flipper_format_file_alloc(storage);
    const uint32_t operation = route->operation;
    const uint32_t medium = route->medium;
    bool success = false;
    do {
        if(!flipper_format_file_open_new(format, TUMOTAG_ROUTE_TEMP_PATH) ||
           !flipper_format_write_header_cstr(
               format, TUMOTAG_ROUTE_FILETYPE, TUMOTAG_FORMAT_VERSION) ||
           !flipper_format_write_uint32(format, "Operation", &operation, 1U) ||
           !flipper_format_write_uint32(format, "Medium", &medium, 1U) ||
           !flipper_format_write_string_cstr(format, "Expected path", route->expected_path)) {
            break;
        }
        success = true;
    } while(false);
    flipper_format_free(format);

    return success && tumotag_replace_file(storage, TUMOTAG_ROUTE_TEMP_PATH, TUMOTAG_ROUTE_PATH);
}

bool tumotag_route_resume(Storage* storage, TumoTagRoute* route) {
    furi_check(storage);
    furi_check(route);
    memset(route, 0, sizeof(*route));
    if(!storage_file_exists(storage, TUMOTAG_ROUTE_PATH)) return false;

    FlipperFormat* format = flipper_format_file_alloc(storage);
    FuriString* header = furi_string_alloc();
    FuriString* path = furi_string_alloc();
    uint32_t version = 0U;
    uint32_t operation = UINT32_MAX;
    uint32_t medium = UINT32_MAX;
    bool success = false;
    do {
        if(!flipper_format_file_open_existing(format, TUMOTAG_ROUTE_PATH) ||
           !flipper_format_read_header(format, header, &version) ||
           version != TUMOTAG_FORMAT_VERSION ||
           furi_string_cmp_str(header, TUMOTAG_ROUTE_FILETYPE) != 0 ||
           !flipper_format_read_uint32(format, "Operation", &operation, 1U) ||
           !flipper_format_read_uint32(format, "Medium", &medium, 1U) ||
           !flipper_format_read_string(format, "Expected path", path) ||
           operation > TumoTagOperationFind || medium > TumoTagMediumIButton ||
           furi_string_size(path) >= sizeof(route->expected_path)) {
            break;
        }
        route->operation = operation;
        route->medium = medium;
        snprintf(
            route->expected_path, sizeof(route->expected_path), "%s", furi_string_get_cstr(path));
        success = true;
    } while(false);

    furi_string_free(path);
    furi_string_free(header);
    flipper_format_free(format);
    storage_common_remove(storage, TUMOTAG_ROUTE_PATH);
    return success;
}

void tumotag_route_cancel(Storage* storage) {
    furi_check(storage);
    storage_common_remove(storage, TUMOTAG_ROUTE_PATH);
    storage_common_remove(storage, TUMOTAG_ROUTE_TEMP_PATH);
    storage_common_remove(storage, TUMOTAG_NFC_OBSERVED_PATH);
}

static bool tumotag_result_write(
    Storage* storage,
    const char* path,
    TumoTagOperation operation,
    const TumoTagResult* result) {
    FlipperFormat* format = flipper_format_file_alloc(storage);
    const uint32_t operation_value = operation;
    const uint32_t medium = result->medium;
    const uint32_t verdict = result->verdict;
    const uint32_t compared = result->compared_units;
    const uint32_t missing = result->missing_units;
    const uint32_t mismatched = result->mismatched_units;
    const uint32_t duplicates = result->duplicate_matches;
    const uint32_t truncated = result->search_truncated ? 1U : 0U;
    bool success = false;
    do {
        if(!flipper_format_file_open_new(format, path) ||
           !flipper_format_write_header_cstr(
               format, TUMOTAG_RESULT_FILETYPE, TUMOTAG_FORMAT_VERSION) ||
           !flipper_format_write_uint32(format, "Operation", &operation_value, 1U) ||
           !flipper_format_write_uint32(format, "Medium", &medium, 1U) ||
           !flipper_format_write_uint32(format, "Verdict", &verdict, 1U) ||
           !flipper_format_write_string_cstr(
               format, "Expected protocol", result->expected_protocol) ||
           !flipper_format_write_string_cstr(
               format, "Observed protocol", result->observed_protocol) ||
           !flipper_format_write_string_cstr(format, "Expected ID", result->expected_id) ||
           !flipper_format_write_string_cstr(format, "Observed ID", result->observed_id) ||
           !flipper_format_write_string_cstr(format, "Detail", result->detail) ||
           !flipper_format_write_string_cstr(format, "Expected path", result->expected_path) ||
           !flipper_format_write_string_cstr(format, "Match path", result->match_path) ||
           !flipper_format_write_uint32(format, "Compared units", &compared, 1U) ||
           !flipper_format_write_uint32(format, "Missing units", &missing, 1U) ||
           !flipper_format_write_uint32(format, "Mismatched units", &mismatched, 1U) ||
           !flipper_format_write_uint32(format, "Duplicate matches", &duplicates, 1U) ||
           !flipper_format_write_uint32(format, "Search truncated", &truncated, 1U)) {
            break;
        }
        success = true;
    } while(false);
    flipper_format_free(format);
    return success;
}

bool tumotag_report_save(
    Storage* storage,
    TumoTagOperation operation,
    const TumoTagResult* result,
    char* report_path,
    size_t report_path_size) {
    furi_check(storage);
    furi_check(result);
    if(!tumotag_storage_prepare(storage)) return false;

    storage_common_remove(storage, TUMOTAG_LAST_TEMP_PATH);
    if(!tumotag_result_write(storage, TUMOTAG_LAST_TEMP_PATH, operation, result) ||
       !tumotag_replace_file(storage, TUMOTAG_LAST_TEMP_PATH, TUMOTAG_LAST_RESULT_PATH)) {
        return false;
    }

    char path[256];
    const int length = snprintf(
        path,
        sizeof(path),
        TUMOTAG_REPORTS_DIR "/%lu-%lu.ff",
        (unsigned long)furi_hal_rtc_get_timestamp(),
        (unsigned long)furi_get_tick());
    if(length <= 0 || (size_t)length >= sizeof(path) ||
       !tumotag_result_write(storage, path, operation, result)) {
        return false;
    }
    if(report_path && report_path_size > 0U) {
        snprintf(report_path, report_path_size, "%s", path);
    }
    return true;
}

static bool tumotag_read_string(
    FlipperFormat* format,
    const char* key,
    char* destination,
    size_t destination_size,
    FuriString* buffer) {
    if(!flipper_format_read_string(format, key, buffer) ||
       furi_string_size(buffer) >= destination_size) {
        return false;
    }
    snprintf(destination, destination_size, "%s", furi_string_get_cstr(buffer));
    return true;
}

bool tumotag_report_load_last(Storage* storage, TumoTagOperation* operation, TumoTagResult* result) {
    furi_check(storage);
    furi_check(operation);
    furi_check(result);
    if(!storage_file_exists(storage, TUMOTAG_LAST_RESULT_PATH)) return false;

    FlipperFormat* format = flipper_format_file_alloc(storage);
    FuriString* text = furi_string_alloc();
    FuriString* header = furi_string_alloc();
    uint32_t version = 0U;
    uint32_t operation_value = UINT32_MAX;
    uint32_t medium = UINT32_MAX;
    uint32_t verdict = UINT32_MAX;
    uint32_t compared = 0U;
    uint32_t missing = 0U;
    uint32_t mismatched = 0U;
    uint32_t duplicates = 0U;
    uint32_t truncated = 0U;
    bool success = false;
    do {
        if(!flipper_format_file_open_existing(format, TUMOTAG_LAST_RESULT_PATH) ||
           !flipper_format_read_header(format, header, &version) ||
           version != TUMOTAG_FORMAT_VERSION ||
           furi_string_cmp_str(header, TUMOTAG_RESULT_FILETYPE) != 0 ||
           !flipper_format_read_uint32(format, "Operation", &operation_value, 1U) ||
           !flipper_format_read_uint32(format, "Medium", &medium, 1U) ||
           !flipper_format_read_uint32(format, "Verdict", &verdict, 1U) ||
           operation_value > TumoTagOperationFind || medium > TumoTagMediumIButton ||
           verdict > TumoTagVerdictUnsupported) {
            break;
        }
        tumotag_result_reset(result, medium);
        result->verdict = verdict;
        *operation = operation_value;
        if(!tumotag_read_string(
               format,
               "Expected protocol",
               result->expected_protocol,
               sizeof(result->expected_protocol),
               text) ||
           !tumotag_read_string(
               format,
               "Observed protocol",
               result->observed_protocol,
               sizeof(result->observed_protocol),
               text) ||
           !tumotag_read_string(
               format, "Expected ID", result->expected_id, sizeof(result->expected_id), text) ||
           !tumotag_read_string(
               format, "Observed ID", result->observed_id, sizeof(result->observed_id), text) ||
           !tumotag_read_string(format, "Detail", result->detail, sizeof(result->detail), text) ||
           !tumotag_read_string(
               format,
               "Expected path",
               result->expected_path,
               sizeof(result->expected_path),
               text) ||
           !tumotag_read_string(
               format, "Match path", result->match_path, sizeof(result->match_path), text) ||
           !flipper_format_read_uint32(format, "Compared units", &compared, 1U) ||
           !flipper_format_read_uint32(format, "Missing units", &missing, 1U) ||
           !flipper_format_read_uint32(format, "Mismatched units", &mismatched, 1U) ||
           !flipper_format_read_uint32(format, "Duplicate matches", &duplicates, 1U) ||
           !flipper_format_read_uint32(format, "Search truncated", &truncated, 1U)) {
            break;
        }
        result->compared_units = compared;
        result->missing_units = missing;
        result->mismatched_units = mismatched;
        result->duplicate_matches = duplicates;
        result->search_truncated = truncated != 0U;
        success = true;
    } while(false);

    furi_string_free(header);
    furi_string_free(text);
    flipper_format_free(format);
    return success;
}
