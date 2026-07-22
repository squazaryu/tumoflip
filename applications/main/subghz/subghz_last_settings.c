#include "subghz_last_settings.h"
#include "subghz_i.h"

#include <string.h>

#define TAG "SubGhzLastSettings"

#define SUBGHZ_LAST_SETTING_FILE_TYPE    "Flipper SubGhz Last Setting File"
#define SUBGHZ_LAST_SETTING_FILE_VERSION 3
#define SUBGHZ_LAST_SETTINGS_PATH        EXT_PATH("subghz/assets/last_subghz.settings")

#define SUBGHZ_LAST_SETTING_FIELD_FREQUENCY                         "Frequency"
#define SUBGHZ_LAST_SETTING_FIELD_PRESET                            "Preset" // AKA Modulation
#define SUBGHZ_LAST_SETTING_FIELD_FREQUENCY_ANALYZER_FEEDBACK_LEVEL "FeedbackLevel"
#define SUBGHZ_LAST_SETTING_FIELD_FREQUENCY_ANALYZER_TRIGGER        "FATrigger"
#define SUBGHZ_LAST_SETTING_FIELD_PROTOCOL_FILE_NAMES               "ProtocolNames"
#define SUBGHZ_LAST_SETTING_FIELD_HOPPING_MODE                      "HoppingMode"
#define SUBGHZ_LAST_SETTING_FIELD_PROTOCOL_PACK_GROUP               "ProtocolPackGroup"
#define SUBGHZ_LAST_SETTING_FIELD_IGNORE_FILTER                     "IgnoreFilter"
#define SUBGHZ_LAST_SETTING_FIELD_FILTER                            "Filter"
#define SUBGHZ_LAST_SETTING_FIELD_RSSI_THRESHOLD                    "RSSI"
#define SUBGHZ_LAST_SETTING_FIELD_DELETE_OLD                        "DelOldSignals"
#define SUBGHZ_LAST_SETTING_FIELD_HOPPING_THRESHOLD                 "HoppingThreshold"
#define SUBGHZ_LAST_SETTING_FIELD_LED_AND_POWER_AMP                 "LedAndPowerAmp"
#define SUBGHZ_LAST_SETTING_FIELD_TX_POWER                          "TXPower"
#define SUBGHZ_LAST_SETTING_FIELD_PROTOCOL_FILTER                   "ProtocolFilterOff"

static void subghz_last_settings_protocol_filter_next_token(
    const char** cursor,
    const char** token,
    size_t* token_len) {
    const char* start = *cursor;
    while((*start == ',') || (*start == ' ') || (*start == '\t')) start++;

    const char* end = start;
    while((*end != '\0') && (*end != ',')) end++;

    const char* trim_end = end;
    while((trim_end > start) && ((trim_end[-1] == ' ') || (trim_end[-1] == '\t'))) {
        trim_end--;
    }

    *token = start;
    *token_len = (size_t)(trim_end - start);
    *cursor = (*end == ',') ? end + 1 : end;
}

static bool subghz_last_settings_protocol_filter_contains_token(
    const char* filter,
    const char* token,
    size_t token_len) {
    const char* cursor = filter;
    const char* current = NULL;
    size_t current_len = 0;

    while(*cursor != '\0') {
        subghz_last_settings_protocol_filter_next_token(&cursor, &current, &current_len);
        if((current_len == token_len) && (strncmp(current, token, token_len) == 0)) return true;
    }

    return false;
}

static bool subghz_last_settings_protocol_filter_append_token(
    char* filter,
    size_t filter_size,
    const char* token,
    size_t token_len) {
    if(token_len == 0) return true;

    size_t filter_len = strlen(filter);
    const size_t separator_len = filter_len == 0 ? 0 : 1;
    if((filter_len + separator_len + token_len) >= filter_size) return false;

    if(separator_len != 0) filter[filter_len++] = ',';
    memcpy(&filter[filter_len], token, token_len);
    filter[filter_len + token_len] = '\0';
    return true;
}

bool subghz_last_settings_protocol_filter_contains(
    const SubGhzLastSettings* instance,
    const char* protocol) {
    if(!instance || !protocol || (protocol[0] == '\0')) return false;

    return subghz_last_settings_protocol_filter_contains_token(
        instance->protocol_filter, protocol, strlen(protocol));
}

bool subghz_last_settings_protocol_filter_normalize(SubGhzLastSettings* instance) {
    if(!instance) return false;

    char* normalized = calloc(1, sizeof(instance->protocol_filter));
    furi_check(normalized);
    const char* cursor = instance->protocol_filter;
    const char* token = NULL;
    size_t token_len = 0;

    while(*cursor != '\0') {
        subghz_last_settings_protocol_filter_next_token(&cursor, &token, &token_len);
        if((token_len == 0) ||
           subghz_last_settings_protocol_filter_contains_token(normalized, token, token_len)) {
            continue;
        }
        if(!subghz_last_settings_protocol_filter_append_token(
               normalized, sizeof(instance->protocol_filter), token, token_len)) {
            break;
        }
    }

    const bool changed = strcmp(instance->protocol_filter, normalized) != 0;
    if(changed) {
        memcpy(instance->protocol_filter, normalized, sizeof(instance->protocol_filter));
    }
    free(normalized);
    return changed;
}

bool subghz_last_settings_protocol_filter_set(
    SubGhzLastSettings* instance,
    const char* protocol,
    bool disabled) {
    if(!instance || !protocol || (protocol[0] == '\0')) return false;

    char* updated = calloc(1, sizeof(instance->protocol_filter));
    furi_check(updated);
    const char* cursor = instance->protocol_filter;
    const char* token = NULL;
    size_t token_len = 0;
    const size_t protocol_len = strlen(protocol);

    while(*cursor != '\0') {
        subghz_last_settings_protocol_filter_next_token(&cursor, &token, &token_len);
        if((token_len == 0) ||
           ((token_len == protocol_len) && (strncmp(token, protocol, token_len) == 0))) {
            continue;
        }
        if(!subghz_last_settings_protocol_filter_contains_token(updated, token, token_len) &&
           !subghz_last_settings_protocol_filter_append_token(
               updated, sizeof(instance->protocol_filter), token, token_len)) {
            free(updated);
            return false;
        }
    }

    if(disabled &&
       !subghz_last_settings_protocol_filter_append_token(
           updated, sizeof(instance->protocol_filter), protocol, protocol_len)) {
        free(updated);
        return false;
    }

    const bool changed = strcmp(instance->protocol_filter, updated) != 0;
    if(changed) memcpy(instance->protocol_filter, updated, sizeof(instance->protocol_filter));
    free(updated);
    return changed;
}

uint8_t subghz_last_settings_protocol_filter_count(const SubGhzLastSettings* instance) {
    if(!instance) return 0;

    const char* cursor = instance->protocol_filter;
    const char* token = NULL;
    size_t token_len = 0;
    uint8_t count = 0;
    while(*cursor != '\0') {
        subghz_last_settings_protocol_filter_next_token(&cursor, &token, &token_len);
        if((token_len != 0) && (count != UINT8_MAX)) count++;
    }
    return count;
}

SubGhzLastSettings* subghz_last_settings_alloc(void) {
    SubGhzLastSettings* instance = calloc(1, sizeof(SubGhzLastSettings));
    return instance;
}

void subghz_last_settings_free(SubGhzLastSettings* instance) {
    furi_assert(instance);
    free(instance);
}

void subghz_last_settings_load(SubGhzLastSettings* instance, size_t preset_count) {
    UNUSED(preset_count);
    furi_assert(instance);

    // Default values (all others set to 0, if read from file fails these are used)
    instance->frequency = SUBGHZ_LAST_SETTING_DEFAULT_FREQUENCY;
    instance->preset_index = SUBGHZ_LAST_SETTING_DEFAULT_PRESET;
    instance->frequency_analyzer_feedback_level =
        SUBGHZ_LAST_SETTING_FREQUENCY_ANALYZER_FEEDBACK_LEVEL;
    instance->frequency_analyzer_trigger = SUBGHZ_LAST_SETTING_FREQUENCY_ANALYZER_TRIGGER;
    // See bin_raw_value in scenes/subghz_scene_receiver_config.c
    instance->filter = SubGhzProtocolFlag_Decodable;
    instance->rssi = SUBGHZ_RAW_THRESHOLD_MIN;
    instance->hopping_threshold = -90.0f;
    instance->hopping_mode = SubGhzHoppingModeOff;
    instance->protocol_pack_group = SubGhzProtocolPackGroupCore;
    instance->leds_and_amp = true;
    instance->protocol_filter[0] = '\0';

    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* fff_data_file = flipper_format_file_alloc(storage);

    FuriString* temp_str = furi_string_alloc();
    uint32_t config_version = 0;

    if(FSE_OK == storage_sd_status(storage) &&
       flipper_format_file_open_existing(fff_data_file, SUBGHZ_LAST_SETTINGS_PATH)) {
        do {
            if(!flipper_format_read_header(fff_data_file, temp_str, &config_version)) break;
            if((strcmp(furi_string_get_cstr(temp_str), SUBGHZ_LAST_SETTING_FILE_TYPE) != 0) ||
               (config_version != SUBGHZ_LAST_SETTING_FILE_VERSION)) {
                break;
            }

            if(!flipper_format_read_uint32(
                   fff_data_file, SUBGHZ_LAST_SETTING_FIELD_FREQUENCY, &instance->frequency, 1)) {
                flipper_format_rewind(fff_data_file);
            }
            if(!flipper_format_read_uint32(
                   fff_data_file, SUBGHZ_LAST_SETTING_FIELD_PRESET, &instance->preset_index, 1)) {
                flipper_format_rewind(fff_data_file);
            }
            if(!flipper_format_read_uint32(
                   fff_data_file,
                   SUBGHZ_LAST_SETTING_FIELD_FREQUENCY_ANALYZER_FEEDBACK_LEVEL,
                   &instance->frequency_analyzer_feedback_level,
                   1)) {
                flipper_format_rewind(fff_data_file);
            }
            if(!flipper_format_read_float(
                   fff_data_file,
                   SUBGHZ_LAST_SETTING_FIELD_FREQUENCY_ANALYZER_TRIGGER,
                   &instance->frequency_analyzer_trigger,
                   1)) {
                flipper_format_rewind(fff_data_file);
            }
            if(!flipper_format_read_bool(
                   fff_data_file,
                   SUBGHZ_LAST_SETTING_FIELD_PROTOCOL_FILE_NAMES,
                   &instance->protocol_file_names,
                   1)) {
                flipper_format_rewind(fff_data_file);
            }
            uint32_t hopping_mode = SubGhzHoppingModeOff;
            if(!flipper_format_read_uint32(
                   fff_data_file, SUBGHZ_LAST_SETTING_FIELD_HOPPING_MODE, &hopping_mode, 1)) {
                flipper_format_rewind(fff_data_file);
            }
            if(hopping_mode < SubGhzHoppingModeCount) {
                instance->hopping_mode = hopping_mode;
            }
            uint32_t protocol_pack_group = SubGhzProtocolPackGroupCore;
            if(!flipper_format_read_uint32(
                   fff_data_file,
                   SUBGHZ_LAST_SETTING_FIELD_PROTOCOL_PACK_GROUP,
                   &protocol_pack_group,
                   1)) {
                flipper_format_rewind(fff_data_file);
            }
            if(protocol_pack_group < SubGhzProtocolPackGroupCount) {
                instance->protocol_pack_group = protocol_pack_group;
            }
            if(!flipper_format_read_uint32(
                   fff_data_file,
                   SUBGHZ_LAST_SETTING_FIELD_IGNORE_FILTER,
                   &instance->ignore_filter,
                   1)) {
                flipper_format_rewind(fff_data_file);
            }
            if(!flipper_format_read_uint32(
                   fff_data_file, SUBGHZ_LAST_SETTING_FIELD_FILTER, &instance->filter, 1)) {
                flipper_format_rewind(fff_data_file);
            }
            if(!flipper_format_read_float(
                   fff_data_file, SUBGHZ_LAST_SETTING_FIELD_RSSI_THRESHOLD, &instance->rssi, 1)) {
                flipper_format_rewind(fff_data_file);
            }
            if(!flipper_format_read_bool(
                   fff_data_file,
                   SUBGHZ_LAST_SETTING_FIELD_DELETE_OLD,
                   &instance->delete_old_signals,
                   1)) {
                flipper_format_rewind(fff_data_file);
            }
            uint32_t tx_power = 0;
            if(!flipper_format_read_uint32(
                   fff_data_file, SUBGHZ_LAST_SETTING_FIELD_TX_POWER, &tx_power, 1)) {
                flipper_format_rewind(fff_data_file);
            }
            instance->tx_power = (uint8_t)(tx_power & 0xFF);
            if(!flipper_format_read_float(
                   fff_data_file,
                   SUBGHZ_LAST_SETTING_FIELD_HOPPING_THRESHOLD,
                   &instance->hopping_threshold,
                   1)) {
                flipper_format_rewind(fff_data_file);
            }
            if(!flipper_format_read_bool(
                   fff_data_file,
                   SUBGHZ_LAST_SETTING_FIELD_LED_AND_POWER_AMP,
                   &instance->leds_and_amp,
                   1)) {
                flipper_format_rewind(fff_data_file);
            }
            flipper_format_rewind(fff_data_file);
            if(flipper_format_read_string(
                   fff_data_file, SUBGHZ_LAST_SETTING_FIELD_PROTOCOL_FILTER, temp_str)) {
                strlcpy(
                    instance->protocol_filter,
                    furi_string_get_cstr(temp_str),
                    sizeof(instance->protocol_filter));
                subghz_last_settings_protocol_filter_normalize(instance);
            }

        } while(0);
    } else {
        FURI_LOG_E(TAG, "Error open file %s", SUBGHZ_LAST_SETTINGS_PATH);
    }

    furi_string_free(temp_str);

    flipper_format_file_close(fff_data_file);
    flipper_format_free(fff_data_file);
    furi_record_close(RECORD_STORAGE);

    if(instance->frequency == 0 || !furi_hal_subghz_is_tx_allowed(instance->frequency)) {
        instance->frequency = SUBGHZ_LAST_SETTING_DEFAULT_FREQUENCY;
    }

    if(instance->preset_index > 4) {
        instance->preset_index = SUBGHZ_LAST_SETTING_DEFAULT_PRESET;
    }
}

bool subghz_last_settings_save(SubGhzLastSettings* instance) {
    furi_assert(instance);

    bool saved = false;
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* file = flipper_format_file_alloc(storage);

    do {
        if(FSE_OK != storage_sd_status(storage)) {
            break;
        }

        // Open file
        if(!flipper_format_file_open_always(file, SUBGHZ_LAST_SETTINGS_PATH)) break;

        // Write header
        if(!flipper_format_write_header_cstr(
               file, SUBGHZ_LAST_SETTING_FILE_TYPE, SUBGHZ_LAST_SETTING_FILE_VERSION))
            break;
        if(!flipper_format_write_uint32(
               file, SUBGHZ_LAST_SETTING_FIELD_FREQUENCY, &instance->frequency, 1)) {
            break;
        }
        if(!flipper_format_write_uint32(
               file, SUBGHZ_LAST_SETTING_FIELD_PRESET, &instance->preset_index, 1)) {
            break;
        }
        if(!flipper_format_write_uint32(
               file,
               SUBGHZ_LAST_SETTING_FIELD_FREQUENCY_ANALYZER_FEEDBACK_LEVEL,
               &instance->frequency_analyzer_feedback_level,
               1)) {
            break;
        }
        if(!flipper_format_write_float(
               file,
               SUBGHZ_LAST_SETTING_FIELD_FREQUENCY_ANALYZER_TRIGGER,
               &instance->frequency_analyzer_trigger,
               1)) {
            break;
        }
        if(!flipper_format_write_bool(
               file,
               SUBGHZ_LAST_SETTING_FIELD_PROTOCOL_FILE_NAMES,
               &instance->protocol_file_names,
               1)) {
            break;
        }
        uint32_t hopping_mode = instance->hopping_mode;
        if(!flipper_format_write_uint32(
               file, SUBGHZ_LAST_SETTING_FIELD_HOPPING_MODE, &hopping_mode, 1)) {
            break;
        }
        uint32_t protocol_pack_group = instance->protocol_pack_group;
        if(!flipper_format_write_uint32(
               file, SUBGHZ_LAST_SETTING_FIELD_PROTOCOL_PACK_GROUP, &protocol_pack_group, 1)) {
            break;
        }
        if(!flipper_format_write_uint32(
               file, SUBGHZ_LAST_SETTING_FIELD_IGNORE_FILTER, &instance->ignore_filter, 1)) {
            break;
        }
        if(!flipper_format_write_uint32(
               file, SUBGHZ_LAST_SETTING_FIELD_FILTER, &instance->filter, 1)) {
            break;
        }
        if(!flipper_format_write_float(
               file, SUBGHZ_LAST_SETTING_FIELD_RSSI_THRESHOLD, &instance->rssi, 1)) {
            break;
        }
        if(!flipper_format_write_bool(
               file, SUBGHZ_LAST_SETTING_FIELD_DELETE_OLD, &instance->delete_old_signals, 1)) {
            break;
        }
        uint32_t tx_power = instance->tx_power;
        if(!flipper_format_write_uint32(file, SUBGHZ_LAST_SETTING_FIELD_TX_POWER, &tx_power, 1)) {
            break;
        }
        if(!flipper_format_write_float(
               file,
               SUBGHZ_LAST_SETTING_FIELD_HOPPING_THRESHOLD,
               &instance->hopping_threshold,
               1)) {
            break;
        }
        if(!flipper_format_write_bool(
               file, SUBGHZ_LAST_SETTING_FIELD_LED_AND_POWER_AMP, &instance->leds_and_amp, 1)) {
            break;
        }
        if(!flipper_format_write_string_cstr(
               file,
               SUBGHZ_LAST_SETTING_FIELD_PROTOCOL_FILTER,
               instance->protocol_filter)) {
            break;
        }

        saved = true;
    } while(0);

    if(!saved) {
        FURI_LOG_E(TAG, "Error save file %s", SUBGHZ_LAST_SETTINGS_PATH);
    }

    flipper_format_file_close(file);
    flipper_format_free(file);
    furi_record_close(RECORD_STORAGE);

    return saved;
}
