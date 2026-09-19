#pragma once

#include "subghz_setting.h"
#include <string.h>

// Narrow adaptation of the seven RX profiles from ProtoPirate models.txt at
// all-the-plugins 8970b6ba0e. These are receiver presets, not protocol detection
// or transmission claims. No model index is persisted or loaded at startup.
typedef struct {
    const char* label;
    uint32_t frequency;
    const char* preset;
} SubGhzRxProfile;

#define SUBGHZ_RX_PROFILE_COUNT 8U // Manual + seven explicit choices
#define SUBGHZ_RX_CUSTOM_NAME   "Tumo RX Honda"

static const uint8_t subghz_rx_honda_preset[] = {
    0x02, 0x0D, 0x0B, 0x06, 0x08, 0x32, 0x07, 0x04, 0x14, 0x00, 0x13, 0x02,
    0x12, 0x07, 0x11, 0x36, 0x10, 0xE9, 0x15, 0x32, 0x18, 0x18, 0x19, 0x16,
    0x1D, 0x92, 0x1C, 0x40, 0x1B, 0x03, 0x20, 0xFB, 0x22, 0x10, 0x21, 0x56,
    0x00, 0x00, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static inline const SubGhzRxProfile* subghz_rx_profile_get(unsigned index) {
    static const SubGhzRxProfile profiles[] = {
        {"433 AM650", 433920000, "AM650"},
        {"433 FM476", 433920000, "FM476"},
        {"315 AM650", 315000000, "AM650"},
        {"315 FM476", 315000000, "FM476"},
        {"Ford BA-FGX", 433920000, "AM650"},
        {"Ford SX-SYII", 433920000, "AM650"},
        {"Honda custom", 433920000, SUBGHZ_RX_CUSTOM_NAME},
    };
    return index > 0 && index < SUBGHZ_RX_PROFILE_COUNT ? &profiles[index - 1] : NULL;
}

static inline bool subghz_rx_profiles_init(SubGhzSetting* setting) {
    const int existing = subghz_setting_get_inx_preset_by_name(setting, SUBGHZ_RX_CUSTOM_NAME);
    if(existing >= 0) {
        // Never overwrite or silently adopt a user preset with the same name.
        return subghz_setting_get_preset_data_size(setting, existing) == sizeof(subghz_rx_honda_preset) &&
               memcmp(subghz_setting_get_preset_data(setting, existing), subghz_rx_honda_preset,
                      sizeof(subghz_rx_honda_preset)) == 0;
    }
    FlipperFormat* data = flipper_format_string_alloc();
    const bool ok = flipper_format_write_hex(
                        data, "Custom_preset_data", subghz_rx_honda_preset,
                        sizeof(subghz_rx_honda_preset)) &&
                    flipper_format_rewind(data) &&
                    subghz_setting_load_custom_preset(setting, SUBGHZ_RX_CUSTOM_NAME, data);
    flipper_format_free(data);
    return ok;
}

static inline int subghz_rx_profile_preset_index(SubGhzSetting* setting, unsigned index) {
    const SubGhzRxProfile* profile = subghz_rx_profile_get(index);
    if(!profile) return -1;
    if(index == 7 && !subghz_rx_profiles_init(setting)) return -1;
    return subghz_setting_get_inx_preset_by_name(setting, profile->preset);
}
