#include "plugin_registry.h"

#include "plugin.h"

#include <flipper_application/plugins/plugin_manager.h>
#include <loader/firmware_api/firmware_api.h>

#define TAG "SubGhzProtocolPack"

struct SubGhzProtocolPackRegistry {
    SubGhzProtocolRegistry registry;
    PluginManager* manager;
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
    const char* plugin_path) {
    furi_check(base_registry);
    furi_check(plugin_path);

    SubGhzProtocolPackRegistry* instance = malloc(sizeof(SubGhzProtocolPackRegistry));
    instance->manager = plugin_manager_alloc(
        SUBGHZ_PROTOCOL_PLUGIN_APP_ID, SUBGHZ_PROTOCOL_PLUGIN_API_VERSION, firmware_api_interface);
    plugin_manager_load_all_with_prefix(instance->manager, plugin_path, "protocol_");

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
