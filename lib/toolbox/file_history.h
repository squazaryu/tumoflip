#pragma once
#include <storage/storage.h>
#include <flipper_application/plugins/plugin_manager.h>
#include <loader/firmware_api/firmware_api.h>
#include <string.h>
#define FILE_HISTORY_ROOT EXT_PATH("apps_data/device_library/history")
#define FILE_HISTORY_ENABLED FILE_HISTORY_ROOT "/enabled"
#define FILE_HISTORY_PLUGIN EXT_PATH("apps_data/device_library/plugins/file_history.fal")
#define FILE_HISTORY_APP_ID "TumoFileHistory"
#define FILE_HISTORY_ABI 1U
#define FILE_HISTORY_MAX_BYTES (2U * 1024U * 1024U)
typedef struct {
    uint32_t generation;
    uint32_t size;
    uint32_t timestamp;
    uint8_t digest[32];
    char source[256];
} FileHistoryRecord;
typedef struct {
    bool (*snapshot)(Storage* storage, const char* source);
    bool (*list)(Storage* storage, const char* source, FileHistoryRecord records[4]);
    bool (*restore_copy)(Storage* storage, const char* source, unsigned slot, FuriString* destination);
    bool (*sources)(Storage* storage, char paths[32][256], uint32_t* count);
} FileHistoryApi;
static inline bool file_history_supported(const char* path) {
    if(!path || strncmp(path, "/ext/", 5)) return false;
    const char* suffix = strrchr(path, '.');
    return suffix && (!strcmp(suffix, ".sub") || !strcmp(suffix, ".ir") || !strcmp(suffix, ".nfc"));
}
/** Opt-in synchronous checkpoint; false forbids replacing the original file. */
static inline bool file_history_before_write(Storage* storage, const char* source) {
    if(!file_history_supported(source)) return true;
    FS_Error enabled = storage_common_stat(storage, FILE_HISTORY_ENABLED, NULL);
    if(enabled == FSE_NOT_EXIST) return true;
    if(enabled != FSE_OK) return false;
    FS_Error exists = storage_common_stat(storage, source, NULL);
    if(exists == FSE_NOT_EXIST) return true;
    if(exists != FSE_OK) return false;
    PluginManager* manager = plugin_manager_alloc(FILE_HISTORY_APP_ID, FILE_HISTORY_ABI, firmware_api_interface);
    bool ok = plugin_manager_load_single(manager, FILE_HISTORY_PLUGIN) == PluginManagerErrorNone;
    const FileHistoryApi* api = ok ? plugin_manager_get_ep(manager, 0) : NULL;
    ok = api && api->snapshot && api->snapshot(storage, source);
    plugin_manager_free(manager);
    return ok;
}
