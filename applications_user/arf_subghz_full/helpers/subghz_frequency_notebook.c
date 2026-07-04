#include "subghz_frequency_notebook.h"

#include <furi.h>
#include <storage/storage.h>
#include <string.h>

#define SUBGHZ_FREQUENCY_NOTEBOOK_APP_DIR EXT_PATH("apps_data/arf_subghz_full")
#define SUBGHZ_FREQUENCY_NOTEBOOK_DIR     EXT_PATH("apps_data/arf_subghz_full/notebook")
#define SUBGHZ_FREQUENCY_NOTEBOOK_CSV \
    EXT_PATH("apps_data/arf_subghz_full/notebook/observations.csv")
#define SUBGHZ_FREQUENCY_NOTEBOOK_JSONL \
    EXT_PATH("apps_data/arf_subghz_full/notebook/observations.jsonl")

static const char* subghz_frequency_notebook_csv_header =
    "timestamp,source,frequency_hz,rssi_dbm,trigger_dbm,preset,radio,rx_count,gps_fix,gps_lat,gps_lon,note\n";

static bool subghz_frequency_notebook_mkdir(Storage* storage, const char* path) {
    FS_Error error = storage_common_mkdir(storage, path);
    return error == FSE_OK || error == FSE_EXIST;
}

static void subghz_frequency_notebook_format_timestamp(FuriString* output) {
    DateTime datetime;
    furi_hal_rtc_get_datetime(&datetime);
    furi_string_printf(
        output,
        "%04u-%02u-%02uT%02u:%02u:%02u",
        datetime.year,
        datetime.month,
        datetime.day,
        datetime.hour,
        datetime.minute,
        datetime.second);
}

static void subghz_frequency_notebook_append_dbm(FuriString* output, int16_t dbm_x10) {
    int16_t value = dbm_x10;
    if(value < 0) {
        furi_string_cat(output, "-");
        value = -value;
    }

    furi_string_cat_printf(output, "%d.%d", value / 10, value % 10);
}

static const char* subghz_frequency_notebook_source_name(
    const SubGhzFrequencyAnalyzerObservation* observation) {
    return observation->source == SubGhzFrequencyAnalyzerObservationSourceHistory ? "history" :
                                                                                   "live";
}

static const char* subghz_frequency_notebook_radio_name(
    const SubGhzFrequencyAnalyzerObservation* observation) {
    return observation->is_ext_radio ? "external" : "internal";
}

static void subghz_frequency_notebook_format_note(
    FuriString* output,
    const SubGhzFrequencyAnalyzerObservation* observation) {
    if(observation->source == SubGhzFrequencyAnalyzerObservationSourceHistory) {
        furi_string_printf(output, "history_%u", observation->history_index);
    } else {
        furi_string_printf(output, "live");
    }
}

static bool subghz_frequency_notebook_write_line(
    Storage* storage,
    const char* path,
    const char* header,
    const char* line) {
    File* file = storage_file_alloc(storage);
    bool result = storage_file_open(file, path, FSAM_WRITE, FSOM_OPEN_APPEND);
    if(result) {
        if(header && storage_file_size(file) == 0) {
            size_t header_size = strlen(header);
            result = storage_file_write(file, header, header_size) == header_size;
        }

        if(result) {
            size_t line_size = strlen(line);
            result = storage_file_write(file, line, line_size) == line_size;
        }

        if(result) {
            result = storage_file_sync(file);
        }
    }

    storage_file_close(file);
    storage_file_free(file);
    return result;
}

static void subghz_frequency_notebook_build_csv_line(
    FuriString* output,
    const FuriString* timestamp,
    const FuriString* note,
    const SubGhzFrequencyAnalyzerObservation* observation) {
    furi_string_printf(
        output,
        "%s,%s,%lu,",
        furi_string_get_cstr(timestamp),
        subghz_frequency_notebook_source_name(observation),
        observation->frequency);
    subghz_frequency_notebook_append_dbm(output, observation->rssi_dbm_x10);
    furi_string_cat(output, ",");
    subghz_frequency_notebook_append_dbm(output, observation->trigger_dbm_x10);
    furi_string_cat_printf(
        output,
        ",FrequencyAnalyzer,%s,%u,false,,,",
        subghz_frequency_notebook_radio_name(observation),
        observation->rx_count);
    furi_string_cat(output, furi_string_get_cstr(note));
    furi_string_cat(output, "\n");
}

static void subghz_frequency_notebook_build_jsonl_line(
    FuriString* output,
    const FuriString* timestamp,
    const FuriString* note,
    const SubGhzFrequencyAnalyzerObservation* observation) {
    furi_string_printf(
        output,
        "{\"timestamp\":\"%s\",\"source\":\"%s\",\"frequency_hz\":%lu,\"rssi_dbm\":",
        furi_string_get_cstr(timestamp),
        subghz_frequency_notebook_source_name(observation),
        observation->frequency);
    subghz_frequency_notebook_append_dbm(output, observation->rssi_dbm_x10);
    furi_string_cat(output, ",\"trigger_dbm\":");
    subghz_frequency_notebook_append_dbm(output, observation->trigger_dbm_x10);
    furi_string_cat_printf(
        output,
        ",\"preset\":\"FrequencyAnalyzer\",\"radio\":\"%s\",\"rx_count\":%u,\"gps_fix\":false,\"gps_lat\":null,\"gps_lon\":null,\"note\":\"%s\"}\n",
        subghz_frequency_notebook_radio_name(observation),
        observation->rx_count,
        furi_string_get_cstr(note));
}

bool subghz_frequency_notebook_append(const SubGhzFrequencyAnalyzerObservation* observation) {
    furi_assert(observation);
    if(!observation->valid || observation->frequency == 0) {
        return false;
    }

    Storage* storage = furi_record_open(RECORD_STORAGE);
    bool result = subghz_frequency_notebook_mkdir(storage, EXT_PATH("apps_data"));
    result = result &&
             subghz_frequency_notebook_mkdir(storage, SUBGHZ_FREQUENCY_NOTEBOOK_APP_DIR);
    result = result && subghz_frequency_notebook_mkdir(storage, SUBGHZ_FREQUENCY_NOTEBOOK_DIR);

    if(result) {
        FuriString* timestamp = furi_string_alloc();
        FuriString* note = furi_string_alloc();
        FuriString* csv_line = furi_string_alloc();
        FuriString* jsonl_line = furi_string_alloc();

        subghz_frequency_notebook_format_timestamp(timestamp);
        subghz_frequency_notebook_format_note(note, observation);
        subghz_frequency_notebook_build_csv_line(csv_line, timestamp, note, observation);
        subghz_frequency_notebook_build_jsonl_line(jsonl_line, timestamp, note, observation);

        result = subghz_frequency_notebook_write_line(
            storage,
            SUBGHZ_FREQUENCY_NOTEBOOK_CSV,
            subghz_frequency_notebook_csv_header,
            furi_string_get_cstr(csv_line));
        result = result &&
                 subghz_frequency_notebook_write_line(
                     storage,
                     SUBGHZ_FREQUENCY_NOTEBOOK_JSONL,
                     NULL,
                     furi_string_get_cstr(jsonl_line));

        furi_string_free(jsonl_line);
        furi_string_free(csv_line);
        furi_string_free(note);
        furi_string_free(timestamp);
    }

    furi_record_close(RECORD_STORAGE);
    return result;
}
