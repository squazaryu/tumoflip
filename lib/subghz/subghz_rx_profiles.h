#pragma once

#include "subghz_setting.h"
#include <string.h>

// Narrow adaptation of the RX profiles from ProtoPirate models.txt at
// all-the-plugins a80f75c670. These are receiver presets, not protocol detection
// or transmission claims. No model index is persisted or loaded at startup.
typedef struct {
    const char* label;
    uint32_t frequency;
    const char* preset;
} SubGhzRxProfile;

#define SUBGHZ_RX_PROFILE_COUNT 9U // Manual + eight explicit choices
#define SUBGHZ_RX_CUSTOM_NAME   "TumoHonda"

static inline const SubGhzRxProfile* subghz_rx_profile_get(unsigned index) {
    static const SubGhzRxProfile profiles[] = {
        {"433 AM650", 433920000, "AM650"},
        {"433 FM476", 433920000, "FM476"},
        {"315 AM650", 315000000, "AM650"},
        {"315 FM476", 315000000, "FM476"},
        {"Ford BA-FGX", 433920000, "AM650"},
        {"Ford SX-SYII", 433920000, "AM650"},
        {"Honda 433", 433920000, SUBGHZ_RX_CUSTOM_NAME},
        {"Honda 315", 315000000, SUBGHZ_RX_CUSTOM_NAME},
    };
    return index > 0 && index < SUBGHZ_RX_PROFILE_COUNT ? &profiles[index - 1] : NULL;
}

#ifdef __cplusplus
extern "C" {
#endif

// Private source utility, deliberately not an SDK export. External apps using
// this catalog compile subghz_rx_profiles.c into their own module.
bool subghz_rx_profiles_init(SubGhzSetting* setting);

#ifdef __cplusplus
}
#endif

static inline int subghz_rx_profile_preset_index(SubGhzSetting* setting, unsigned index) {
    const SubGhzRxProfile* profile = subghz_rx_profile_get(index);
    if(!profile) return -1;
    if(strcmp(profile->preset, SUBGHZ_RX_CUSTOM_NAME) == 0 &&
       !subghz_rx_profiles_init(setting))
        return -1;
    return subghz_setting_get_inx_preset_by_name(setting, profile->preset);
}
