// scenes/rolljam_scene.h
#pragma once

#include <gui/scene_manager.h>
#include "../rolljam_app_i.h"

// Generate scene id and total number
#define ADD_SCENE(prefix, name, id) RollJamScene##id,
typedef enum {
#include "rolljam_scene_config.h"
    RollJamSceneNum,
} RollJamScene;
#undef ADD_SCENE

extern const SceneManagerHandlers rolljam_scene_handlers;

// Generate scene on_enter handlers declaration
#define ADD_SCENE(prefix, name, id) void prefix##_scene_##name##_on_enter(void*);
#include "rolljam_scene_config.h"
#undef ADD_SCENE

// Generate scene on_event handlers declaration
#define ADD_SCENE(prefix, name, id) \
    bool prefix##_scene_##name##_on_event(void* context, SceneManagerEvent event);
#include "rolljam_scene_config.h"
#undef ADD_SCENE

// Generate scene on_exit handlers declaration
#define ADD_SCENE(prefix, name, id) void prefix##_scene_##name##_on_exit(void* context);
#include "rolljam_scene_config.h"
#undef ADD_SCENE
