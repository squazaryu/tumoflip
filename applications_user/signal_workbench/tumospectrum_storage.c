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

static bool tumospectrum_validate_persisted_inference(const TumoSpectrumInference* inference) {
    if(inference->bit_count > TUMOSPECTRUM_MAX_BITS ||
       (uint16_t)inference->stable_bits + inference->changing_bits + inference->unknown_bits !=
           inference->bit_count ||
       inference->counter_direction > TumoSpectrumCounterDecrementing ||
       inference->checksum_candidates >
           (TumoSpectrumChecksumXor8 | TumoSpectrumChecksumSum8 | TumoSpectrumChecksumCrc8Poly07 |
            TumoSpectrumChecksumCrc8Poly31)) {
        return false;
    }
    uint8_t stable = 0U;
    uint8_t changing = 0U;
    for(size_t index = 0U; index < inference->bit_count; index++) {
        const bool stable_bit = tumospectrum_bitset_get(inference->stable_mask, index);
        const bool changing_bit = tumospectrum_bitset_get(inference->changing_mask, index);
        const bool known_bit = tumospectrum_bitset_get(inference->known_mask, index);
        if(stable_bit && changing_bit) return false;
        if(known_bit != (stable_bit || changing_bit)) return false;
        stable += stable_bit ? 1U : 0U;
        changing += changing_bit ? 1U : 0U;
    }
    if(stable != inference->stable_bits || changing != inference->changing_bits) return false;
    if(inference->counter_direction != TumoSpectrumCounterNone &&
       (inference->counter_length < 2U ||
        inference->counter_length > TUMOSPECTRUM_COUNTER_MAX_BITS ||
        (uint16_t)inference->counter_start + inference->counter_length > inference->bit_count)) {
        return false;
    }
    if(inference->checksum_candidates != 0U &&
       ((uint16_t)inference->checksum_start + 8U > inference->bit_count)) {
        return false;
    }
    return true;
}

static void tumospectrum_append_bit_string(
    FuriString* output,
    const TumoSpectrumInference* inference,
    bool pattern) {
    furi_string_push_back(output, '"');
    for(size_t index = 0U; index < inference->bit_count; index++) {
        char value = '?';
        if(tumospectrum_bitset_get(inference->stable_mask, index)) {
            value = tumospectrum_bitset_get(inference->reference_bits, index) ? '1' : '0';
        } else if(tumospectrum_bitset_get(inference->changing_mask, index)) {
            value = pattern ?
                        '*' :
                        (tumospectrum_bitset_get(inference->reference_bits, index) ? '1' : '0');
        }
        furi_string_push_back(output, value);
    }
    furi_string_push_back(output, '"');
}

static void tumospectrum_append_checksum_json(FuriString* output, uint8_t candidates) {
    const struct {
        uint8_t flag;
        const char* name;
    } names[] = {
        {TumoSpectrumChecksumXor8, "xor8"},
        {TumoSpectrumChecksumSum8, "sum8"},
        {TumoSpectrumChecksumCrc8Poly07, "crc8-07"},
        {TumoSpectrumChecksumCrc8Poly31, "crc8-31"},
    };
    furi_string_push_back(output, '[');
    bool first = true;
    for(size_t index = 0U; index < sizeof(names) / sizeof(names[0]); index++) {
        if((candidates & names[index].flag) == 0U) continue;
        if(!first) furi_string_push_back(output, ',');
        tumospectrum_append_json_string(output, names[index].name);
        first = false;
    }
    furi_string_push_back(output, ']');
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
    furi_string_cat(output, "],\"bitstream\":{\"bit_count\":");
    furi_string_cat_printf(output, "%u,\"reference\":", capture_set->inference.bit_count);
    tumospectrum_append_bit_string(output, &capture_set->inference, false);
    furi_string_cat(output, ",\"pattern\":");
    tumospectrum_append_bit_string(output, &capture_set->inference, true);
    furi_string_cat_printf(
        output,
        ",\"stable_bits\":%u,\"changing_bits\":%u,\"unknown_bits\":%u},\"fields\":[",
        capture_set->inference.stable_bits,
        capture_set->inference.changing_bits,
        capture_set->inference.unknown_bits);
    for(uint8_t index = 0U; index < capture_set->inference.field_count; index++) {
        const TumoSpectrumField* field = &capture_set->inference.fields[index];
        furi_string_cat_printf(output, "%s{\"kind\":", index == 0U ? "" : ",");
        tumospectrum_append_json_string(output, tumospectrum_field_kind_name(field->kind));
        furi_string_cat_printf(
            output, ",\"start\":%u,\"length\":%u}", field->start, field->length);
    }
    furi_string_cat_printf(
        output,
        "],\"counter\":{\"found\":%s,\"direction\":",
        capture_set->inference.counter_direction == TumoSpectrumCounterNone ? "false" : "true");
    tumospectrum_append_json_string(
        output, tumospectrum_counter_direction_name(capture_set->inference.counter_direction));
    furi_string_cat_printf(
        output,
        ",\"start\":%u,\"length\":%u,\"confidence\":%u},\"checksum\":{\"candidates\":",
        capture_set->inference.counter_start,
        capture_set->inference.counter_length,
        capture_set->inference.counter_confidence);
    tumospectrum_append_checksum_json(output, capture_set->inference.checksum_candidates);
    furi_string_cat_printf(
        output,
        ",\"start\":%u,\"length\":%u,\"confidence\":%u}},\"samples\":[",
        capture_set->inference.checksum_start,
        capture_set->inference.checksum_candidates == 0U ? 0U : 8U,
        capture_set->inference.checksum_confidence);
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
    uint32_t bit_count = capture_set->inference.bit_count;
    uint32_t stable_bits = capture_set->inference.stable_bits;
    uint32_t changing_bits = capture_set->inference.changing_bits;
    uint32_t unknown_bits = capture_set->inference.unknown_bits;
    uint32_t counter_direction = capture_set->inference.counter_direction;
    uint32_t counter_start = capture_set->inference.counter_start;
    uint32_t counter_length = capture_set->inference.counter_length;
    uint32_t counter_confidence = capture_set->inference.counter_confidence;
    uint32_t checksum_candidates = capture_set->inference.checksum_candidates;
    uint32_t checksum_start = capture_set->inference.checksum_start;
    uint32_t checksum_confidence = capture_set->inference.checksum_confidence;
    bool success = false;
    do {
        if(!flipper_format_file_open_always(format, temporary) ||
           !flipper_format_write_header_cstr(format, "TumoSpectrum Capture Set", 2U) ||
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
        if(!flipper_format_write_uint32(format, "Bit count", &bit_count, 1U) ||
           !flipper_format_write_uint32(format, "Stable bits", &stable_bits, 1U) ||
           !flipper_format_write_uint32(format, "Changing bits", &changing_bits, 1U) ||
           !flipper_format_write_uint32(format, "Unknown bits", &unknown_bits, 1U) ||
           !flipper_format_write_uint32(format, "Counter direction", &counter_direction, 1U) ||
           !flipper_format_write_uint32(format, "Counter start", &counter_start, 1U) ||
           !flipper_format_write_uint32(format, "Counter length", &counter_length, 1U) ||
           !flipper_format_write_uint32(format, "Counter confidence", &counter_confidence, 1U) ||
           !flipper_format_write_uint32(format, "Checksum candidates", &checksum_candidates, 1U) ||
           !flipper_format_write_uint32(format, "Checksum start", &checksum_start, 1U) ||
           !flipper_format_write_uint32(format, "Checksum confidence", &checksum_confidence, 1U)) {
            success = false;
            break;
        }
        const size_t bitset_size = (bit_count + 7U) / 8U;
        if(bitset_size > 0U &&
           (!flipper_format_write_hex(
                format, "Reference bits", capture_set->inference.reference_bits, bitset_size) ||
            !flipper_format_write_hex(
                format, "Known mask", capture_set->inference.known_mask, bitset_size) ||
            !flipper_format_write_hex(
                format, "Stable mask", capture_set->inference.stable_mask, bitset_size) ||
            !flipper_format_write_hex(
                format, "Changing mask", capture_set->inference.changing_mask, bitset_size))) {
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
    furi_string_cat_printf(
        output,
        " us\nBits: %u (stable %u, changing %u, unknown %u)\nPattern: ",
        capture_set->inference.bit_count,
        capture_set->inference.stable_bits,
        capture_set->inference.changing_bits,
        capture_set->inference.unknown_bits);
    for(size_t index = 0U; index < capture_set->inference.bit_count; index++) {
        char value = '?';
        if(tumospectrum_bitset_get(capture_set->inference.stable_mask, index)) {
            value = tumospectrum_bitset_get(capture_set->inference.reference_bits, index) ? '1' :
                                                                                            '0';
        } else if(tumospectrum_bitset_get(capture_set->inference.changing_mask, index)) {
            value = '*';
        }
        furi_string_push_back(output, value);
    }
    furi_string_cat(output, "\nFields:");
    for(uint8_t index = 0U; index < capture_set->inference.field_count; index++) {
        const TumoSpectrumField* field = &capture_set->inference.fields[index];
        furi_string_cat_printf(
            output,
            " %s[%u:%u]",
            tumospectrum_field_kind_name(field->kind),
            field->start,
            field->length);
    }
    furi_string_cat_printf(
        output,
        "\nCounter candidate: %s",
        tumospectrum_counter_direction_name(capture_set->inference.counter_direction));
    if(capture_set->inference.counter_direction != TumoSpectrumCounterNone) {
        furi_string_cat_printf(
            output,
            " [%u:%u], %u%%",
            capture_set->inference.counter_start,
            capture_set->inference.counter_length,
            capture_set->inference.counter_confidence);
    }
    char checksum[48];
    tumospectrum_format_checksum_candidates(
        capture_set->inference.checksum_candidates, checksum, sizeof(checksum));
    furi_string_cat_printf(output, "\nChecksum candidates: %s\n\nSamples:\n", checksum);
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
    uint32_t bit_count = 0U;
    uint32_t stable_bits = 0U;
    uint32_t changing_bits = 0U;
    uint32_t unknown_bits = 0U;
    uint32_t counter_direction = 0U;
    uint32_t counter_start = 0U;
    uint32_t counter_length = 0U;
    uint32_t counter_confidence = 0U;
    uint32_t checksum_candidates = 0U;
    uint32_t checksum_start = 0U;
    uint32_t checksum_confidence = 0U;
    bool success = false;
    do {
        if(!flipper_format_file_open_existing(format, TUMOSPECTRUM_LATEST_SET) ||
           !flipper_format_read_header(format, header, &version) ||
           (version != 1U && version != 2U) ||
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
        if(version >= 2U) {
            if(!flipper_format_read_uint32(format, "Bit count", &bit_count, 1U) ||
               !flipper_format_read_uint32(format, "Stable bits", &stable_bits, 1U) ||
               !flipper_format_read_uint32(format, "Changing bits", &changing_bits, 1U) ||
               !flipper_format_read_uint32(format, "Unknown bits", &unknown_bits, 1U) ||
               !flipper_format_read_uint32(format, "Counter direction", &counter_direction, 1U) ||
               !flipper_format_read_uint32(format, "Counter start", &counter_start, 1U) ||
               !flipper_format_read_uint32(format, "Counter length", &counter_length, 1U) ||
               !flipper_format_read_uint32(format, "Counter confidence", &counter_confidence, 1U) ||
               !flipper_format_read_uint32(
                   format, "Checksum candidates", &checksum_candidates, 1U) ||
               !flipper_format_read_uint32(format, "Checksum start", &checksum_start, 1U) ||
               !flipper_format_read_uint32(
                   format, "Checksum confidence", &checksum_confidence, 1U) ||
               bit_count > TUMOSPECTRUM_MAX_BITS || stable_bits > TUMOSPECTRUM_MAX_BITS ||
               changing_bits > TUMOSPECTRUM_MAX_BITS || unknown_bits > TUMOSPECTRUM_MAX_BITS ||
               counter_start > UINT8_MAX || counter_length > UINT8_MAX ||
               counter_confidence > 100U || checksum_start > UINT8_MAX ||
               checksum_confidence > 100U) {
                break;
            }
            inference->bit_count = (uint8_t)bit_count;
            inference->stable_bits = (uint8_t)stable_bits;
            inference->changing_bits = (uint8_t)changing_bits;
            inference->unknown_bits = (uint8_t)unknown_bits;
            inference->counter_direction = (TumoSpectrumCounterDirection)counter_direction;
            inference->counter_start = (uint8_t)counter_start;
            inference->counter_length = (uint8_t)counter_length;
            inference->counter_confidence = (uint8_t)counter_confidence;
            inference->checksum_candidates = (uint8_t)checksum_candidates;
            inference->checksum_start = (uint8_t)checksum_start;
            inference->checksum_confidence = (uint8_t)checksum_confidence;
            const size_t bitset_size = (bit_count + 7U) / 8U;
            if(bitset_size > 0U &&
               (!flipper_format_read_hex(
                    format, "Reference bits", inference->reference_bits, bitset_size) ||
                !flipper_format_read_hex(
                    format, "Known mask", inference->known_mask, bitset_size) ||
                !flipper_format_read_hex(
                    format, "Stable mask", inference->stable_mask, bitset_size) ||
                !flipper_format_read_hex(
                    format, "Changing mask", inference->changing_mask, bitset_size))) {
                break;
            }
            if(!tumospectrum_validate_persisted_inference(inference)) break;
            tumospectrum_inference_rebuild_fields(inference);
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
