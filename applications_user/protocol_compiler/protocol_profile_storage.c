#include "protocol_profile_storage.h"

#include <flipper_format/flipper_format.h>

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PROTOCOL_PROFILE_FILETYPE     "Tumo Protocol Profile"
#define PROTOCOL_PROFILE_RAW_FILETYPE "Flipper SubGhz RAW File"

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
    strlcpy(destination, furi_string_get_cstr(source), destination_size);
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
