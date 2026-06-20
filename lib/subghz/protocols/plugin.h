#pragma once

#include <flipper_application/flipper_application.h>

#include "../registry.h"

#define SUBGHZ_PROTOCOL_PLUGIN_APP_ID      "subghz_protocol"
#define SUBGHZ_PROTOCOL_PLUGIN_API_VERSION 1U

#define SUBGHZ_PROTOCOL_PLUGIN(protocol_symbol, entry_symbol)             \
    static const FlipperAppPluginDescriptor entry_symbol##_descriptor = { \
        .appid = SUBGHZ_PROTOCOL_PLUGIN_APP_ID,                           \
        .ep_api_version = SUBGHZ_PROTOCOL_PLUGIN_API_VERSION,             \
        .entry_point = &(protocol_symbol),                                \
    };                                                                    \
    const FlipperAppPluginDescriptor* entry_symbol(void) {                \
        return &entry_symbol##_descriptor;                                \
    }
