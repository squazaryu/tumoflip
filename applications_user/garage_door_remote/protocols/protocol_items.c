#include "protocol_items.h"
#include <furi.h>
#ifdef ENABLE_TIMING_TUNER_SCENE
#include <string.h>
#endif

#define TAG "ProtoPirateRegistry"

#define PROTOPIRATE_CC1101_REG_MDMCFG2        0x12U
#define PROTOPIRATE_CC1101_MOD_FORMAT_MASK    0x70U
#define PROTOPIRATE_CC1101_MOD_FORMAT_2FSK    0x00U
#define PROTOPIRATE_CC1101_MOD_FORMAT_GFSK    0x10U
#define PROTOPIRATE_CC1101_MOD_FORMAT_ASK_OOK 0x30U
#define PROTOPIRATE_CC1101_MOD_FORMAT_4FSK    0x40U
#define PROTOPIRATE_CC1101_MOD_FORMAT_MSK     0x70U

static bool protopirate_preset_try_get_register(
    const uint8_t* preset_data,
    size_t preset_data_size,
    uint8_t reg,
    uint8_t* value) {
    if(!preset_data || !value || (preset_data_size < 2U)) {
        return false;
    }

    for(size_t i = 0; i + 1U < preset_data_size; i += 2U) {
        const uint8_t address = preset_data[i];
        const uint8_t data = preset_data[i + 1U];

        if((address == 0x00U) && (data == 0x00U)) {
            break;
        }

        if(address == reg) {
            *value = data;
            return true;
        }
    }

    return false;
}

ProtoPirateProtocolRegistryFilter protopirate_get_protocol_registry_filter_for_preset(
    const uint8_t* preset_data,
    size_t preset_data_size) {
    uint8_t mdmcfg2 = 0U;

    if(!protopirate_preset_try_get_register(
           preset_data, preset_data_size, PROTOPIRATE_CC1101_REG_MDMCFG2, &mdmcfg2)) {
        FURI_LOG_W(TAG, "Preset missing MDMCFG2, defaulting to AM registry");
        return ProtoPirateProtocolRegistryFilterAM;
    }

    // MDMCFG2[6:4] stores the CC1101 modulation format.
    // ASK/OOK maps to our AM decoder set; the FSK-family formats map to FM.
    switch(mdmcfg2 & PROTOPIRATE_CC1101_MOD_FORMAT_MASK) {
    case PROTOPIRATE_CC1101_MOD_FORMAT_ASK_OOK:
        return ProtoPirateProtocolRegistryFilterAM;
    case PROTOPIRATE_CC1101_MOD_FORMAT_2FSK:
    case PROTOPIRATE_CC1101_MOD_FORMAT_GFSK:
    case PROTOPIRATE_CC1101_MOD_FORMAT_4FSK:
    case PROTOPIRATE_CC1101_MOD_FORMAT_MSK:
        return ProtoPirateProtocolRegistryFilterFM;
    default:
        FURI_LOG_W(TAG, "Unknown MDMCFG2 0x%02X, defaulting to AM registry", mdmcfg2);
        return ProtoPirateProtocolRegistryFilterAM;
    }
}

const char*
    protopirate_get_protocol_registry_filter_name(ProtoPirateProtocolRegistryFilter filter) {
    return (filter == ProtoPirateProtocolRegistryFilterFM) ? "FM" : "AM";
}

#ifdef ENABLE_TIMING_TUNER_SCENE
// Protocol timing definitions - mirrors the SubGhzBlockConst in each protocol
static const ProtoPirateProtocolTiming protocol_timings[] = {
    // Star Line: PWM 250/500µs
    {
        .name = "Star Line",
        .te_short = 250,
        .te_long = 500,
        .te_delta = 120,
        .min_count_bit = 64,
    },
    // KeeLoq: PWM 400/800µs
    {
        .name = SUBGHZ_PROTOCOL_KEELOQ_NAME,
        .te_short = 400,
        .te_long = 800,
        .te_delta = 180,
        .min_count_bit = 64,
    },
};
};

static const size_t protocol_timings_count = COUNT_OF(protocol_timings);

const ProtoPirateProtocolTiming* protopirate_get_protocol_timing(const char* protocol_name) {
    if(!protocol_name) return NULL;

    for(size_t i = 0; i < protocol_timings_count; i++) {
        // Check for exact match or if the protocol name contains our timing name
        if(strcmp(protocol_name, protocol_timings[i].name) == 0 ||
           strstr(protocol_name, protocol_timings[i].name) != NULL) {
            return &protocol_timings[i];
        }
    }

    static const struct {
        const char* alias;
        const char* canonical;
    } aliases[] = {
    };
    for(size_t a = 0; a < COUNT_OF(aliases); a++) {
        if(strstr(protocol_name, aliases[a].alias) == NULL) continue;
        for(size_t i = 0; i < protocol_timings_count; i++) {
            if(strstr(protocol_timings[i].name, aliases[a].canonical) != NULL) {
                return &protocol_timings[i];
            }
        }
    }

    return NULL;
}

const ProtoPirateProtocolTiming* protopirate_get_protocol_timing_by_index(size_t index) {
    if(index >= protocol_timings_count) return NULL;
    return &protocol_timings[index];
}

size_t protopirate_get_protocol_timing_count(void) {
    return protocol_timings_count;
}
#endif
