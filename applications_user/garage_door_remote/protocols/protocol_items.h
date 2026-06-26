// protocols/protocol_items.h
#pragma once

#include <lib/subghz/types.h>

#include "keeloq.h"

typedef enum {
    ProtoPirateProtocolRegistryFilterAM = 0,
    ProtoPirateProtocolRegistryFilterFM,
} ProtoPirateProtocolRegistryFilter;

ProtoPirateProtocolRegistryFilter protopirate_get_protocol_registry_filter_for_preset(
    const uint8_t* preset_data,
    size_t preset_data_size);

const char*
    protopirate_get_protocol_registry_filter_name(ProtoPirateProtocolRegistryFilter filter);

#ifdef ENABLE_TIMING_TUNER_SCENE
// Timing information for protocol analysis
typedef struct {
    const char* name;
    uint32_t te_short;
    uint32_t te_long;
    uint32_t te_delta;
    uint32_t min_count_bit;
} ProtoPirateProtocolTiming;

// Get timing info for a protocol by name (returns NULL if not found)
const ProtoPirateProtocolTiming* protopirate_get_protocol_timing(const char* protocol_name);

// Get timing info by index (for iteration)
const ProtoPirateProtocolTiming* protopirate_get_protocol_timing_by_index(size_t index);

// Get number of protocols with timing info
size_t protopirate_get_protocol_timing_count(void);
#endif
