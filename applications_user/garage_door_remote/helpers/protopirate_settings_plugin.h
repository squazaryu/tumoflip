#pragma once

#include "protopirate_settings.h"
#include <stddef.h>
#include <lib/flipper_application/flipper_application.h>

#define GDR_SETTINGS_PLUGIN_ID "gdr_settings"
#define GDR_SETTINGS_PLUGIN_VERSION 1U

typedef struct {
    size_t settings_size;
    void (*load)(ProtoPirateSettings* settings);
    void (*save)(ProtoPirateSettings* settings);
} ProtoPirateSettingsPlugin;
