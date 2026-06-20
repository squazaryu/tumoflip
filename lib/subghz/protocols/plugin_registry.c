#include "plugin_registry.h"

#include "plugin.h"

#include <flipper_application/plugins/plugin_manager.h>
#include <loader/firmware_api/firmware_api.h>
#include <toolbox/path.h>

#define TAG "SubGhzProtocolPack"

struct SubGhzProtocolPackRegistry {
    SubGhzProtocolRegistry registry;
    PluginManager* manager;
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
};

static bool subghz_protocol_pack_registry_contains(
    const SubGhzProtocolPackRegistry* instance,
    const char* protocol_name) {
    for(size_t i = 0; i < instance->registry.size; i++) {
        if(strcmp(instance->registry.items[i]->name, protocol_name) == 0) return true;
    }
    return false;
}

SubGhzProtocolPackRegistry* subghz_protocol_pack_registry_alloc(
    const SubGhzProtocolRegistry* base_registry,
    const char* plugin_path,
    SubGhzProtocolPackGroup group) {
    furi_check(base_registry);
    furi_check(plugin_path);

    SubGhzProtocolPackRegistry* instance = malloc(sizeof(SubGhzProtocolPackRegistry));
    instance->manager = plugin_manager_alloc(
        SUBGHZ_PROTOCOL_PLUGIN_APP_ID, SUBGHZ_PROTOCOL_PLUGIN_API_VERSION, firmware_api_interface);
    if(group >= SubGhzProtocolPackGroupCount) group = SubGhzProtocolPackGroupLegacy;

    FuriString* plugin_file = furi_string_alloc();
    const uint32_t group_mask = PACK_GROUP(group);
    for(size_t i = 0; i < COUNT_OF(subghz_protocol_pack_entries); i++) {
        const SubGhzProtocolPackEntry* entry = &subghz_protocol_pack_entries[i];
        if(!(entry->groups & group_mask)) continue;

        path_concat(plugin_path, entry->filename, plugin_file);
        if(plugin_manager_load_single(instance->manager, furi_string_get_cstr(plugin_file)) !=
           PluginManagerErrorNone) {
            FURI_LOG_W(TAG, "Failed to load %s", entry->filename);
        }
    }
    furi_string_free(plugin_file);

    const size_t plugin_count = plugin_manager_get_count(instance->manager);
    instance->registry.items =
        malloc(sizeof(SubGhzProtocol*) * (base_registry->size + plugin_count));
    memcpy(
        (void*)instance->registry.items,
        base_registry->items,
        sizeof(SubGhzProtocol*) * base_registry->size);
    instance->registry.size = base_registry->size;

    for(size_t i = 0; i < plugin_count; i++) {
        const SubGhzProtocol* protocol = plugin_manager_get_ep(instance->manager, i);
        if(!protocol || !protocol->name ||
           subghz_protocol_pack_registry_contains(instance, protocol->name)) {
            FURI_LOG_W(TAG, "Ignoring invalid or duplicate protocol plugin");
            continue;
        }
        ((const SubGhzProtocol**)instance->registry.items)[instance->registry.size++] = protocol;
        FURI_LOG_I(TAG, "Loaded %s", protocol->name);
    }

    return instance;
}

void subghz_protocol_pack_registry_free(SubGhzProtocolPackRegistry* instance) {
    furi_check(instance);
    plugin_manager_free(instance->manager);
    free((void*)instance->registry.items);
    free(instance);
}

const SubGhzProtocolRegistry*
    subghz_protocol_pack_registry_get(SubGhzProtocolPackRegistry* instance) {
    furi_check(instance);
    return &instance->registry;
}
