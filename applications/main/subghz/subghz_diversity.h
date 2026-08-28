#pragma once

#include "subghz_history.h"
#include "helpers/subghz_types.h"

typedef enum {
    SubGhzHistoryAddResultRejected,
    SubGhzHistoryAddResultDuplicate,
    SubGhzHistoryAddResultUpdated,
    SubGhzHistoryAddResultAdded,
} SubGhzHistoryAddResult;

SubGhzHistoryAddResult subghz_history_add_to_history_with_source(
    SubGhzHistory* instance,
    void* context,
    SubGhzRadioPreset* preset,
    SubGhzRadioDeviceType source,
    float rssi,
    uint32_t air_time_ms);
