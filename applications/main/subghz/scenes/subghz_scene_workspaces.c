#include "../subghz_i.h"
#include "../helpers/subghz_feature_plugin.h"

void subghz_scene_workspaces_on_enter(void* context) {
    SubGhz* s = context;
    s->workspace_plugin = subghz_feature_plugin_load(
        s,
        &s->workspace_plugin_manager,
        SUBGHZ_WORKSPACE_PLUGIN_APP_ID,
        SUBGHZ_FEATURE_PLUGIN_DIR "subghz_workspaces.fal",
        "Profiles module\nmissing or outdated.\nUpdate resources.");
    if(s->workspace_plugin) s->workspace_context = s->workspace_plugin->alloc(s);
}

bool subghz_scene_workspaces_on_event(void* context, SceneManagerEvent event) {
    SubGhz* s = context;
    // Plugin callbacks only change their own views, never host scenes. Returning false for
    // top-level Back lets SceneManager pop AFTER this callback has returned.
    if(s->workspace_plugin) return s->workspace_plugin->event(s->workspace_context, event);
    return subghz_feature_plugin_handle_missing(s, event);
}

void subghz_scene_workspaces_on_exit(void* context) {
    SubGhz* s = context;
    view_dispatcher_switch_to_view(s->view_dispatcher, SubGhzViewIdWidget);
    if(s->workspace_plugin) s->workspace_plugin->free(s->workspace_context);
    s->workspace_context = NULL;
    s->workspace_plugin = NULL;
    subghz_feature_plugin_unload(s, &s->workspace_plugin_manager);
}
