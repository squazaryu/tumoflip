#include "tumospectrum_storage.h"

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
           tumospectrum_mkdir(storage, TUMOSPECTRUM_NOTEBOOK_DIR);
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
        success = storage_file_write(file, text, text_size) == text_size && storage_file_sync(file);
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
    bool success = storage_file_open(file, TUMOSPECTRUM_NOTEBOOK_CSV, FSAM_WRITE, FSOM_OPEN_APPEND);
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
            if(!file_info_is_dir(&info) && length > 4U && strcmp(name + length - 4U, ".txt") == 0 &&
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
    while(success && total < 4096U && (count = storage_file_read(file, block, sizeof(block))) > 0U) {
        const size_t copy = total + count > 4096U ? 4096U - total : count;
        furi_string_cat_printf(output, "%.*s", (int)copy, block);
        total += copy;
    }
    success = success && storage_file_get_error(file) == FSE_OK && total > 0U;
    storage_file_close(file);
    storage_file_free(file);
    return success;
}
