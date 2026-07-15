#include "tumospectrum_storage.h"

#include <flipper_format/flipper_format.h>
#include <furi_hal_rtc.h>

#include <stdio.h>
#include <string.h>

static bool tumospectrum_mkdir(Storage* storage, const char* path) {
    const FS_Error error = storage_common_mkdir(storage, path);
    return error == FSE_OK || error == FSE_EXIST;
}

bool tumospectrum_storage_prepare(Storage* storage) {
    return tumospectrum_mkdir(storage, TUMOSPECTRUM_DATA_DIR) &&
           tumospectrum_mkdir(storage, TUMOSPECTRUM_REPORT_DIR) &&
           tumospectrum_mkdir(storage, TUMOSPECTRUM_NOTEBOOK_DIR) &&
           tumospectrum_mkdir(storage, TUMOSPECTRUM_SET_DIR);
}

static void tumospectrum_timestamp(char* output, size_t output_size, bool filename) {
    DateTime now;
    furi_hal_rtc_get_datetime(&now);
    snprintf(
        output,
        output_size,
        filename ? "%04u%02u%02u_%02u%02u%02u" : "%04u-%02u-%02uT%02u:%02u:%02u",
        now.year,
        now.month,
        now.day,
        now.hour,
        now.minute,
        now.second);
}

static bool tumospectrum_write_file(Storage* storage, const char* path, const char* text) {
    char temporary[TUMOSPECTRUM_PATH_SIZE];
    const int length = snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    if(length <= 0 || (size_t)length >= sizeof(temporary)) return false;

    File* file = storage_file_alloc(storage);
    bool success = storage_file_open(file, temporary, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    if(success) {
        const size_t text_size = strlen(text);
        success = storage_file_write(file, text, text_size) == text_size &&
                  storage_file_sync(file);
    }
    storage_file_close(file);
    storage_file_free(file);
    if(!success) {
        storage_common_remove(storage, temporary);
        return false;
    }
    storage_common_remove(storage, path);
    if(storage_common_rename(storage, temporary, path) != FSE_OK) {
        storage_common_remove(storage, temporary);
        return false;
    }
    return true;
}

static void tumospectrum_append_json_string(FuriString* output, const char* text) {
    furi_string_push_back(output, '"');
    for(const unsigned char* cursor = (const unsigned char*)text; *cursor; cursor++) {
        switch(*cursor) {
        case '"':
            furi_string_cat(output, "\\\"");
            break;
        case '\\':
            furi_string_cat(output, "\\\\");
            break;
        case '\n':
            furi_string_cat(output, "\\n");
            break;
        case '\r':
            furi_string_cat(output, "\\r");
            break;
        case '\t':
            furi_string_cat(output, "\\t");
            break;
        default:
            if(*cursor >= 0x20U && *cursor < 0x7FU) furi_string_push_back(output, (char)*cursor);
            break;
        }
    }
    furi_string_push_back(output, '"');
}

static bool tumospectrum_csv_needs_quotes(const char* value) {
    for(const char* cursor = value; *cursor; cursor++) {
        if(*cursor == ',' || *cursor == '"' || *cursor == '\r' || *cursor == '\n') return true;
    }
    return false;
}

static void tumospectrum_append_capture_json(
    FuriString* output,
    const char* key,
    const TumoSpectrumCapture* capture) {
    furi_string_cat_printf(output, "\"%s\":{\"type\":", key);
    tumospectrum_append_json_string(output, tumospectrum_type_name(capture->type));
    furi_string_cat(output, ",\"name\":");
    tumospectrum_append_json_string(output, capture->name);
    furi_string_cat(output, ",\"path\":");
    tumospectrum_append_json_string(output, capture->path);
    furi_string_cat(output, ",\"protocol\":");
    tumospectrum_append_json_string(output, capture->protocol);
    furi_string_cat(output, ",\"preset\":");
    tumospectrum_append_json_string(output, capture->preset);
    furi_string_cat(output, ",\"note\":");
    tumospectrum_append_json_string(output, capture->note);
    furi_string_cat_printf(
        output,
        ",\"frequency_hz\":%lu,\"file_size\":%llu,\"truncated\":%s,"
        "\"timings\":%lu,\"duration_us\":%llu,\"min_us\":%lu,\"avg_us\":%lu,"
        "\"max_us\":%lu,\"bursts\":%u,\"repeat_score\":%u,\"candidate\":",
        (unsigned long)capture->frequency_hz,
        (unsigned long long)capture->file_size,
        capture->truncated ? "true" : "false",
        (unsigned long)capture->analysis.count,
        (unsigned long long)capture->analysis.duration_us,
        (unsigned long)capture->analysis.minimum_us,
        (unsigned long)capture->analysis.average_us,
        (unsigned long)capture->analysis.maximum_us,
        capture->analysis.burst_count,
        capture->analysis.repeat_score);
    tumospectrum_append_json_string(output, capture->analysis.candidate);
    furi_string_cat(output, ",\"histogram\":[");
    for(size_t index = 0U; index < TUMOSPECTRUM_HISTOGRAM_BUCKETS; index++) {
        furi_string_cat_printf(
            output, "%s%u", index == 0U ? "" : ",", capture->analysis.histogram[index]);
    }
    furi_string_cat(output, "]}");
}

static const char* tumospectrum_capture_name(const TumoSpectrumCapture* capture) {
    if(capture->name[0]) return capture->name;
    const char* separator = strrchr(capture->path, '/');
    return separator ? separator + 1U : capture->path;
}

static void
    tumospectrum_build_set_json(const TumoSpectrumCaptureSet* capture_set, FuriString* output) {
    char timestamp[32];
    tumospectrum_timestamp(timestamp, sizeof(timestamp), false);
    furi_string_reset(output);
    furi_string_cat_printf(
        output,
        "{\"schema\":2,\"app\":\"TumoSpectrum\",\"kind\":\"capture_set\","
        "\"version\":\"%s\",\"created_at\":\"%s\",\"device\":",
        TUMOSPECTRUM_APP_VERSION,
        timestamp);
    tumospectrum_append_json_string(output, capture_set->device_label);
    furi_string_cat(output, ",\"control\":");
    tumospectrum_append_json_string(output, capture_set->control_label);
    furi_string_cat(output, ",\"capture_type\":");
    tumospectrum_append_json_string(output, tumospectrum_type_name(capture_set->type));
    furi_string_cat_printf(
        output,
        ",\"sample_count\":%u,\"inference\":{\"compatible\":%s,"
        "\"encoding\":",
        (unsigned int)capture_set->sample_count,
        capture_set->inference.compatible ? "true" : "false");
    tumospectrum_append_json_string(
        output, tumospectrum_encoding_name(capture_set->inference.encoding));
    furi_string_cat(output, ",\"replay_class\":");
    tumospectrum_append_json_string(
        output, tumospectrum_replay_class_name(capture_set->inference.replay_class));
    furi_string_cat_printf(
        output,
        ",\"stable_percent\":%u,\"stable_points\":%u,\"changing_points\":%u,"
        "\"aligned_points\":%u,\"frames\":%u,\"clusters_us\":[",
        capture_set->inference.stable_percent,
        capture_set->inference.stable_points,
        capture_set->inference.changing_points,
        capture_set->inference.aligned_points,
        capture_set->inference.frame_count);
    for(uint8_t index = 0U; index < capture_set->inference.cluster_count; index++) {
        furi_string_cat_printf(
            output,
            "%s%lu",
            index == 0U ? "" : ",",
            (unsigned long)capture_set->inference.clusters_us[index]);
    }
    furi_string_cat(output, "]},\"samples\":[");
    for(size_t index = 0U; index < capture_set->sample_count; index++) {
        if(index > 0U) furi_string_push_back(output, ',');
        tumospectrum_append_json_string(
            output, tumospectrum_capture_name(&capture_set->samples[index]));
    }
    furi_string_cat(output, "]}\n");
}

static bool tumospectrum_storage_write_set_file(
    Storage* storage,
    const char* path,
    const TumoSpectrumCaptureSet* capture_set) {
    char temporary[TUMOSPECTRUM_PATH_SIZE];
    const int temporary_length = snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    if(temporary_length <= 0 || (size_t)temporary_length >= sizeof(temporary)) return false;

    FlipperFormat* format = flipper_format_file_alloc(storage);
    uint32_t capture_type = (uint32_t)capture_set->type;
    uint32_t sample_count = (uint32_t)capture_set->sample_count;
    uint32_t compatible = capture_set->inference.compatible ? 1U : 0U;
    uint32_t lengths_match = capture_set->inference.frame_lengths_match ? 1U : 0U;
    uint32_t encoding = (uint32_t)capture_set->inference.encoding;
    uint32_t replay_class = (uint32_t)capture_set->inference.replay_class;
    uint32_t stable_percent = capture_set->inference.stable_percent;
    uint32_t frame_count = capture_set->inference.frame_count;
    uint32_t reference_points = capture_set->inference.reference_points;
    uint32_t aligned_points = capture_set->inference.aligned_points;
    uint32_t stable_points = capture_set->inference.stable_points;
    uint32_t changing_points = capture_set->inference.changing_points;
    uint32_t cluster_count = capture_set->inference.cluster_count;
    bool success = false;
    do {
        if(!flipper_format_file_open_always(format, temporary) ||
           !flipper_format_write_header_cstr(format, "TumoSpectrum Capture Set", 1U) ||
           !flipper_format_write_string_cstr(format, "Device", capture_set->device_label) ||
           !flipper_format_write_string_cstr(format, "Control", capture_set->control_label) ||
           !flipper_format_write_uint32(format, "Capture type", &capture_type, 1U) ||
           !flipper_format_write_uint32(format, "Sample count", &sample_count, 1U)) {
            break;
        }
        for(size_t index = 0U; index < capture_set->sample_count; index++) {
            char key[] = "Sample 1";
            key[7] = (char)('1' + index);
            if(!flipper_format_write_string_cstr(format, key, capture_set->samples[index].path)) {
                break;
            }
            if(index + 1U == capture_set->sample_count) success = true;
        }
        if(!success || !flipper_format_write_uint32(format, "Compatible", &compatible, 1U) ||
           !flipper_format_write_uint32(format, "Frame lengths match", &lengths_match, 1U) ||
           !flipper_format_write_uint32(format, "Encoding", &encoding, 1U) ||
           !flipper_format_write_uint32(format, "Replay class", &replay_class, 1U) ||
           !flipper_format_write_uint32(format, "Stable percent", &stable_percent, 1U) ||
           !flipper_format_write_uint32(format, "Frame count", &frame_count, 1U) ||
           !flipper_format_write_uint32(format, "Reference points", &reference_points, 1U) ||
           !flipper_format_write_uint32(format, "Aligned points", &aligned_points, 1U) ||
           !flipper_format_write_uint32(format, "Stable points", &stable_points, 1U) ||
           !flipper_format_write_uint32(format, "Changing points", &changing_points, 1U) ||
           !flipper_format_write_uint32(format, "Cluster count", &cluster_count, 1U)) {
            success = false;
            break;
        }
        if(cluster_count > 0U &&
           !flipper_format_write_uint32(
               format, "Clusters us", capture_set->inference.clusters_us, cluster_count)) {
            success = false;
            break;
        }
        success = true;
    } while(false);
    flipper_format_free(format);
    if(!success) {
        storage_common_remove(storage, temporary);
        return false;
    }
    storage_common_remove(storage, path);
    if(storage_common_rename(storage, temporary, path) != FSE_OK) {
        storage_common_remove(storage, temporary);
        return false;
    }
    return true;
}

static void tumospectrum_build_json(
    const TumoSpectrumCapture* capture,
    const TumoSpectrumCapture* compared,
    const TumoSpectrumComparison* comparison,
    FuriString* output) {
    char timestamp[32];
    tumospectrum_timestamp(timestamp, sizeof(timestamp), false);
    furi_string_reset(output);
    furi_string_cat_printf(
        output,
        "{\"schema\":1,\"app\":\"TumoSpectrum\",\"version\":\"%s\",\"created_at\":\"%s\",",
        TUMOSPECTRUM_APP_VERSION,
        timestamp);
    tumospectrum_append_capture_json(output, "capture", capture);
    if(compared && comparison && comparison->compatible) {
        furi_string_cat(output, ",");
        tumospectrum_append_capture_json(output, "compared", compared);
        furi_string_cat_printf(
            output,
            ",\"comparison\":{\"compatible\":true,\"likely_same\":%s,"
            "\"frequency_delta_hz\":%ld,\"pulse_delta\":%ld,"
            "\"duration_delta_percent\":%ld,\"histogram_similarity\":%u,"
            "\"overall_similarity\":%u}",
            comparison->likely_same ? "true" : "false",
            (long)comparison->frequency_delta_hz,
            (long)comparison->pulse_delta,
            (long)comparison->duration_delta_percent,
            comparison->histogram_similarity,
            comparison->overall_similarity);
    }
    furi_string_cat(output, "}\n");
}

void tumospectrum_storage_build_text(
    const TumoSpectrumCapture* capture,
    const TumoSpectrumCapture* compared,
    const TumoSpectrumComparison* comparison,
    FuriString* output) {
    char timestamp[32];
    tumospectrum_timestamp(timestamp, sizeof(timestamp), false);
    furi_string_reset(output);
    furi_string_cat_printf(
        output,
        "TumoSpectrum %s\nGenerated: %s\n\n"
        "Capture: %s\nName: %s\nPath: %s\nStatus: %s\n"
        "Frequency: %lu Hz\nProtocol: %s\nPreset: %s\n"
        "Timings: %lu%s\nDuration: %llu us\nMin/Avg/Max: %lu/%lu/%lu us\n"
        "Bursts: %u\nRepeat score: %u%%\nCandidate: %s\nNote: %s\n",
        TUMOSPECTRUM_APP_VERSION,
        timestamp,
        tumospectrum_type_name(capture->type),
        capture->name,
        capture->path,
        tumospectrum_status_name(capture->status),
        (unsigned long)capture->frequency_hz,
        capture->protocol[0] ? capture->protocol : "--",
        capture->preset[0] ? capture->preset : "--",
        (unsigned long)capture->analysis.count,
        capture->truncated ? " (preview)" : "",
        (unsigned long long)capture->analysis.duration_us,
        (unsigned long)capture->analysis.minimum_us,
        (unsigned long)capture->analysis.average_us,
        (unsigned long)capture->analysis.maximum_us,
        capture->analysis.burst_count,
        capture->analysis.repeat_score,
        capture->analysis.candidate[0] ? capture->analysis.candidate : "--",
        capture->note[0] ? capture->note : "--");
    if(compared && comparison && comparison->compatible) {
        furi_string_cat_printf(
            output,
            "\nComparison: %s\nSecond: %s\nSimilarity: %u%%\nHistogram: %u%%\n"
            "Frequency delta: %ld Hz\nPulse delta: %ld\nDuration delta: %ld%%\n",
            comparison->likely_same ? "likely same" : "different",
            compared->name,
            comparison->overall_similarity,
            comparison->histogram_similarity,
            (long)comparison->frequency_delta_hz,
            (long)comparison->pulse_delta,
            (long)comparison->duration_delta_percent);
    }
    furi_string_cat(output, "\nRead-only analysis. Replay requires explicit stock-app handoff.\n");
}

static bool tumospectrum_append_notebook(
    Storage* storage,
    const char* timestamp,
    const TumoSpectrumCapture* capture,
    const char* report_path) {
    const bool exists = storage_file_exists(storage, TUMOSPECTRUM_NOTEBOOK_CSV);
    File* file = storage_file_alloc(storage);
    bool success =
        storage_file_open(file, TUMOSPECTRUM_NOTEBOOK_CSV, FSAM_WRITE, FSOM_OPEN_APPEND);
    if(success && !exists) {
        static const char header[] = "timestamp,type,name,frequency_hz,protocol,note,report\n";
        success = storage_file_write(file, header, sizeof(header) - 1U) == sizeof(header) - 1U;
    }
    if(success) {
        char frequency[16];
        snprintf(frequency, sizeof(frequency), "%lu", (unsigned long)capture->frequency_hz);
        const char* fields[] = {
            timestamp,
            tumospectrum_type_name(capture->type),
            capture->name,
            frequency,
            capture->protocol,
            capture->note,
            report_path,
        };
        FuriString* line = furi_string_alloc();
        for(size_t field = 0U; field < COUNT_OF(fields); field++) {
            if(field > 0U) furi_string_push_back(line, ',');
            const char* value = fields[field];
            const bool quoted = tumospectrum_csv_needs_quotes(value);
            if(quoted) furi_string_push_back(line, '"');
            for(const char* cursor = value; *cursor; cursor++) {
                if(*cursor == '"') furi_string_push_back(line, '"');
                furi_string_push_back(line, *cursor);
            }
            if(quoted) furi_string_push_back(line, '"');
        }
        furi_string_push_back(line, '\n');
        const size_t length = furi_string_size(line);
        success = storage_file_write(file, furi_string_get_cstr(line), length) == length;
        furi_string_free(line);
    }
    if(success) success = storage_file_sync(file);
    storage_file_close(file);
    storage_file_free(file);
    return success;
}

bool tumospectrum_storage_save(
    Storage* storage,
    const TumoSpectrumCapture* capture,
    const TumoSpectrumCapture* compared,
    const TumoSpectrumComparison* comparison,
    char* report_path,
    size_t report_path_size) {
    if(!storage || !capture || !report_path || report_path_size == 0U ||
       !tumospectrum_storage_prepare(storage)) {
        return false;
    }
    char stem[32];
    char timestamp[32];
    char text_path[TUMOSPECTRUM_PATH_SIZE];
    tumospectrum_timestamp(stem, sizeof(stem), true);
    tumospectrum_timestamp(timestamp, sizeof(timestamp), false);
    snprintf(report_path, report_path_size, TUMOSPECTRUM_REPORT_DIR "/spectrum_%s.json", stem);
    snprintf(text_path, sizeof(text_path), TUMOSPECTRUM_REPORT_DIR "/spectrum_%s.txt", stem);

    FuriString* output = furi_string_alloc();
    tumospectrum_build_json(capture, compared, comparison, output);
    bool success = tumospectrum_write_file(storage, report_path, furi_string_get_cstr(output));
    if(success) {
        tumospectrum_storage_build_text(capture, compared, comparison, output);
        success = tumospectrum_write_file(storage, text_path, furi_string_get_cstr(output));
    }
    if(success) success = tumospectrum_append_notebook(storage, timestamp, capture, report_path);
    furi_string_free(output);
    return success;
}

bool tumospectrum_storage_load_latest(Storage* storage, FuriString* output) {
    if(!storage || !output) return false;
    File* directory = storage_file_alloc(storage);
    FileInfo info;
    char name[96];
    char latest[96] = "";
    if(storage_dir_open(directory, TUMOSPECTRUM_REPORT_DIR)) {
        while(storage_dir_read(directory, &info, name, sizeof(name))) {
            const size_t length = strlen(name);
            if(!file_info_is_dir(&info) && length > 4U &&
               strcmp(name + length - 4U, ".txt") == 0 &&
               (!latest[0] || strcmp(name, latest) > 0)) {
                strlcpy(latest, name, sizeof(latest));
            }
        }
        storage_dir_close(directory);
    }
    storage_file_free(directory);
    if(!latest[0]) return false;

    char path[TUMOSPECTRUM_PATH_SIZE];
    snprintf(path, sizeof(path), TUMOSPECTRUM_REPORT_DIR "/%s", latest);
    File* file = storage_file_alloc(storage);
    bool success = storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING);
    furi_string_reset(output);
    char block[128];
    size_t total = 0U;
    size_t count = 0U;
    while(success && total < 4096U &&
          (count = storage_file_read(file, block, sizeof(block))) > 0U) {
        const size_t copy = total + count > 4096U ? 4096U - total : count;
        furi_string_cat_printf(output, "%.*s", (int)copy, block);
        total += copy;
    }
    success = success && storage_file_get_error(file) == FSE_OK && total > 0U;
    storage_file_close(file);
    storage_file_free(file);
    return success;
}

void tumospectrum_storage_build_set_text(
    const TumoSpectrumCaptureSet* capture_set,
    FuriString* output) {
    furi_string_reset(output);
    furi_string_cat_printf(
        output,
        "TumoSpectrum Capture Set\n\n"
        "Device: %s\nControl: %s\nType: %s\nSamples: %u\n\n"
        "Encoding: %s\nReplay class: %s\nStable: %u%% (%u/%u)\n"
        "Changing: %u\nFrames: %u\nClusters:",
        capture_set->device_label,
        capture_set->control_label,
        tumospectrum_type_name(capture_set->type),
        (unsigned int)capture_set->sample_count,
        tumospectrum_encoding_name(capture_set->inference.encoding),
        tumospectrum_replay_class_name(capture_set->inference.replay_class),
        capture_set->inference.stable_percent,
        capture_set->inference.stable_points,
        capture_set->inference.aligned_points,
        capture_set->inference.changing_points,
        capture_set->inference.frame_count);
    for(uint8_t index = 0U; index < capture_set->inference.cluster_count; index++) {
        furi_string_cat_printf(
            output, " %lu", (unsigned long)capture_set->inference.clusters_us[index]);
    }
    furi_string_cat(output, " us\n\nSamples:\n");
    for(size_t index = 0U; index < capture_set->sample_count; index++) {
        furi_string_cat_printf(
            output,
            "%u. %s\n",
            (unsigned int)index + 1U,
            tumospectrum_capture_name(&capture_set->samples[index]));
    }
    furi_string_cat(
        output,
        "\nStatic-like describes timing similarity only. It is not a cryptographic or replay guarantee.\n");
}

bool tumospectrum_storage_save_set(
    Storage* storage,
    const TumoSpectrumCaptureSet* capture_set,
    char* profile_path,
    size_t profile_path_size,
    char* report_path,
    size_t report_path_size) {
    if(!storage || !capture_set || !profile_path || profile_path_size == 0U || !report_path ||
       report_path_size == 0U || !capture_set->inferred ||
       capture_set->sample_count < TUMOSPECTRUM_SET_MIN_SAMPLES ||
       capture_set->sample_count > TUMOSPECTRUM_SET_MAX_SAMPLES ||
       !tumospectrum_storage_prepare(storage)) {
        return false;
    }

    char stem[32];
    tumospectrum_timestamp(stem, sizeof(stem), true);
    const int profile_length =
        snprintf(profile_path, profile_path_size, TUMOSPECTRUM_SET_DIR "/set_%s.tsp", stem);
    const int report_length =
        snprintf(report_path, report_path_size, TUMOSPECTRUM_REPORT_DIR "/set_%s.json", stem);
    if(profile_length <= 0 || (size_t)profile_length >= profile_path_size || report_length <= 0 ||
       (size_t)report_length >= report_path_size) {
        return false;
    }

    bool success = tumospectrum_storage_write_set_file(storage, profile_path, capture_set);
    if(success) {
        success =
            tumospectrum_storage_write_set_file(storage, TUMOSPECTRUM_LATEST_SET, capture_set);
    }
    if(success) {
        FuriString* output = furi_string_alloc();
        tumospectrum_build_set_json(capture_set, output);
        success = tumospectrum_write_file(storage, report_path, furi_string_get_cstr(output));
        if(success) {
            success = tumospectrum_write_file(
                storage, TUMOSPECTRUM_LATEST_SET_REPORT, furi_string_get_cstr(output));
        }
        furi_string_free(output);
    }
    return success;
}

bool tumospectrum_storage_load_latest_set(Storage* storage, TumoSpectrumCaptureSet* capture_set) {
    if(!storage || !capture_set || !storage_file_exists(storage, TUMOSPECTRUM_LATEST_SET)) {
        return false;
    }
    memset(capture_set, 0, sizeof(*capture_set));

    FlipperFormat* format = flipper_format_file_alloc(storage);
    FuriString* header = furi_string_alloc();
    FuriString* value = furi_string_alloc();
    uint32_t version = 0U;
    uint32_t capture_type = 0U;
    uint32_t sample_count = 0U;
    uint32_t compatible = 0U;
    uint32_t lengths_match = 0U;
    uint32_t encoding = 0U;
    uint32_t replay_class = 0U;
    uint32_t stable_percent = 0U;
    uint32_t frame_count = 0U;
    uint32_t reference_points = 0U;
    uint32_t aligned_points = 0U;
    uint32_t stable_points = 0U;
    uint32_t changing_points = 0U;
    uint32_t cluster_count = 0U;
    bool success = false;
    do {
        if(!flipper_format_file_open_existing(format, TUMOSPECTRUM_LATEST_SET) ||
           !flipper_format_read_header(format, header, &version) || version != 1U ||
           furi_string_cmp_str(header, "TumoSpectrum Capture Set") != 0 ||
           !flipper_format_read_string(format, "Device", value)) {
            break;
        }
        strlcpy(
            capture_set->device_label,
            furi_string_get_cstr(value),
            sizeof(capture_set->device_label));
        if(!flipper_format_read_string(format, "Control", value)) break;
        strlcpy(
            capture_set->control_label,
            furi_string_get_cstr(value),
            sizeof(capture_set->control_label));
        if(!flipper_format_read_uint32(format, "Capture type", &capture_type, 1U) ||
           !flipper_format_read_uint32(format, "Sample count", &sample_count, 1U) ||
           sample_count < TUMOSPECTRUM_SET_MIN_SAMPLES ||
           sample_count > TUMOSPECTRUM_SET_MAX_SAMPLES ||
           (capture_type != TumoSpectrumCaptureSubGhzRaw &&
            capture_type != TumoSpectrumCaptureInfraredRaw)) {
            break;
        }
        capture_set->type = (TumoSpectrumCaptureType)capture_type;
        capture_set->sample_count = sample_count;
        bool samples_valid = true;
        for(size_t index = 0U; index < sample_count; index++) {
            char key[] = "Sample 1";
            key[7] = (char)('1' + index);
            if(!flipper_format_read_string(format, key, value)) {
                samples_valid = false;
                break;
            }
            TumoSpectrumCapture* sample = &capture_set->samples[index];
            sample->type = capture_set->type;
            sample->status = TumoSpectrumStatusOk;
            strlcpy(sample->path, furi_string_get_cstr(value), sizeof(sample->path));
            strlcpy(sample->name, tumospectrum_capture_name(sample), sizeof(sample->name));
        }
        if(!samples_valid || !flipper_format_read_uint32(format, "Compatible", &compatible, 1U) ||
           !flipper_format_read_uint32(format, "Frame lengths match", &lengths_match, 1U) ||
           !flipper_format_read_uint32(format, "Encoding", &encoding, 1U) ||
           !flipper_format_read_uint32(format, "Replay class", &replay_class, 1U) ||
           !flipper_format_read_uint32(format, "Stable percent", &stable_percent, 1U) ||
           !flipper_format_read_uint32(format, "Frame count", &frame_count, 1U) ||
           !flipper_format_read_uint32(format, "Reference points", &reference_points, 1U) ||
           !flipper_format_read_uint32(format, "Aligned points", &aligned_points, 1U) ||
           !flipper_format_read_uint32(format, "Stable points", &stable_points, 1U) ||
           !flipper_format_read_uint32(format, "Changing points", &changing_points, 1U) ||
           !flipper_format_read_uint32(format, "Cluster count", &cluster_count, 1U) ||
           encoding > TumoSpectrumEncodingManchester ||
           replay_class > TumoSpectrumReplayChanging || stable_percent > 100U ||
           cluster_count > TUMOSPECTRUM_MAX_CLUSTERS || reference_points > UINT16_MAX ||
           aligned_points > UINT16_MAX || stable_points > UINT16_MAX ||
           changing_points > UINT16_MAX || frame_count > UINT16_MAX) {
            break;
        }
        TumoSpectrumInference* inference = &capture_set->inference;
        inference->compatible = compatible != 0U;
        inference->frame_lengths_match = lengths_match != 0U;
        inference->sample_count = (uint8_t)sample_count;
        inference->encoding = (TumoSpectrumEncoding)encoding;
        inference->replay_class = (TumoSpectrumReplayClass)replay_class;
        inference->stable_percent = (uint8_t)stable_percent;
        inference->frame_count = (uint16_t)frame_count;
        inference->reference_points = (uint16_t)reference_points;
        inference->aligned_points = (uint16_t)aligned_points;
        inference->stable_points = (uint16_t)stable_points;
        inference->changing_points = (uint16_t)changing_points;
        inference->cluster_count = (uint8_t)cluster_count;
        if(cluster_count > 0U &&
           !flipper_format_read_uint32(
               format, "Clusters us", inference->clusters_us, cluster_count)) {
            break;
        }
        capture_set->inferred = true;
        success = true;
    } while(false);
    furi_string_free(value);
    furi_string_free(header);
    flipper_format_free(format);
    if(!success) memset(capture_set, 0, sizeof(*capture_set));
    return success;
}
