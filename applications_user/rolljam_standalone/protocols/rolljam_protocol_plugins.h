#pragma once

#include <lib/flipper_application/flipper_application.h>
#include <lib/subghz/types.h>
#include "protocol_items.h"

#define ROLLJAM_PROTOCOL_PLUGIN_APP_ID       "rolljam_protocol_plugin"
#define ROLLJAM_PROTOCOL_AM_PLUGIN_APP_ID    "rolljam_am_plugin"
#define ROLLJAM_PROTOCOL_FM_PLUGIN_APP_ID    "rolljam_fm_plugin"
#define ROLLJAM_PROTOCOL_PLUGIN_API_VERSION  1U

typedef struct {
    const char* plugin_name;
    RollJamProtocolRegistryFilter filter;
    const SubGhzProtocolRegistry* registry;
} RollJamProtocolPlugin;
