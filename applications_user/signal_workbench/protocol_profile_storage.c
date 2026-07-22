#include "protocol_profile_storage.h"

#include <flipper_format/flipper_format.h>
#include <furi_hal_rtc.h>

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PROTOCOL_PROFILE_FILETYPE              "Tumo Protocol Profile"
#define PROTOCOL_PROFILE_RAW_FILETYPE          "Flipper SubGhz RAW File"
#define PROTOCOL_PROFILE_OBSERVATIONS_MAX_SIZE (64U * 1024U)
#define PROTOCOL_PROFILE_OBSERVATIONS_PREVIOUS \
    PROTOCOL_PROFILE_DATA_DIR "/protocol_observations.previous.csv"

static bool protocol_profile_has_extension(const char* filename, const char* extension);
static bool protocol_profile_id_valid(const char* profile_id);

static bool protocol_profile_filename_safe(const char* filename) {
    if(filename == NULL) return false;
    size_t length = 0U;
    while(length < ProtocolProfileFilenameSize && filename[length] != '\0')
        length++;
    if(length <= strlen(".tproto") || length >= ProtocolProfileFilenameSize ||
       !protocol_profile_has_extension(filename, ".tproto") ||
       strcmp(filename, PROTOCOL_PROFILE_DEMO_FILENAME) == 0 ||
       strchr(filename, '/') != NULL || strchr(filename, '\\') != NULL ||
       strstr(filename, "..") != NULL) {
        return false;
    }
    return true;
}

static const char* protocol_profile_checksum_format(ProtocolProfileChecksum checksum) {
    switch(checksum) {
    case ProtocolProfileChecksumParityEvenLast:
        return "parity-even-last";
    case ProtocolProfileChecksumParityOddLast:
        return "parity-odd-last";
    case ProtocolProfileChecksumXor8Last:
        return "xor8-last";
    case ProtocolProfileChecksumSum8Last:
        return "sum8-last";
    default:
        return "none";
    }
}

static bool protocol_profile_mkdir(Storage* storage, const char* path) {
    const FS_Error error = storage_common_mkdir(storage, path);
    return error == FSE_OK || error == FSE_EXIST;
}

static void protocol_profile_copy_if_missing(
    Storage* storage,
    const char* source,
    const char* destination) {
    if(storage_common_stat(storage, destination, NULL) == FSE_OK ||
       storage_common_stat(storage, source, NULL) != FSE_OK) {
        return;
    }
    storage_common_copy(storage, source, destination);
}

static void protocol_profile_migrate_legacy_profiles(Storage* storage) {
    File* directory = storage_file_alloc(storage);
    FileInfo info;
    char filename[ProtocolProfileFilenameSize];
    if(storage_dir_open(directory, PROTOCOL_PROFILE_LEGACY_DIRECTORY)) {
        while(storage_dir_read(directory, &info, filename, sizeof(filename))) {
            if(file_info_is_dir(&info) || info.size == 0U ||
               info.size > ProtocolProfileMaximumFileSize ||
               !protocol_profile_has_extension(filename, ".tproto")) {
                continue;
            }
            char source[192];
            char destination[192];
            snprintf(source, sizeof(source), PROTOCOL_PROFILE_LEGACY_DIRECTORY "/%s", filename);
            snprintf(destination, sizeof(destination), PROTOCOL_PROFILE_DIRECTORY "/%s", filename);
            protocol_profile_copy_if_missing(storage, source, destination);
        }
        storage_dir_close(directory);
    }
    storage_file_free(directory);
}

bool protocol_profile_storage_prepare(Storage* storage) {
    if(storage == NULL || !protocol_profile_mkdir(storage, PROTOCOL_PROFILE_DATA_DIR) ||
       !protocol_profile_mkdir(storage, PROTOCOL_PROFILE_DIRECTORY) ||
       !protocol_profile_mkdir(storage, PROTOCOL_PROFILE_DEMO_DIRECTORY)) {
        return false;
    }
    protocol_profile_migrate_legacy_profiles(storage);
    protocol_profile_copy_if_missing(
        storage, PROTOCOL_PROFILE_LEGACY_DEMO, PROTOCOL_PROFILE_DEMO_CAPTURE);
    return true;
}

static void protocol_profile_filename_slug(const char* name, char* slug, size_t slug_size) {
    size_t used = 0U;
    bool separator = false;
    for(size_t index = 0U; name[index] != '\0' && used + 1U < slug_size; index++) {
        unsigned char value = (unsigned char)name[index];
        if(value >= 'A' && value <= 'Z') value = (unsigned char)(value - 'A' + 'a');
        if((value >= 'a' && value <= 'z') || (value >= '0' && value <= '9')) {
            slug[used++] = (char)value;
            separator = false;
        } else if(!separator && used > 0U) {
            slug[used++] = '_';
            separator = true;
        }
    }
    while(used > 0U && slug[used - 1U] == '_')
        used--;
    if(used == 0U) {
        strlcpy(slug, "profile", slug_size);
        return;
    }
    slug[used] = '\0';
}

bool protocol_profile_package_save(
    Storage* storage,
    const ProtocolProfilePackage* package,
    char* saved_path,
    size_t saved_path_size) {
    if(storage == NULL || package == NULL || package->status != ProtocolProfileStatusOk ||
       package->name[0] == '\0' || !protocol_profile_id_valid(package->profile_id) ||
       package->training_captures == 0U || package->training_frames == 0U ||
       protocol_profile_validate(&package->profile, package->profile.minimum_api) !=
           ProtocolProfileStatusOk ||
       !protocol_profile_storage_prepare(storage)) {
        return false;
    }

    char slug[25];
    protocol_profile_filename_slug(package->name, slug, sizeof(slug));
    char filename[ProtocolProfileFilenameSize];
    const int filename_length =
        snprintf(filename, sizeof(filename), "%s_%s.tproto", slug, package->profile_id);
    char path[160];
    char temporary[168];
    char backup[168];
    const int path_length =
        snprintf(path, sizeof(path), PROTOCOL_PROFILE_DIRECTORY "/%s", filename);
    const int temporary_length = snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    const int backup_length = snprintf(backup, sizeof(backup), "%s.bak", path);
    if(filename_length <= 0 || (size_t)filename_length >= sizeof(filename) || path_length <= 0 ||
       (size_t)path_length >= sizeof(path) || temporary_length <= 0 ||
       (size_t)temporary_length >= sizeof(temporary) || backup_length <= 0 ||
       (size_t)backup_length >= sizeof(backup)) {
        return false;
    }

    char masks[4][19];
    snprintf(
        masks[0], sizeof(masks[0]), "0x%016llX", (unsigned long long)package->profile.stable_mask);
    snprintf(
        masks[1], sizeof(masks[1]), "0x%016llX", (unsigned long long)package->profile.stable_value);
    snprintf(
        masks[2],
        sizeof(masks[2]),
        "0x%016llX",
        (unsigned long long)package->profile.variable_mask);
    snprintf(
        masks[3],
        sizeof(masks[3]),
        "0x%016llX",
        (unsigned long long)package->profile.uncertain_mask);
    const uint32_t minimum_api = package->profile.minimum_api;
    const uint32_t frequency = package->profile.frequency_hz;
    const uint32_t tolerance = package->profile.tolerance_percent;
    const uint32_t prefix_count = package->profile.prefix_count;
    const uint32_t bit_count = package->profile.bit_count;
    const uint32_t zero_high = package->profile.zero_high_us;
    const uint32_t zero_low = package->profile.zero_low_us;
    const uint32_t one_high = package->profile.one_high_us;
    const uint32_t one_low = package->profile.one_low_us;
    const uint32_t confidence = package->profile.confidence;
    const uint32_t training_captures = package->training_captures;
    const uint32_t training_frames = package->training_frames;
    const bool receive_only = package->profile.receive_only;
    const bool review_required = package->profile.review_required;

    storage_common_remove(storage, temporary);
    FlipperFormat* format = flipper_format_file_alloc(storage);
    bool success = false;
    do {
        if(!flipper_format_file_open_always(format, temporary) ||
           !flipper_format_write_header_cstr(
               format, PROTOCOL_PROFILE_FILETYPE, ProtocolProfileVersion) ||
           !flipper_format_write_string_cstr(format, "Name", package->name) ||
           !flipper_format_write_string_cstr(format, "Profile ID", package->profile_id) ||
           !flipper_format_write_uint32(format, "Minimum API", &minimum_api, 1U) ||
           !flipper_format_write_string_cstr(format, "Encoding", "pulse-pair") ||
           !flipper_format_write_string_cstr(
               format, "Polarity", package->profile.inverted ? "inverted" : "normal") ||
           !flipper_format_write_uint32(format, "Frequency", &frequency, 1U) ||
           !flipper_format_write_uint32(format, "Tolerance", &tolerance, 1U) ||
           !flipper_format_write_uint32(format, "Preamble count", &prefix_count, 1U)) {
            break;
        }
        if(prefix_count > 0U &&
           !flipper_format_write_int32(
               format, "Preamble", package->profile.prefix, package->profile.prefix_count)) {
            break;
        }
        if(!flipper_format_write_uint32(format, "Bit count", &bit_count, 1U) ||
           !flipper_format_write_uint32(format, "Zero high", &zero_high, 1U) ||
           !flipper_format_write_uint32(format, "Zero low", &zero_low, 1U) ||
           !flipper_format_write_uint32(format, "One high", &one_high, 1U) ||
           !flipper_format_write_uint32(format, "One low", &one_low, 1U) ||
           !flipper_format_write_string_cstr(format, "Stable mask", masks[0]) ||
           !flipper_format_write_string_cstr(format, "Stable value", masks[1]) ||
           !flipper_format_write_string_cstr(format, "Variable mask", masks[2]) ||
           !flipper_format_write_string_cstr(format, "Uncertain mask", masks[3]) ||
           !flipper_format_write_string_cstr(
               format, "Checksum", protocol_profile_checksum_format(package->profile.checksum)) ||
           !flipper_format_write_string_cstr(
               format,
               "Checksum candidates",
               protocol_profile_checksum_format(package->profile.checksum)) ||
           !flipper_format_write_uint32(format, "Confidence", &confidence, 1U) ||
           !flipper_format_write_string_cstr(format, "Ambiguity", package->ambiguity) ||
           !flipper_format_write_uint32(format, "Training captures", &training_captures, 1U) ||
           !flipper_format_write_uint32(format, "Training frames", &training_frames, 1U) ||
           !flipper_format_write_bool(format, "Receive only", &receive_only, 1U) ||
           !flipper_format_write_bool(format, "Review required", &review_required, 1U)) {
            break;
        }
        success = flipper_format_file_close(format);
    } while(false);
    flipper_format_free(format);
    if(!success) {
        storage_common_remove(storage, temporary);
        return false;
    }
    FileInfo temporary_info = {0};
    if(storage_common_stat(storage, temporary, &temporary_info) != FSE_OK ||
       temporary_info.size == 0U || temporary_info.size > ProtocolProfileMaximumFileSize) {
        storage_common_remove(storage, temporary);
        return false;
    }

    storage_common_remove(storage, backup);
    const bool had_profile = storage_file_exists(storage, path);
    if(had_profile && storage_common_rename(storage, path, backup) != FSE_OK) {
        storage_common_remove(storage, temporary);
        return false;
    }
    if(storage_common_rename(storage, temporary, path) != FSE_OK) {
        storage_common_remove(storage, temporary);
        if(had_profile) storage_common_rename(storage, backup, path);
        return false;
    }
    storage_common_remove(storage, backup);
    if(saved_path != NULL && saved_path_size > 0U) strlcpy(saved_path, path, saved_path_size);
    return true;
}

bool protocol_profile_package_delete(Storage* storage, const ProtocolProfilePackage* package) {
    if(storage == NULL || package == NULL ||
       !protocol_profile_filename_safe(package->filename)) {
        return false;
    }

    char path[160];
    const int path_length =
        snprintf(path, sizeof(path), PROTOCOL_PROFILE_DIRECTORY "/%s", package->filename);
    if(path_length <= 0 || (size_t)path_length >= sizeof(path)) return false;

    FileInfo info = {0};
    if(storage_common_stat(storage, path, &info) != FSE_OK || file_info_is_dir(&info)) {
        return false;
    }
    return storage_common_remove(storage, path) == FSE_OK;
}

static bool protocol_profile_has_extension(const char* filename, const char* extension) {
    const char* dot = strrchr(filename, '.');
    return dot != NULL && strcmp(dot, extension) == 0;
}

static bool protocol_profile_parse_u64(const char* text, uint64_t* value) {
    if(text == NULL || value == NULL || text[0] == '\0' || text[0] == '-') return false;
    errno = 0;
    char* end = NULL;
    const unsigned long long parsed = strtoull(text, &end, 0);
    if(errno == ERANGE || end == text || *end != '\0') return false;
    *value = (uint64_t)parsed;
    return true;
}

static bool protocol_profile_copy_bounded(
    const FuriString* source,
    char* destination,
    size_t destination_size) {
    const size_t length = furi_string_size(source);
    if(length == 0U || length >= destination_size) return false;
    const char* text = furi_string_get_cstr(source);
    for(size_t index = 0U; index < length; index++) {
        const unsigned char value = (unsigned char)text[index];
        if(value < 0x20U || value > 0x7EU || value == '"') return false;
    }
    strlcpy(destination, text, destination_size);
    return true;
}

static bool protocol_profile_id_valid(const char* profile_id) {
    if(strlen(profile_id) != 16U) return false;
    for(size_t index = 0U; index < 16U; index++) {
        const char value = profile_id[index];
        if(!((value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') ||
             (value >= 'A' && value <= 'F'))) {
            return false;
        }
    }
    return true;
}

static bool protocol_profile_checksum_parse(const char* text, ProtocolProfileChecksum* checksum) {
    if(strcmp(text, "none") == 0) {
        *checksum = ProtocolProfileChecksumNone;
    } else if(strcmp(text, "parity-even-last") == 0) {
        *checksum = ProtocolProfileChecksumParityEvenLast;
    } else if(strcmp(text, "parity-odd-last") == 0) {
        *checksum = ProtocolProfileChecksumParityOddLast;
    } else if(strcmp(text, "xor8-last") == 0) {
        *checksum = ProtocolProfileChecksumXor8Last;
    } else if(strcmp(text, "sum8-last") == 0) {
        *checksum = ProtocolProfileChecksumSum8Last;
    } else {
        return false;
    }
    return true;
}

static ProtocolProfileStatus protocol_profile_package_read(
    Storage* storage,
    const char* path,
    uint32_t current_api,
    ProtocolProfilePackage* package) {
    FlipperFormat* format = flipper_format_file_alloc(storage);
    FuriString* header = furi_string_alloc();
    FuriString* value = furi_string_alloc();
    uint32_t version = 0U;
    uint32_t minimum_api = 0U;
    uint32_t frequency_hz = 0U;
    uint32_t tolerance = 0U;
    uint32_t prefix_count = 0U;
    uint32_t bit_count = 0U;
    uint32_t zero_high = 0U;
    uint32_t zero_low = 0U;
    uint32_t one_high = 0U;
    uint32_t one_low = 0U;
    uint32_t confidence = 0U;
    uint32_t training_captures = 0U;
    uint32_t training_frames = 0U;
    bool receive_only = false;
    bool review_required = false;
    ProtocolProfileStatus status = ProtocolProfileStatusUnsafeProfile;
    ProtocolProfile profile = {0};

    do {
        if(!flipper_format_file_open_existing(format, path) ||
           !flipper_format_read_header(format, header, &version) ||
           furi_string_cmp_str(header, PROTOCOL_PROFILE_FILETYPE) != 0 ||
           !flipper_format_read_string(format, "Name", value)) {
            break;
        }
        if(!protocol_profile_copy_bounded(value, package->name, sizeof(package->name))) break;
        if(!flipper_format_read_string(format, "Profile ID", value)) break;
        if(!protocol_profile_copy_bounded(
               value, package->profile_id, sizeof(package->profile_id)) ||
           !protocol_profile_id_valid(package->profile_id) ||
           !flipper_format_read_uint32(format, "Minimum API", &minimum_api, 1U) ||
           !flipper_format_read_string(format, "Encoding", value) ||
           furi_string_cmp_str(value, "pulse-pair") != 0 ||
           !flipper_format_read_string(format, "Polarity", value)) {
            break;
        }
        if(furi_string_cmp_str(value, "normal") == 0) {
            profile.inverted = false;
        } else if(furi_string_cmp_str(value, "inverted") == 0) {
            profile.inverted = true;
        } else {
            break;
        }
        if(!flipper_format_read_uint32(format, "Frequency", &frequency_hz, 1U) ||
           !flipper_format_read_uint32(format, "Tolerance", &tolerance, 1U) ||
           !flipper_format_read_uint32(format, "Preamble count", &prefix_count, 1U) ||
           prefix_count > ProtocolProfileMaximumPrefix ||
           (prefix_count > 0U &&
            !flipper_format_read_int32(format, "Preamble", profile.prefix, prefix_count)) ||
           !flipper_format_read_uint32(format, "Bit count", &bit_count, 1U) ||
           !flipper_format_read_uint32(format, "Zero high", &zero_high, 1U) ||
           !flipper_format_read_uint32(format, "Zero low", &zero_low, 1U) ||
           !flipper_format_read_uint32(format, "One high", &one_high, 1U) ||
           !flipper_format_read_uint32(format, "One low", &one_low, 1U)) {
            break;
        }

        uint64_t* masks[] = {
            &profile.stable_mask,
            &profile.stable_value,
            &profile.variable_mask,
            &profile.uncertain_mask,
        };
        const char* mask_keys[] = {
            "Stable mask",
            "Stable value",
            "Variable mask",
            "Uncertain mask",
        };
        bool masks_valid = true;
        for(size_t index = 0U; index < 4U; index++) {
            if(!flipper_format_read_string(format, mask_keys[index], value) ||
               !protocol_profile_parse_u64(furi_string_get_cstr(value), masks[index])) {
                masks_valid = false;
                break;
            }
        }
        if(!masks_valid || !flipper_format_read_string(format, "Checksum", value) ||
           !protocol_profile_checksum_parse(furi_string_get_cstr(value), &profile.checksum) ||
           !flipper_format_read_uint32(format, "Confidence", &confidence, 1U) ||
           !flipper_format_read_string(format, "Ambiguity", value)) {
            break;
        }
        if(!protocol_profile_copy_bounded(value, package->ambiguity, sizeof(package->ambiguity))) {
            break;
        }
        if(!flipper_format_read_uint32(format, "Training captures", &training_captures, 1U) ||
           !flipper_format_read_uint32(format, "Training frames", &training_frames, 1U) ||
           !flipper_format_read_bool(format, "Receive only", &receive_only, 1U) ||
           !flipper_format_read_bool(format, "Review required", &review_required, 1U) ||
           tolerance > UINT8_MAX || prefix_count > UINT8_MAX || bit_count > UINT8_MAX ||
           confidence > UINT8_MAX || training_captures > UINT8_MAX ||
           training_frames > UINT8_MAX || training_captures == 0U || training_frames == 0U) {
            break;
        }

        profile.version = version;
        profile.minimum_api = minimum_api;
        profile.frequency_hz = frequency_hz;
        profile.tolerance_percent = (uint8_t)tolerance;
        profile.prefix_count = (uint8_t)prefix_count;
        profile.bit_count = (uint8_t)bit_count;
        profile.zero_high_us = zero_high;
        profile.zero_low_us = zero_low;
        profile.one_high_us = one_high;
        profile.one_low_us = one_low;
        profile.confidence = (uint8_t)confidence;
        profile.receive_only = receive_only;
        profile.review_required = review_required;
        package->training_captures = (uint8_t)training_captures;
        package->training_frames = (uint8_t)training_frames;
        status = protocol_profile_validate(&profile, current_api);
        if(status == ProtocolProfileStatusOk) package->profile = profile;
    } while(false);

    furi_string_free(value);
    furi_string_free(header);
    flipper_format_free(format);
    return status;
}

static void
    protocol_profile_package_swap(ProtocolProfilePackage* first, ProtocolProfilePackage* second) {
    const ProtocolProfilePackage temporary = *first;
    *first = *second;
    *second = temporary;
}

size_t protocol_profile_packages_load(
    Storage* storage,
    uint32_t current_api,
    ProtocolProfilePackage* packages,
    size_t capacity) {
    if(storage == NULL || packages == NULL || capacity == 0U) return 0U;
    protocol_profile_storage_prepare(storage);
    const size_t bounded_capacity =
        capacity < ProtocolProfileMaximumProfiles ? capacity : ProtocolProfileMaximumProfiles;
    memset(packages, 0, sizeof(*packages) * bounded_capacity);

    File* directory = storage_file_alloc(storage);
    FileInfo info;
    char filename[ProtocolProfileFilenameSize];
    size_t count = 0U;
    if(storage_dir_open(directory, PROTOCOL_PROFILE_DIRECTORY)) {
        while(count < bounded_capacity &&
              storage_dir_read(directory, &info, filename, sizeof(filename))) {
            if(file_info_is_dir(&info) || !protocol_profile_has_extension(filename, ".tproto")) {
                continue;
            }
            ProtocolProfilePackage* package = &packages[count++];
            strlcpy(package->filename, filename, sizeof(package->filename));
            if(info.size == 0U || info.size > ProtocolProfileMaximumFileSize) {
                package->status = ProtocolProfileStatusUnsafeProfile;
                continue;
            }
            char path[160];
            snprintf(path, sizeof(path), PROTOCOL_PROFILE_DIRECTORY "/%s", filename);
            package->status = protocol_profile_package_read(storage, path, current_api, package);
        }
        storage_dir_close(directory);
    }
    storage_file_free(directory);

    for(size_t outer = 0U; outer < count; outer++) {
        for(size_t inner = outer + 1U; inner < count; inner++) {
            const char* first = packages[outer].name[0] ? packages[outer].name :
                                                          packages[outer].filename;
            const char* second = packages[inner].name[0] ? packages[inner].name :
                                                           packages[inner].filename;
            if(strcmp(first, second) > 0) {
                protocol_profile_package_swap(&packages[outer], &packages[inner]);
            }
        }
    }
    return count;
}

ProtocolProfileStatus protocol_profile_capture_load(
    Storage* storage,
    const char* path,
    int32_t* pulses,
    size_t capacity,
    ProtocolProfileCaptureInfo* info) {
    if(storage == NULL || path == NULL || pulses == NULL || capacity == 0U ||
       capacity > ProtocolProfileMaximumCapturePulses || info == NULL) {
        return ProtocolProfileStatusInvalidArgument;
    }
    memset(info, 0, sizeof(*info));
    FileInfo file_info = {0};
    if(storage_common_stat(storage, path, &file_info) != FSE_OK || file_info.size == 0U ||
       file_info.size > 1024U * 1024U) {
        return ProtocolProfileStatusUnsafeProfile;
    }

    FlipperFormat* format = flipper_format_file_alloc(storage);
    FuriString* header = furi_string_alloc();
    FuriString* protocol = furi_string_alloc();
    uint32_t version = 0U;
    ProtocolProfileStatus status = ProtocolProfileStatusUnsafeProfile;
    do {
        if(!flipper_format_file_open_existing(format, path) ||
           !flipper_format_read_header(format, header, &version) || version != 1U) {
            break;
        }
        if(furi_string_cmp_str(header, PROTOCOL_PROFILE_RAW_FILETYPE) != 0) {
            status = ProtocolProfileStatusUnsupportedCapture;
            break;
        }
        if(!flipper_format_read_uint32(format, "Frequency", &info->frequency_hz, 1U) ||
           !flipper_format_read_string(format, "Protocol", protocol)) {
            break;
        }
        if(furi_string_cmp_str(protocol, "RAW") != 0) {
            status = ProtocolProfileStatusUnsupportedCapture;
            break;
        }

        bool malformed = false;
        while(info->pulse_count < capacity) {
            uint32_t line_count = 0U;
            if(!flipper_format_get_value_count(format, "RAW_Data", &line_count)) break;
            if(line_count == 0U || line_count > ProtocolProfileMaximumCapturePulses) {
                malformed = true;
                break;
            }
            int32_t* line = malloc(sizeof(int32_t) * line_count);
            if(line == NULL) {
                malformed = true;
                break;
            }
            const bool read = flipper_format_read_int32(format, "RAW_Data", line, line_count);
            if(!read) {
                free(line);
                malformed = true;
                break;
            }
            const size_t remaining = capacity - info->pulse_count;
            const size_t copy_count = line_count < remaining ? line_count : remaining;
            bool line_valid = true;
            for(size_t index = 0U; index < copy_count; index++) {
                const int32_t value = line[index];
                const uint32_t magnitude = value == INT32_MIN ? UINT32_MAX : (uint32_t)abs(value);
                if(value == 0 || magnitude < 40U || magnitude > 1000000U) {
                    line_valid = false;
                    break;
                }
                pulses[info->pulse_count + index] = value;
            }
            free(line);
            if(!line_valid) {
                malformed = true;
                break;
            }
            info->pulse_count += copy_count;
            if(copy_count < line_count) {
                info->truncated = true;
                break;
            }
        }
        if(malformed) break;
        if(info->pulse_count == capacity) {
            uint32_t remaining_count = 0U;
            info->truncated = flipper_format_get_value_count(format, "RAW_Data", &remaining_count);
        }
        if(info->pulse_count > 0U) status = ProtocolProfileStatusOk;
    } while(false);

    furi_string_free(protocol);
    furi_string_free(header);
    flipper_format_free(format);
    return status;
}

bool protocol_profile_observation_append(
    Storage* storage,
    const ProtocolProfilePackage* package,
    const ProtocolProfileDecodeResult* result,
    uint64_t changed_mask,
    uint32_t match_count) {
    if(storage == NULL || package == NULL || result == NULL ||
       package->status != ProtocolProfileStatusOk || !protocol_profile_storage_prepare(storage)) {
        return false;
    }

    FileInfo info = {0};
    if(storage_common_stat(storage, PROTOCOL_PROFILE_OBSERVATIONS, &info) == FSE_OK &&
       info.size >= PROTOCOL_PROFILE_OBSERVATIONS_MAX_SIZE) {
        storage_common_remove(storage, PROTOCOL_PROFILE_OBSERVATIONS_PREVIOUS);
        storage_common_rename(
            storage, PROTOCOL_PROFILE_OBSERVATIONS, PROTOCOL_PROFILE_OBSERVATIONS_PREVIOUS);
    }

    const bool write_header = storage_common_stat(storage, PROTOCOL_PROFILE_OBSERVATIONS, NULL) !=
                              FSE_OK;
    File* file = storage_file_alloc(storage);
    bool success =
        storage_file_open(file, PROTOCOL_PROFILE_OBSERVATIONS, FSAM_WRITE, FSOM_OPEN_APPEND);
    if(success && write_header) {
        static const char header[] =
            "timestamp,profile_id,name,frequency_hz,bits,value,changed_mask,matches\n";
        success = storage_file_write(file, header, sizeof(header) - 1U) == sizeof(header) - 1U;
    }
    if(success) {
        DateTime now;
        furi_hal_rtc_get_datetime(&now);
        char line[256];
        const int length = snprintf(
            line,
            sizeof(line),
            "%04u-%02u-%02uT%02u:%02u:%02u,%s,\"%.31s\",%lu,%u,0x%016llX,0x%016llX,%lu\n",
            now.year,
            now.month,
            now.day,
            now.hour,
            now.minute,
            now.second,
            package->profile_id,
            package->name,
            (unsigned long)package->profile.frequency_hz,
            result->bit_count,
            (unsigned long long)result->value,
            (unsigned long long)changed_mask,
            (unsigned long)match_count);
        success = length > 0 && (size_t)length < sizeof(line) &&
                  storage_file_write(file, line, (size_t)length) == (size_t)length;
    }
    if(success) success = storage_file_sync(file);
    storage_file_close(file);
    storage_file_free(file);
    return success;
}
