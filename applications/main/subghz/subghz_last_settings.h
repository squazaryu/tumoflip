#pragma once

#include <furi_hal.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <storage/storage.h>
#include <lib/subghz/types.h>
#include <lib/subghz/protocols/plugin_registry.h>

#define SUBGHZ_LAST_SETTING_FREQUENCY_ANALYZER_TRIGGER        (-93.0f)
// 1 = "AM650"
// "AM270", "AM650", "FM238", "FM12K", "FM476",
#define SUBGHZ_LAST_SETTING_DEFAULT_PRESET                    1
#define SUBGHZ_LAST_SETTING_DEFAULT_FREQUENCY                 433920000
#define SUBGHZ_LAST_SETTING_FREQUENCY_ANALYZER_FEEDBACK_LEVEL 2
#define SUBGHZ_LAST_SETTINGS_PROTOCOL_FILTER_SIZE             1024

typedef enum {
    SubGhzHoppingModeOff,
    SubGhzHoppingModeFrequency,
    SubGhzHoppingModePreset,
    SubGhzHoppingModeCombined,
    SubGhzHoppingModeCount,
} SubGhzHoppingMode;

typedef struct {
    uint32_t frequency;
    uint32_t preset_index; // AKA Modulation
    uint32_t raw_frequency;
    uint32_t raw_preset_index; // AKA Modulation for Read RAW
    uint32_t frequency_analyzer_feedback_level;
    float frequency_analyzer_trigger;
    bool protocol_file_names;
    uint32_t ignore_filter;
    uint32_t filter;
    float rssi;
    bool delete_old_signals;
    float hopping_threshold;
    uint8_t hopping_mode;
    uint8_t protocol_pack_group;
    bool leds_and_amp;
    uint8_t tx_power;
    char protocol_filter[SUBGHZ_LAST_SETTINGS_PROTOCOL_FILTER_SIZE];
} SubGhzLastSettings;

bool subghz_last_settings_protocol_filter_contains(
    const SubGhzLastSettings* instance,
    const char* protocol);

bool subghz_last_settings_protocol_filter_normalize(SubGhzLastSettings* instance);

bool subghz_last_settings_protocol_filter_set(
    SubGhzLastSettings* instance,
    const char* protocol,
    bool disabled);

uint8_t subghz_last_settings_protocol_filter_count(const SubGhzLastSettings* instance);

SubGhzLastSettings* subghz_last_settings_alloc(void);

void subghz_last_settings_free(SubGhzLastSettings* instance);

void subghz_last_settings_load(SubGhzLastSettings* instance, size_t preset_count);

bool subghz_last_settings_save(SubGhzLastSettings* instance);
