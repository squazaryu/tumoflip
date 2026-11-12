#pragma once
#include "../subghz.h"
#include <gui/scene_manager.h>
#define SUBGHZ_WORKSPACE_PLUGIN_APP_ID "SubGhzWorkspacePlugin"
typedef struct {
    void* (*alloc)(SubGhz* subghz);
    bool (*event)(void* context, SceneManagerEvent event);
    void (*free)(void* context);
} SubGhzWorkspacePlugin;
