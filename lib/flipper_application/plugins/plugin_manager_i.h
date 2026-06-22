#pragma once

#include "plugin_manager.h"

typedef enum {
    PluginManagerLoadStatusSuccess = 0,
    PluginManagerLoadStatusInvalidFile,
    PluginManagerLoadStatusNotEnoughMemory,
    PluginManagerLoadStatusInvalidManifest,
    PluginManagerLoadStatusApiTooOld,
    PluginManagerLoadStatusApiTooNew,
    PluginManagerLoadStatusTargetMismatch,
    PluginManagerLoadStatusNotPlugin,
    PluginManagerLoadStatusLoadError,
    PluginManagerLoadStatusMissingImports,
    PluginManagerLoadStatusMissingDescriptor,
    PluginManagerLoadStatusApplicationIdMismatch,
    PluginManagerLoadStatusAPIVersionMismatch,
} PluginManagerLoadStatus;

PluginManagerLoadStatus
    plugin_manager_load_single_detailed(PluginManager* manager, const char* path);
