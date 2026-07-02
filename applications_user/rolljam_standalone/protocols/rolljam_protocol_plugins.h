#pragma once

#include <lib/flipper_application/flipper_application.h>
#include <lib/subghz/types.h>
#include <storage/storage.h>
#include "protocol_items.h"

#define ROLLJAM_PROTOCOL_PLUGIN_APP_ID          "rolljam_protocol_plugin"
#define ROLLJAM_PROTOCOL_AM_PLUGIN_APP_ID       "rolljam_am_plugin"
#define ROLLJAM_PROTOCOL_FM_PLUGIN_APP_ID       "rolljam_fm_plugin"
#define ROLLJAM_PROTOCOL_FM_EXTRA_PLUGIN_APP_ID "rolljam_fm_plugin_extra"
#define ROLLJAM_PROTOCOL_PLUGIN_API_VERSION     1U

#define ROLLJAM_PLUGIN_DATA_DIR EXT_PATH("apps_data/rolljam_standalone/plugins")
#define ROLLJAM_PROTOCOL_AM_PLUGIN_PATH \
    ROLLJAM_PLUGIN_DATA_DIR "/rolljam_am_plugin.fal"
#define ROLLJAM_PROTOCOL_FM_PLUGIN_PATH \
    ROLLJAM_PLUGIN_DATA_DIR "/rolljam_fm_plugin.fal"

static inline const char* rolljam_protocol_plugin_app_id_for_filter(
    RollJamProtocolRegistryFilter filter) {
    return (filter == RollJamProtocolRegistryFilterFM) ? ROLLJAM_PROTOCOL_FM_PLUGIN_APP_ID :
                                                        ROLLJAM_PROTOCOL_AM_PLUGIN_APP_ID;
}

typedef struct {
    const char* plugin_name;
    RollJamProtocolRegistryFilter filter;
    const SubGhzProtocolRegistry* registry;
} RollJamProtocolPlugin;
