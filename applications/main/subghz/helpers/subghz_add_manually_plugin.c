#include "subghz_add_manually_plugin.h"

#include "../subghz_i.h"
#include "subghz_feature_plugin.h"

#define SUBGHZ_ADD_MANUALLY_PLUGIN_PATH SUBGHZ_FEATURE_PLUGIN_DIR "subghz_add_manually.fal"

void subghz_add_manually_scene_on_enter(SubGhz* subghz, SubGhzAddManuallyScene scene) {
    if(!subghz->add_manually_plugin) {
        subghz->add_manually_plugin = subghz_feature_plugin_load(
            subghz,
            &subghz->add_manually_plugin_manager,
            SUBGHZ_ADD_MANUALLY_PLUGIN_APP_ID,
            SUBGHZ_ADD_MANUALLY_PLUGIN_PATH,
            "Add Manually is\nmissing or\noutdated. Update\nresources.");
        if(!subghz->add_manually_plugin) {
            return;
        }
    }

    subghz->add_manually_dispatch_depth++;
    subghz->add_manually_plugin->scene[scene].on_enter(subghz);
    subghz->add_manually_dispatch_depth--;
    if(subghz->add_manually_unload_pending) subghz_add_manually_plugin_unload(subghz);
}

bool subghz_add_manually_scene_on_event(
    SubGhz* subghz,
    SubGhzAddManuallyScene scene,
    SceneManagerEvent event) {
    if(!subghz->add_manually_plugin) {
        return subghz_feature_plugin_handle_missing(subghz, event);
    }

    subghz->add_manually_dispatch_depth++;
    bool handled = subghz->add_manually_plugin->scene[scene].on_event(subghz, event);
    subghz->add_manually_dispatch_depth--;
    if(subghz->add_manually_unload_pending) subghz_add_manually_plugin_unload(subghz);
    return handled;
}

void subghz_add_manually_scene_on_exit(SubGhz* subghz, SubGhzAddManuallyScene scene) {
    if(subghz->add_manually_plugin) {
        subghz->add_manually_dispatch_depth++;
        subghz->add_manually_plugin->scene[scene].on_exit(subghz);
        subghz->add_manually_dispatch_depth--;
        if(subghz->add_manually_unload_pending) subghz_add_manually_plugin_unload(subghz);
    }
}

void subghz_add_manually_plugin_unload(SubGhz* subghz) {
    if(subghz->add_manually_dispatch_depth) {
        subghz->add_manually_unload_pending = true;
        return;
    }
    subghz->add_manually_unload_pending = false;
    if(subghz->add_manually_plugin) {
        // byte_input keeps its header by reference and draws through it, so the string the last
        // scene left there - a literal inside the plugin, including the empty one its own on_exit
        // installs - has to go before the image does. Its result callback points into the plugin
        // too, but only the draw path can still run by the time we are here. Reaching this branch
        // at all means the app was allocated in full, so byte_input exists.
        byte_input_set_header_text(subghz->byte_input, "");
        byte_input_set_result_callback(subghz->byte_input, NULL, NULL, NULL, NULL, 0);
        subghz->add_manually_plugin = NULL;
    }

    subghz_feature_plugin_unload(subghz, &subghz->add_manually_plugin_manager);
}
