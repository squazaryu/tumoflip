#include "protopirate_settings_plugin.h"
#include "protopirate_storage.h"
#include <furi.h>
#include <loader/firmware_api/firmware_api.h>
#include <lib/flipper_application/plugins/plugin_manager.h>

static bool settings_loaded;

void protopirate_settings_set_defaults(ProtoPirateSettings* settings) {
    *settings = (ProtoPirateSettings){.frequency = 433920000};
}

static bool protopirate_settings_call(ProtoPirateSettings* settings, bool save) {
    PluginManager* manager = plugin_manager_alloc(
        PP_SETTINGS_PLUGIN_ID, PP_SETTINGS_PLUGIN_VERSION, firmware_api_interface);
    if(!manager) return false;
    bool ok = false;
    if(plugin_manager_load_single(
           manager, APP_ASSETS_PATH("plugins/protopirate_settings_plugin.fal")) ==
       PluginManagerErrorNone) {
        const ProtoPirateSettingsPlugin* plugin = plugin_manager_get_ep(manager, 0);
        if(plugin && plugin->settings_size == sizeof(*settings) && plugin->load && plugin->save) {
            if(save) {
                /* Preserve our existing backup/rollback implementation in the host;
                 * the settings FAL keeps no callback after this synchronous call. */
                plugin->save(settings, protopirate_storage_commit_temp_file);
            } else {
                plugin->load(settings);
            }
            ok = true;
        }
    }
    plugin_manager_free(manager);
    return ok;
}

void protopirate_settings_load(ProtoPirateSettings* settings) {
    furi_check(settings);
    protopirate_settings_set_defaults(settings);
    settings_loaded = protopirate_settings_call(settings, false);
    if(!settings_loaded) FURI_LOG_E("PPSettings", "Settings codec unavailable; defaults only");
}

void protopirate_settings_save(ProtoPirateSettings* settings) {
    furi_check(settings);
    if(settings_loaded && !protopirate_settings_call(settings, true)) {
        FURI_LOG_E("PPSettings", "Settings codec unavailable; file unchanged");
    }
}
