#include "tumospectrum_parser.h"

#include "tumospectrum_analysis.h"

#include <flipper_format/flipper_format.h>
#include <toolbox/path.h>

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define TUMOSPECTRUM_FILE_SIZE_MAX      (1024U * 1024U)
#define TUMOSPECTRUM_IR_VALUES_MAX      4096U
#define TUMOSPECTRUM_RF_NOTEBOOK_CSV \
    EXT_PATH("apps_data/arf_subghz_full/notebook/observations.csv")
#define TUMOSPECTRUM_SCOPE_CAPTURE_DIR EXT_PATH("apps_data/tumoscope/captures")
#define TUMOSPECTRUM_LINE_SIZE         192U

static void tumospectrum_capture_reset(TumoSpectrumCapture* capture, const char* path) {
    memset(capture, 0, sizeof(*capture));
    capture->status = TumoSpectrumStatusMalformed;
    if(path) strlcpy(capture->path, path, sizeof(capture->path));
    if(path && path[0]) {
        FuriString* source = furi_string_alloc_set(path);
        FuriString* filename = furi_string_alloc();
        path_extract_filename(source, filename, true);
        strlcpy(capture->name, furi_string_get_cstr(filename), sizeof(capture->name));
        furi_string_free(filename);
        furi_string_free(source);
    }
}

static TumoSpectrumStatus tumospectrum_validate_file(
    Storage* storage,
    const char* path,
    TumoSpectrumCapture* capture) {
    FileInfo info = {0};
    if(storage_common_stat(storage, path, &info) != FSE_OK) return TumoSpectrumStatusIoError;
    capture->file_size = info.size;
    if(info.size == 0U) return TumoSpectrumStatusNoData;
    if(info.size > TUMOSPECTRUM_FILE_SIZE_MAX) return TumoSpectrumStatusTooLarge;
    return TumoSpectrumStatusOk;
}

static bool tumospectrum_copy_string(
    const FuriString* source,
    char* destination,
    size_t destination_size) {
    const size_t length = furi_string_size(source);
    if(length == 0U || length >= destination_size) return false;
    strlcpy(destination, furi_string_get_cstr(source), destination_size);
    return true;
}

static bool tumospectrum_parse_i32_token(const char* token, int32_t* value) {
    if(!token || !token[0]) return false;
    errno = 0;
    char* end = NULL;
    const long parsed = strtol(token, &end, 10);
    if(errno == ERANGE || end == token || *end != '\0' || parsed < INT32_MIN || parsed > INT32_MAX) {
        return false;
    }
    *value = (int32_t)parsed;
    return true;
}

static bool tumospectrum_subghz_add_token(
    TumoSpectrumCapture* capture,
    const char* token) {
    int32_t value = 0;
    if(!tumospectrum_parse_i32_token(token, &value) || value == 0 || value == INT32_MIN) return false;
    const uint32_t magnitude = (uint32_t)abs(value);
    if(magnitude < 40U || magnitude > 1000000U) return false;
    if(capture->timing_count < TUMOSPECTRUM_MAX_TIMINGS) {
        capture->timings[capture->timing_count++] = value;
    } else {
        capture->truncated = true;
    }
    return true;
}

// Header and metadata are validated with FlipperFormat. RAW_Data is then streamed
// token by token so a long valid line never forces a line-sized heap allocation.
static TumoSpectrumStatus tumospectrum_subghz_stream_raw(
    Storage* storage,
    const char* path,
    TumoSpectrumCapture* capture) {
    static const char prefix[] = "RAW_Data:";
    File* file = storage_file_alloc(storage);
    if(!storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        storage_file_free(file);
        return TumoSpectrumStatusIoError;
    }

    bool line_start = true;
    bool data_line = false;
    bool malformed = false;
    size_t prefix_index = 0U;
    char token[16];
    size_t token_length = 0U;
    uint8_t block[96];
    size_t bytes_read = 0U;
    while(!malformed && (bytes_read = storage_file_read(file, block, sizeof(block))) > 0U) {
        for(size_t index = 0U; index < bytes_read && !malformed; index++) {
            const char value = (char)block[index];
            const bool newline = value == '\n' || value == '\r';
            if(line_start) {
                if(value == prefix[prefix_index]) {
                    prefix_index++;
                    if(prefix_index == sizeof(prefix) - 1U) {
                        data_line = true;
                        line_start = false;
                        prefix_index = 0U;
                    }
                    continue;
                }
                line_start = false;
                prefix_index = 0U;
            }

            if(data_line) {
                const bool separator = newline || value == ' ' || value == '\t';
                if(separator) {
                    if(token_length > 0U) {
                        token[token_length] = '\0';
                        malformed = !tumospectrum_subghz_add_token(capture, token);
                        token_length = 0U;
                    }
                } else if(token_length + 1U < sizeof(token)) {
                    token[token_length++] = value;
                } else {
                    malformed = true;
                }
            }

            if(newline) {
                line_start = true;
                data_line = false;
                prefix_index = 0U;
                token_length = 0U;
            }
        }
    }
    if(!malformed && data_line && token_length > 0U) {
        token[token_length] = '\0';
        malformed = !tumospectrum_subghz_add_token(capture, token);
    }
    const bool io_error = storage_file_get_error(file) != FSE_OK;
    storage_file_close(file);
    storage_file_free(file);
    if(io_error) return TumoSpectrumStatusIoError;
    if(malformed) return TumoSpectrumStatusMalformed;
    return capture->timing_count > 0U ? TumoSpectrumStatusOk : TumoSpectrumStatusNoData;
}

TumoSpectrumStatus tumospectrum_parse_subghz(
    Storage* storage,
    const char* path,
    TumoSpectrumCapture* capture) {
    if(!storage || !path || !capture) return TumoSpectrumStatusMalformed;
    tumospectrum_capture_reset(capture, path);
    TumoSpectrumStatus status = tumospectrum_validate_file(storage, path, capture);
    if(status != TumoSpectrumStatusOk) return capture->status = status;

    FlipperFormat* format = flipper_format_file_alloc(storage);
    FuriString* header = furi_string_alloc();
    FuriString* value = furi_string_alloc();
    uint32_t version = 0U;
    bool is_raw = false;
    do {
        if(!flipper_format_file_open_existing(format, path) ||
           !flipper_format_read_header(format, header, &version) || version != 1U) {
            status = TumoSpectrumStatusMalformed;
            break;
        }
        is_raw = furi_string_cmp_str(header, "Flipper SubGhz RAW File") == 0;
        const bool is_decoded = furi_string_cmp_str(header, "Flipper SubGhz Key File") == 0;
        if(!is_raw && !is_decoded) {
            status = TumoSpectrumStatusUnsupported;
            break;
        }
        capture->version = version;
        capture->type = is_raw ? TumoSpectrumCaptureSubGhzRaw : TumoSpectrumCaptureSubGhzDecoded;
        if(!flipper_format_read_uint32(format, "Frequency", &capture->frequency_hz, 1U) ||
           !flipper_format_read_string(format, "Preset", value) ||
           !tumospectrum_copy_string(value, capture->preset, sizeof(capture->preset)) ||
           !flipper_format_read_string(format, "Protocol", value) ||
           !tumospectrum_copy_string(value, capture->protocol, sizeof(capture->protocol))) {
            status = TumoSpectrumStatusMalformed;
            break;
        }
        if(is_raw && strcmp(capture->protocol, "RAW") != 0) {
            status = TumoSpectrumStatusMalformed;
            break;
        }
        status = TumoSpectrumStatusOk;
    } while(false);

    furi_string_free(value);
    furi_string_free(header);
    flipper_format_free(format);
    if(status == TumoSpectrumStatusOk && is_raw) {
        status = tumospectrum_subghz_stream_raw(storage, path, capture);
    }
    capture->status = status;
    if(status == TumoSpectrumStatusOk) tumospectrum_analyze(capture);
    return status;
}

TumoSpectrumStatus tumospectrum_parse_infrared(
    Storage* storage,
    const char* path,
    TumoSpectrumCapture* capture) {
    if(!storage || !path || !capture) return TumoSpectrumStatusMalformed;
    tumospectrum_capture_reset(capture, path);
    TumoSpectrumStatus status = tumospectrum_validate_file(storage, path, capture);
    if(status != TumoSpectrumStatusOk) return capture->status = status;

    FlipperFormat* format = flipper_format_buffered_file_alloc(storage);
    FuriString* header = furi_string_alloc();
    FuriString* value = furi_string_alloc();
    FuriString* signal_name = furi_string_alloc();
    uint32_t version = 0U;
    bool saw_raw = false;
    bool saw_parsed = false;
    do {
        if(!flipper_format_buffered_file_open_existing(format, path) ||
           !flipper_format_read_header(format, header, &version) || version != 1U) {
            status = TumoSpectrumStatusMalformed;
            break;
        }
        if(furi_string_cmp_str(header, "IR signals file") != 0) {
            status = TumoSpectrumStatusUnsupported;
            break;
        }
        capture->version = version;
        status = TumoSpectrumStatusNoData;
        while(capture->signal_count < 128U &&
              flipper_format_read_string(format, "name", signal_name)) {
            capture->signal_count++;
            if(capture->name[0] == '\0') {
                tumospectrum_copy_string(signal_name, capture->name, sizeof(capture->name));
            }
            if(!flipper_format_read_string(format, "type", value)) {
                status = TumoSpectrumStatusMalformed;
                break;
            }
            if(furi_string_cmp_str(value, "raw") == 0) {
                uint32_t frequency = 0U;
                float duty = 0.0f;
                uint32_t count = 0U;
                if(!flipper_format_read_uint32(format, "frequency", &frequency, 1U) ||
                   !flipper_format_read_float(format, "duty_cycle", &duty, 1U) ||
                   !flipper_format_get_value_count(format, "data", &count) || count == 0U ||
                   count > TUMOSPECTRUM_IR_VALUES_MAX) {
                    status = count > TUMOSPECTRUM_IR_VALUES_MAX ? TumoSpectrumStatusTooLarge :
                                                                  TumoSpectrumStatusMalformed;
                    break;
                }
                uint32_t* timings = malloc(sizeof(uint32_t) * count);
                if(!timings) {
                    status = TumoSpectrumStatusOutOfMemory;
                    break;
                }
                const bool read = flipper_format_read_uint32(format, "data", timings, count);
                if(read && !saw_raw) {
                    capture->frequency_hz = frequency;
                    capture->duty_cycle = duty;
                    const size_t copy_count = count < TUMOSPECTRUM_MAX_TIMINGS ?
                                                  count :
                                                  TUMOSPECTRUM_MAX_TIMINGS;
                    for(size_t index = 0U; index < copy_count; index++) {
                        if(timings[index] == 0U || timings[index] > 1000000U) {
                            status = TumoSpectrumStatusMalformed;
                            break;
                        }
                        capture->timings[index] = index % 2U == 0U ? (int32_t)timings[index] :
                                                                    -(int32_t)timings[index];
                    }
                    capture->timing_count = copy_count;
                    capture->truncated = count > copy_count;
                }
                free(timings);
                if(!read || status == TumoSpectrumStatusMalformed) {
                    status = TumoSpectrumStatusMalformed;
                    break;
                }
                saw_raw = true;
            } else if(furi_string_cmp_str(value, "parsed") == 0) {
                if(!flipper_format_read_string(format, "protocol", value) ||
                   (!saw_parsed &&
                    !tumospectrum_copy_string(value, capture->protocol, sizeof(capture->protocol)))) {
                    status = TumoSpectrumStatusMalformed;
                    break;
                }
                uint8_t ignored[4];
                if(!flipper_format_read_hex(format, "address", ignored, sizeof(ignored)) ||
                   !flipper_format_read_hex(format, "command", ignored, sizeof(ignored))) {
                    status = TumoSpectrumStatusMalformed;
                    break;
                }
                capture->decoded_count++;
                saw_parsed = true;
            } else {
                status = TumoSpectrumStatusUnsupported;
                break;
            }
            status = TumoSpectrumStatusOk;
        }
        if(status != TumoSpectrumStatusOk) break;
        capture->type = saw_raw ? TumoSpectrumCaptureInfraredRaw :
                                  TumoSpectrumCaptureInfraredParsed;
        if(saw_raw && saw_parsed) strlcpy(capture->detail, "Mixed remote", sizeof(capture->detail));
    } while(false);

    furi_string_free(signal_name);
    furi_string_free(value);
    furi_string_free(header);
    flipper_format_free(format);
    capture->status = status;
    if(status == TumoSpectrumStatusOk && saw_raw) tumospectrum_analyze(capture);
    return status;
}

static bool tumospectrum_read_line(File* file, char* output, size_t output_size, bool* overflow) {
    size_t length = 0U;
    bool read_any = false;
    *overflow = false;
    char value = '\0';
    while(storage_file_read(file, &value, 1U) == 1U) {
        read_any = true;
        if(value == '\n') break;
        if(value == '\r') continue;
        if(length + 1U < output_size) {
            output[length++] = value;
        } else {
            *overflow = true;
        }
    }
    output[length] = '\0';
    return read_any;
}

TumoSpectrumStatus tumospectrum_parse_tumoscope(
    Storage* storage,
    const char* path,
    TumoSpectrumCapture* capture) {
    if(!storage || !path || !capture) return TumoSpectrumStatusMalformed;
    tumospectrum_capture_reset(capture, path);
    TumoSpectrumStatus status = tumospectrum_validate_file(storage, path, capture);
    if(status != TumoSpectrumStatusOk) return capture->status = status;

    File* file = storage_file_alloc(storage);
    if(!storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        storage_file_free(file);
        return capture->status = TumoSpectrumStatusIoError;
    }
    capture->type = TumoSpectrumCaptureTumoScope;
    strlcpy(capture->protocol, "PC0 edges", sizeof(capture->protocol));
    bool header_seen = false;
    bool definitions_seen = false;
    bool level_known = false;
    bool level = false;
    uint64_t timestamp_ns = 0U;
    uint64_t previous_edge_ns = 0U;
    char line[TUMOSPECTRUM_LINE_SIZE];
    bool overflow = false;
    while(tumospectrum_read_line(file, line, sizeof(line), &overflow)) {
        if(overflow) {
            status = TumoSpectrumStatusMalformed;
            break;
        }
        if(strncmp(line, "$timescale 1 ns", 15U) == 0) header_seen = true;
        if(strcmp(line, "$enddefinitions $end") == 0) definitions_seen = true;
        if(line[0] == '#') {
            errno = 0;
            char* end = NULL;
            const unsigned long long parsed = strtoull(line + 1U, &end, 10);
            if(errno == ERANGE || end == line + 1U || *end != '\0') {
                status = TumoSpectrumStatusMalformed;
                break;
            }
            timestamp_ns = (uint64_t)parsed;
        } else if((line[0] == '0' || line[0] == '1') && line[1] == '!' && line[2] == '\0') {
            const bool next_level = line[0] == '1';
            if(level_known && next_level != level) {
                if(timestamp_ns < previous_edge_ns) {
                    status = TumoSpectrumStatusMalformed;
                    break;
                }
                uint64_t delta_us = (timestamp_ns - previous_edge_ns + 500U) / 1000U;
                if(delta_us == 0U) delta_us = 1U;
                if(delta_us > 1000000U) delta_us = 1000000U;
                if(capture->timing_count < TUMOSPECTRUM_MAX_TIMINGS) {
                    capture->timings[capture->timing_count++] =
                        level ? (int32_t)delta_us : -(int32_t)delta_us;
                } else {
                    capture->truncated = true;
                }
            }
            level_known = true;
            level = next_level;
            previous_edge_ns = timestamp_ns;
        }
    }
    if(storage_file_get_error(file) != FSE_OK) status = TumoSpectrumStatusIoError;
    storage_file_close(file);
    storage_file_free(file);
    if(status == TumoSpectrumStatusOk && (!header_seen || !definitions_seen)) {
        status = TumoSpectrumStatusUnsupported;
    } else if(status == TumoSpectrumStatusOk && capture->timing_count == 0U) {
        status = TumoSpectrumStatusNoData;
    }
    capture->status = status;
    if(status == TumoSpectrumStatusOk) tumospectrum_analyze(capture);
    return status;
}

static void tumospectrum_csv_field(const char** cursor, char* output, size_t output_size) {
    size_t index = 0U;
    while(**cursor && **cursor != ',' && **cursor != '\n' && **cursor != '\r') {
        if(index + 1U < output_size) output[index++] = **cursor;
        (*cursor)++;
    }
    output[index] = '\0';
    if(**cursor == ',') (*cursor)++;
}

TumoSpectrumStatus tumospectrum_import_frequency_observation(
    Storage* storage,
    TumoSpectrumCapture* capture) {
    if(!storage || !capture) return TumoSpectrumStatusMalformed;
    tumospectrum_capture_reset(capture, TUMOSPECTRUM_RF_NOTEBOOK_CSV);
    capture->type = TumoSpectrumCaptureFrequencyObservation;
    strlcpy(capture->name, "Frequency Analyzer", sizeof(capture->name));
    File* file = storage_file_alloc(storage);
    if(!storage_file_open(file, TUMOSPECTRUM_RF_NOTEBOOK_CSV, FSAM_READ, FSOM_OPEN_EXISTING)) {
        storage_file_free(file);
        return capture->status = TumoSpectrumStatusNoData;
    }
    char line[TUMOSPECTRUM_LINE_SIZE];
    char latest[TUMOSPECTRUM_LINE_SIZE] = "";
    bool overflow = false;
    while(tumospectrum_read_line(file, line, sizeof(line), &overflow)) {
        if(overflow) {
            capture->status = TumoSpectrumStatusMalformed;
            break;
        }
        if(line[0] && strncmp(line, "timestamp,", 10U) != 0) strlcpy(latest, line, sizeof(latest));
    }
    const bool io_error = storage_file_get_error(file) != FSE_OK;
    storage_file_close(file);
    storage_file_free(file);
    if(io_error) return capture->status = TumoSpectrumStatusIoError;
    if(capture->status == TumoSpectrumStatusMalformed) return capture->status;
    if(!latest[0]) return capture->status = TumoSpectrumStatusNoData;

    const char* cursor = latest;
    char ignored[48];
    char frequency[24];
    char rssi[24];
    tumospectrum_csv_field(&cursor, ignored, sizeof(ignored));
    tumospectrum_csv_field(&cursor, capture->protocol, sizeof(capture->protocol));
    tumospectrum_csv_field(&cursor, frequency, sizeof(frequency));
    tumospectrum_csv_field(&cursor, rssi, sizeof(rssi));
    for(size_t index = 0U; index < 2U; index++) {
        tumospectrum_csv_field(&cursor, ignored, sizeof(ignored));
    }
    tumospectrum_csv_field(&cursor, capture->preset, sizeof(capture->preset));
    for(size_t index = 0U; index < 4U; index++) {
        tumospectrum_csv_field(&cursor, ignored, sizeof(ignored));
    }
    tumospectrum_csv_field(&cursor, capture->note, sizeof(capture->note));
    errno = 0;
    char* end = NULL;
    const unsigned long parsed = strtoul(frequency, &end, 10);
    if(errno == ERANGE || end == frequency || *end != '\0' || parsed > UINT32_MAX) {
        return capture->status = TumoSpectrumStatusMalformed;
    }
    capture->frequency_hz = (uint32_t)parsed;
    snprintf(capture->detail, sizeof(capture->detail), "%s dBm", rssi[0] ? rssi : "--");
    return capture->status = TumoSpectrumStatusOk;
}
