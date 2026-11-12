#pragma once

#include "protopirate_settings.h"
#include <stddef.h>
#include <storage/storage.h>
#include <lib/flipper_application/flipper_application.h>

#define PP_SETTINGS_PLUGIN_ID "pp_settings"
#define PP_SETTINGS_PLUGIN_VERSION 1U

typedef bool (*ProtoPirateSettingsCommit)(Storage*, const char*, const char*);
typedef struct {
    size_t settings_size;
    void (*load)(ProtoPirateSettings* settings);
    void (*save)(ProtoPirateSettings* settings, ProtoPirateSettingsCommit commit);
} ProtoPirateSettingsPlugin;
