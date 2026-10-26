#include "plugin_registry_i.h"

#include "plugin.h"

#include <flipper_application/plugins/plugin_manager_i.h>
#include <furi/core/memmgr.h>
#include <loader/firmware_api/firmware_api.h>
#include <toolbox/path.h>
#include <stdio.h>

#define TAG "SubGhzProtocolPack"

struct SubGhzProtocolPackRegistry {
    SubGhzProtocolRegistry registry;
    PluginManager* manager;
    SubGhzProtocolPackReport report;
    SubGhzProtocolPackReportEntry* report_entries;
};

typedef struct {
    uint32_t groups;
    // Basename only. The invariant protocol_ / .fal text is stored once.
    const char* filename;
} SubGhzProtocolPackEntry;

static bool subghz_protocol_pack_filename(const char* stem, char* output, size_t size) {
    if(!stem || !*stem || !output || !size) return false;
    size_t length = 0;
    for(; stem[length]; length++)
        if(length >= 31 || !((stem[length] >= 'a' && stem[length] <= 'z') ||
                             (stem[length] >= '0' && stem[length] <= '9') || stem[length] == '_'))
            return false;
    int written = snprintf(output, size, "protocol_%s.fal", stem);
    return written > 0 && (size_t)written < size;
}

#define PACK_GROUP(group) (1UL << (group))
#define PACK_GROUP_ALL ((1UL << SubGhzProtocolPackGroupCount) - 1UL)

static const SubGhzProtocolPackEntry subghz_protocol_pack_entries[] = {
    {PACK_GROUP_ALL, "superrollo"},
    {PACK_GROUP(SubGhzProtocolPackGroupCore), "rx_x10"},
    {PACK_GROUP(SubGhzProtocolPackGroupCore), "rx_urmet"},
    {PACK_GROUP(SubGhzProtocolPackGroupAsiaUs), "rx_gm0"},
    {PACK_GROUP(SubGhzProtocolPackGroupAsiaUs), "rx_gm1"},
    {PACK_GROUP(SubGhzProtocolPackGroupAsiaUs), "rx_ty0"},
    {PACK_GROUP(SubGhzProtocolPackGroupAsiaUs), "rx_mz2"},
    {PACK_GROUP(SubGhzProtocolPackGroupEurope), "rx_mb0"},
    {PACK_GROUP(SubGhzProtocolPackGroupEurope), "rx_mb1"},
    {PACK_GROUP(SubGhzProtocolPackGroupEurope), "rx_op1"},
    {PACK_GROUP(SubGhzProtocolPackGroupEurope), "rx_bmw"},
    {PACK_GROUP(SubGhzProtocolPackGroupLegacy) | PACK_GROUP(SubGhzProtocolPackGroupKia),
     "kia_v0"},
    {PACK_GROUP(SubGhzProtocolPackGroupLegacy) | PACK_GROUP(SubGhzProtocolPackGroupKia),
     "kia_v1"},
    {PACK_GROUP(SubGhzProtocolPackGroupLegacy) | PACK_GROUP(SubGhzProtocolPackGroupKia),
     "kia_v2"},
    {PACK_GROUP(SubGhzProtocolPackGroupKia), "kia_v3_v4"},
    {PACK_GROUP(SubGhzProtocolPackGroupKia), "kia_v5"},
    {PACK_GROUP(SubGhzProtocolPackGroupKia), "kia_v6"},
    {PACK_GROUP(SubGhzProtocolPackGroupKia), "kia_v7"},
    {PACK_GROUP(SubGhzProtocolPackGroupFord), "ford_v0"},
    {PACK_GROUP(SubGhzProtocolPackGroupFord), "ford_v1"},
    {PACK_GROUP(SubGhzProtocolPackGroupFord), "ford_v2"},
    {PACK_GROUP(SubGhzProtocolPackGroupFord), "ford_v3"},
    {PACK_GROUP(SubGhzProtocolPackGroupLegacy) | PACK_GROUP(SubGhzProtocolPackGroupAsiaUs),
     "mitsubishi_v0"},
    {PACK_GROUP(SubGhzProtocolPackGroupEurope), "fiat_marelli"},
    {PACK_GROUP(SubGhzProtocolPackGroupEurope), "renault_v1"},
    {PACK_GROUP(SubGhzProtocolPackGroupEurope), "land_rover_v0"},
    {PACK_GROUP(SubGhzProtocolPackGroupEurope), "porsche_cayenne"},
    {PACK_GROUP(SubGhzProtocolPackGroupEurope), "psa"},
    {PACK_GROUP(SubGhzProtocolPackGroupLegacy) | PACK_GROUP(SubGhzProtocolPackGroupEurope),
     "vag"},
    {PACK_GROUP(SubGhzProtocolPackGroupAsiaUs), "chrysler"},
    {PACK_GROUP(SubGhzProtocolPackGroupAsiaUs), "mazda_siemens"},
    {PACK_GROUP(SubGhzProtocolPackGroupAsiaUs), "mazda_v0"},
    {PACK_GROUP(SubGhzProtocolPackGroupAsiaUs), "subaru"},
    {PACK_GROUP(SubGhzProtocolPackGroupAlarm), "scher_khan"},
    {PACK_GROUP(SubGhzProtocolPackGroupAlarm), "sheriff_cfm"},
    {PACK_GROUP(SubGhzProtocolPackGroupAlarm), "star_line"},
    {PACK_GROUP(SubGhzProtocolPackGroupShuka), "gm_rolling"},
    {PACK_GROUP(SubGhzProtocolPackGroupShuka), "honda_acura"},
    {PACK_GROUP(SubGhzProtocolPackGroupShuka), "hyundai_new"},
    {PACK_GROUP(SubGhzProtocolPackGroupShuka), "nissan"},
    {PACK_GROUP(SubGhzProtocolPackGroupShuka), "renault"},
    {PACK_GROUP(SubGhzProtocolPackGroupShuka), "toyota_lexus"},
};

const char* subghz_protocol_pack_group_get_name(SubGhzProtocolPackGroup group) {
    static const char* const names[SubGhzProtocolPackGroupCount] = {
        "Core",
        "Legacy",
        "Kia",
        "Ford",
        "Europe",
        "Asia/US",
        "Alarm",
        "Shuka Auto",
    };

    return group < SubGhzProtocolPackGroupCount ? names[group] : "Unknown";
}

const char* subghz_protocol_pack_status_get_name(SubGhzProtocolPackStatus status) {
    switch(status) {
    case SubGhzProtocolPackStatusLoaded:
        return "Loaded";
    case SubGhzProtocolPackStatusInvalidFile:
        return "Missing or invalid file";
    case SubGhzProtocolPackStatusNotEnoughMemory:
        return "Not enough RAM";
    case SubGhzProtocolPackStatusInvalidManifest:
        return "Invalid manifest";
    case SubGhzProtocolPackStatusFirmwareApiTooOld:
        return "Plugin FW API is too old";
    case SubGhzProtocolPackStatusFirmwareApiTooNew:
        return "Plugin FW API is too new";
    case SubGhzProtocolPackStatusTargetMismatch:
        return "Hardware target mismatch";
    case SubGhzProtocolPackStatusNotPlugin:
        return "File is not a plugin";
    case SubGhzProtocolPackStatusLoadError:
        return "Plugin load error";
    case SubGhzProtocolPackStatusMissingImports:
        return "Missing imports";
    case SubGhzProtocolPackStatusMissingDescriptor:
        return "Missing plugin descriptor";
    case SubGhzProtocolPackStatusApplicationIdMismatch:
        return "Application ID mismatch";
    case SubGhzProtocolPackStatusPluginApiMismatch:
        return "Plugin API mismatch";
    case SubGhzProtocolPackStatusInvalidProtocol:
        return "Invalid protocol entry";
    case SubGhzProtocolPackStatusDuplicateProtocol:
        return "Duplicate protocol";
    default:
        return "Unknown error";
    }
}

static bool subghz_protocol_pack_registry_contains(
    const SubGhzProtocolPackRegistry* instance,
    const char* protocol_name) {
    for(size_t i = 0; i < instance->registry.size; i++) {
        if(strcmp(instance->registry.items[i]->name, protocol_name) == 0) return true;
    }
    return false;
}

static SubGhzProtocolPackStatus
    subghz_protocol_pack_status_from_load_status(PluginManagerLoadStatus status) {
    switch(status) {
    case PluginManagerLoadStatusSuccess:
        return SubGhzProtocolPackStatusLoaded;
    case PluginManagerLoadStatusInvalidFile:
        return SubGhzProtocolPackStatusInvalidFile;
    case PluginManagerLoadStatusNotEnoughMemory:
        return SubGhzProtocolPackStatusNotEnoughMemory;
    case PluginManagerLoadStatusInvalidManifest:
        return SubGhzProtocolPackStatusInvalidManifest;
    case PluginManagerLoadStatusApiTooOld:
        return SubGhzProtocolPackStatusFirmwareApiTooOld;
    case PluginManagerLoadStatusApiTooNew:
        return SubGhzProtocolPackStatusFirmwareApiTooNew;
    case PluginManagerLoadStatusTargetMismatch:
        return SubGhzProtocolPackStatusTargetMismatch;
    case PluginManagerLoadStatusNotPlugin:
        return SubGhzProtocolPackStatusNotPlugin;
    case PluginManagerLoadStatusLoadError:
        return SubGhzProtocolPackStatusLoadError;
    case PluginManagerLoadStatusMissingImports:
        return SubGhzProtocolPackStatusMissingImports;
    case PluginManagerLoadStatusMissingDescriptor:
        return SubGhzProtocolPackStatusMissingDescriptor;
    case PluginManagerLoadStatusApplicationIdMismatch:
        return SubGhzProtocolPackStatusApplicationIdMismatch;
    case PluginManagerLoadStatusAPIVersionMismatch:
        return SubGhzProtocolPackStatusPluginApiMismatch;
    default:
        return SubGhzProtocolPackStatusLoadError;
    }
}

SubGhzProtocolPackRegistry* subghz_protocol_pack_registry_alloc(
    const SubGhzProtocolRegistry* base_registry,
    const char* plugin_path,
    SubGhzProtocolPackGroup group) {
    furi_check(base_registry);
    furi_check(plugin_path);

    const size_t heap_before = memmgr_get_free_heap();
    SubGhzProtocolPackRegistry* instance = malloc(sizeof(SubGhzProtocolPackRegistry));
    if(group >= SubGhzProtocolPackGroupCount) group = SubGhzProtocolPackGroupCore;

    const uint32_t group_mask = PACK_GROUP(group);
    size_t expected_plugin_count = 0;
    for(size_t i = 0; i < COUNT_OF(subghz_protocol_pack_entries); i++) {
        if(subghz_protocol_pack_entries[i].groups & group_mask) expected_plugin_count++;
    }

    instance->report_entries =
        expected_plugin_count ?
            malloc((sizeof(SubGhzProtocolPackReportEntry) + 48U) * expected_plugin_count) :
            NULL;
    instance->report = (SubGhzProtocolPackReport){
        .group = group,
        .base_protocol_count = base_registry->size,
        .expected_plugin_count = expected_plugin_count,
        .firmware_api_major = firmware_api_interface->api_version_major,
        .firmware_api_minor = firmware_api_interface->api_version_minor,
        .plugin_api_version = SUBGHZ_PROTOCOL_PLUGIN_API_VERSION,
        .entries = instance->report_entries,
    };

    instance->registry.items =
        malloc(sizeof(SubGhzProtocol*) * (base_registry->size + expected_plugin_count));
    memcpy(
        (void*)instance->registry.items,
        base_registry->items,
        sizeof(SubGhzProtocol*) * base_registry->size);
    instance->registry.size = base_registry->size;

    instance->manager = plugin_manager_alloc(
        SUBGHZ_PROTOCOL_PLUGIN_APP_ID, SUBGHZ_PROTOCOL_PLUGIN_API_VERSION, firmware_api_interface);

    FuriString* plugin_file = furi_string_alloc();
    size_t report_index = 0;
    char (*filenames)[48] = (char (*)[48])(instance->report_entries + expected_plugin_count);
    for(size_t i = 0; i < COUNT_OF(subghz_protocol_pack_entries); i++) {
        const SubGhzProtocolPackEntry* entry = &subghz_protocol_pack_entries[i];
        if(!(entry->groups & group_mask)) continue;

        SubGhzProtocolPackReportEntry* report_entry = &instance->report_entries[report_index++];
        char* filename = filenames[report_index - 1];
        if(!subghz_protocol_pack_filename(entry->filename, filename, 48)) {
            filename[0] = '\0';
            report_entry->filename = filename;
            report_entry->protocol_name = NULL;
            report_entry->status = SubGhzProtocolPackStatusInvalidFile;
            continue;
        }
        report_entry->filename = filename;
        report_entry->protocol_name = NULL;

        path_concat(plugin_path, filename, plugin_file);
        const PluginManagerLoadStatus load_status = plugin_manager_load_single_detailed(
            instance->manager, furi_string_get_cstr(plugin_file));
        report_entry->status = subghz_protocol_pack_status_from_load_status(load_status);
        if(load_status != PluginManagerLoadStatusSuccess) {
            FURI_LOG_W(TAG, "Failed to load %s", entry->filename);
            continue;
        }

        const size_t plugin_index = plugin_manager_get_count(instance->manager) - 1;
        const SubGhzProtocol* protocol = plugin_manager_get_ep(instance->manager, plugin_index);
        if(!protocol || !protocol->name) {
            report_entry->status = SubGhzProtocolPackStatusInvalidProtocol;
            FURI_LOG_W(TAG, "Ignoring invalid protocol plugin %s", entry->filename);
            continue;
        }

        report_entry->protocol_name = protocol->name;
        if(subghz_protocol_pack_registry_contains(instance, protocol->name)) {
            report_entry->status = SubGhzProtocolPackStatusDuplicateProtocol;
            FURI_LOG_W(TAG, "Ignoring duplicate protocol %s", protocol->name);
            continue;
        }

        ((const SubGhzProtocol**)instance->registry.items)[instance->registry.size++] = protocol;
        instance->report.loaded_plugin_count++;
        FURI_LOG_I(TAG, "Loaded %s", protocol->name);
    }
    furi_string_free(plugin_file);

    instance->report.registered_protocol_count = instance->registry.size;
    const size_t heap_after = memmgr_get_free_heap();
    instance->report.heap_used = heap_before > heap_after ? heap_before - heap_after : 0;

    return instance;
}

void subghz_protocol_pack_registry_free(SubGhzProtocolPackRegistry* instance) {
    furi_check(instance);
    plugin_manager_free(instance->manager);
    free((void*)instance->registry.items);
    free(instance->report_entries);
    free(instance);
}

const SubGhzProtocolRegistry*
    subghz_protocol_pack_registry_get(SubGhzProtocolPackRegistry* instance) {
    furi_check(instance);
    return &instance->registry;
}

const SubGhzProtocolPackReport*
    subghz_protocol_pack_registry_get_report(const SubGhzProtocolPackRegistry* instance) {
    furi_check(instance);
    return &instance->report;
}
