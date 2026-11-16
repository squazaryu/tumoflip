#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

typedef struct {
    uint32_t frequency;
    const char* mod;
    const char* name;
} SubGhzManualBand;

// Canonical presets only: no profile-specific automotive modulation.
static const SubGhzManualBand subghz_manual_bands[] = {
    {433920000, "AM650", "433.92 AM650"},
    {433920000, "FM12K", "433.92 FM12K"},
    {433420000, "AM650", "433.42 AM650"},
    {434420000, "FM476", "434.42 FM476"},
    {434420000, "FM12K", "434.42 FM12K"},
    {868350000, "AM650", "868.35 AM650"},
    {868000000, "AM650", "868.00 AM650"},
    {868460000, "AM650", "868.46 AM650"},
    {868800000, "FM476", "868.80 FM476"},
    {868800000, "FM12K", "868.80 FM12K"},
    {300000000, "AM650", "300.00 AM650"},
    {310000000, "AM650", "310.00 AM650"},
    {315000000, "AM650", "315.00 AM650"},
    {390000000, "AM650", "390.00 AM650"},
    {430500000, "AM650", "430.50 AM650"},
};

typedef struct {
    uint16_t te_min, te_max, gt_min, gt_max;
} SubGhzManualTiming;

enum {
    SubGhzManualEventNumberDone = 0x7E10,
    SubGhzManualEventContinue
};

static inline size_t subghz_manual_band_count(void) {
    return sizeof(subghz_manual_bands) / sizeof(subghz_manual_bands[0]);
}

static inline const SubGhzManualBand* subghz_manual_band_get(unsigned index) {
    return index < subghz_manual_band_count() ? &subghz_manual_bands[index] : NULL;
}

static inline int subghz_manual_band_index(uint32_t frequency, const char* mod) {
    if(!mod) return -1;
    for(size_t i = 0; i < subghz_manual_band_count(); ++i) {
        if(subghz_manual_bands[i].frequency == frequency &&
           strcmp(subghz_manual_bands[i].mod, mod) == 0)
            return (int)i;
    }
    return -1;
}

static inline bool subghz_manual_timing(const char* name, SubGhzManualTiming* limits) {
    if(!limits) return false;
    *limits = (SubGhzManualTiming){0};
    if(!name) return false;
    if(strcmp(name, "Princeton") == 0) {
        *limits = (SubGhzManualTiming){109, 689, 15, 72};
        return true;
    }
    if(strcmp(name, "Faac TM") == 0) {
        *limits = (SubGhzManualTiming){101, 999, 0, 0};
        return true;
    }
    return false;
}

static inline bool subghz_manual_value_valid(int32_t value, uint16_t min, uint16_t max) {
    return min > 0 && min < max && value >= min && value <= max;
}
