#include "plugin_registry_i.h"

#include "plugin.h"

#include <flipper_application/plugins/plugin_manager_i.h>
#include <furi/core/memmgr.h>
#include <loader/firmware_api/firmware_api.h>
#include <toolbox/path.h>

#define TAG "SubGhzProtocolPack"

struct SubGhzProtocolPackRegistry {
    SubGhzProtocolRegistry registry;
    PluginManager* manager;
    SubGhzProtocolPackReport report;
    SubGhzProtocolPackReportEntry* report_entries;
};

typedef struct {
    uint32_t groups;
    const char* filename;
} SubGhzProtocolPackEntry;

#define PACK_GROUP(group) (1UL << (group))

static const SubGhzProtocolPackEntry subghz_protocol_pack_entries[] = {
    {PACK_GROUP(SubGhzProtocolPackGroupLegacy) | PACK_GROUP(SubGhzProtocolPackGroupKia),
     "protocol_kia_v0.fal"},
    {PACK_GROUP(SubGhzProtocolPackGroupLegacy) | PACK_GROUP(SubGhzProtocolPackGroupKia),
     "protocol_kia_v1.fal"},
    {PACK_GROUP(SubGhzProtocolPackGroupLegacy) | PACK_GROUP(SubGhzProtocolPackGroupKia),
     "protocol_kia_v2.fal"},
    {PACK_GROUP(SubGhzProtocolPackGroupKia), "protocol_kia_v3_v4.fal"},
    {PACK_GROUP(SubGhzProtocolPackGroupKia), "protocol_kia_v5.fal"},
    {PACK_GROUP(SubGhzProtocolPackGroupKia), "protocol_kia_v6.fal"},
    {PACK_GROUP(SubGhzProtocolPackGroupKia), "protocol_kia_v7.fal"},
    {PACK_GROUP(SubGhzProtocolPackGroupFord), "protocol_ford_v0.fal"},
    {PACK_GROUP(SubGhzProtocolPackGroupFord), "protocol_ford_v1.fal"},
    {PACK_GROUP(SubGhzProtocolPackGroupFord), "protocol_ford_v2.fal"},
    {PACK_GROUP(SubGhzProtocolPackGroupFord), "protocol_ford_v3.fal"},
    {PACK_GROUP(SubGhzProtocolPackGroupLegacy) | PACK_GROUP(SubGhzProtocolPackGroupAsiaUs),
     "protocol_mitsubishi_v0.fal"},
    {PACK_GROUP(SubGhzProtocolPackGroupEurope), "protocol_fiat_marelli.fal"},
    {PACK_GROUP(SubGhzProtocolPackGroupEurope), "protocol_land_rover_v0.fal"},
    {PACK_GROUP(SubGhzProtocolPackGroupEurope), "protocol_porsche_cayenne.fal"},
    {PACK_GROUP(SubGhzProtocolPackGroupEurope), "protocol_psa.fal"},
    {PACK_GROUP(SubGhzProtocolPackGroupLegacy) | PACK_GROUP(SubGhzProtocolPackGroupEurope),
     "protocol_vag.fal"},
    {PACK_GROUP(SubGhzProtocolPackGroupAsiaUs), "protocol_chrysler.fal"},
    {PACK_GROUP(SubGhzProtocolPackGroupAsiaUs), "protocol_mazda_siemens.fal"},
    {PACK_GROUP(SubGhzProtocolPackGroupAsiaUs), "protocol_mazda_v0.fal"},
    {PACK_GROUP(SubGhzProtocolPackGroupAsiaUs), "protocol_subaru.fal"},
    {PACK_GROUP(SubGhzProtocolPackGroupAlarm), "protocol_scher_khan.fal"},
    {PACK_GROUP(SubGhzProtocolPackGroupAlarm), "protocol_sheriff_cfm.fal"},
    {PACK_GROUP(SubGhzProtocolPackGroupAlarm), "protocol_star_line.fal"},
    {PACK_GROUP(SubGhzProtocolPackGroupShuka), "protocol_gm_rolling.fal"},
    {PACK_GROUP(SubGhzProtocolPackGroupShuka), "protocol_honda_acura.fal"},
    {PACK_GROUP(SubGhzProtocolPackGroupShuka), "protocol_hyundai_new.fal"},
    {PACK_GROUP(SubGhzProtocolPackGroupShuka), "protocol_nissan.fal"},
    {PACK_GROUP(SubGhzProtocolPackGroupShuka), "protocol_renault.fal"},
    {PACK_GROUP(SubGhzProtocolPackGroupShuka), "protocol_toyota_lexus.fal"},
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
            malloc(sizeof(SubGhzProtocolPackReportEntry) * expected_plugin_count) :
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
    for(size_t i = 0; i < COUNT_OF(subghz_protocol_pack_entries); i++) {
        const SubGhzProtocolPackEntry* entry = &subghz_protocol_pack_entries[i];
        if(!(entry->groups & group_mask)) continue;

        SubGhzProtocolPackReportEntry* report_entry = &instance->report_entries[report_index++];
        report_entry->filename = entry->filename;
        report_entry->protocol_name = NULL;

        path_concat(plugin_path, entry->filename, plugin_file);
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
