// scenes/rolljam_scene_saved.c
#include "../rolljam_app_i.h"

#define TAG "RollJamSceneSaved"

void rolljam_scene_saved_on_enter(void* context) {
    RollJamApp* app = context;
    scene_manager_previous_scene(app->scene_manager);
}

bool rolljam_scene_saved_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void rolljam_scene_saved_on_exit(void* context) {
    UNUSED(context);
}
