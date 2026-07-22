#include "../subghz_i.h"
#include <lib/subghz/subghz_protocol_registry.h>
#include <stdlib.h>

#define TAG "SubGhzSceneProtocolList"

typedef struct {
    SubGhz* subghz;
    const char* protocol_name;
} SubGhzProtocolListItemContext;

static SubGhzProtocolListItemContext* protocol_list_item_contexts = NULL;

static void subghz_scene_protocol_list_free_contexts(void) {
    free(protocol_list_item_contexts);
    protocol_list_item_contexts = NULL;
}

static void subghz_scene_protocol_list_item_changed(VariableItem* item) {
    SubGhzProtocolListItemContext* item_context = variable_item_get_context(item);
    if(!item_context || !item_context->subghz || !item_context->protocol_name) return;

    SubGhz* subghz = item_context->subghz;
    bool should_disable = variable_item_get_current_value_index(item) == 1;

    bool changed = subghz_last_settings_protocol_filter_set(
        subghz->last_settings, item_context->protocol_name, should_disable);
    bool is_disabled = subghz_last_settings_protocol_filter_contains(
        subghz->last_settings, item_context->protocol_name);

    variable_item_set_current_value_index(item, is_disabled ? 1 : 0);
    variable_item_set_current_value_text(item, is_disabled ? "OFF" : "ON");
    if(changed) subghz_last_settings_save(subghz->last_settings);
}

void subghz_scene_protocol_list_on_enter(void* context) {
    SubGhz* subghz = context;
    VariableItemList* list = subghz->variable_item_list;
    variable_item_list_reset(list);
    subghz_scene_protocol_list_free_contexts();

    size_t protocol_count = subghz_protocol_registry_count(&subghz_protocol_registry);
    protocol_list_item_contexts =
        malloc(sizeof(SubGhzProtocolListItemContext) * protocol_count);
    furi_check(protocol_list_item_contexts);

    for(size_t i = 0; i < protocol_count; i++) {
        const SubGhzProtocol* protocol =
            subghz_protocol_registry_get_by_index(&subghz_protocol_registry, i);
        if(!protocol) continue;

        protocol_list_item_contexts[i].subghz = subghz;
        protocol_list_item_contexts[i].protocol_name = protocol->name;

        VariableItem* item = variable_item_list_add(
            list,
            protocol->name,
            2,
            subghz_scene_protocol_list_item_changed,
            &protocol_list_item_contexts[i]);

        bool is_disabled =
            subghz_last_settings_protocol_filter_contains(subghz->last_settings, protocol->name);
        variable_item_set_current_value_index(item, is_disabled ? 1 : 0);
        variable_item_set_current_value_text(item, is_disabled ? "OFF" : "ON");
    }

    variable_item_list_set_selected_item(
        list,
        scene_manager_get_scene_state(subghz->scene_manager, SubGhzSceneProtocolList));

    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdVariableItemList);
}

bool subghz_scene_protocol_list_on_event(void* context, SceneManagerEvent event) {
    SubGhz* subghz = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        scene_manager_set_scene_state(
            subghz->scene_manager, SubGhzSceneProtocolList, event.event);
        consumed = true;
    } else if(event.type == SceneManagerEventTypeBack) {
        scene_manager_previous_scene(subghz->scene_manager);
        consumed = true;
    }

    return consumed;
}

void subghz_scene_protocol_list_on_exit(void* context) {
    SubGhz* subghz = context;
    scene_manager_set_scene_state(
        subghz->scene_manager,
        SubGhzSceneProtocolList,
        variable_item_list_get_selected_item_index(subghz->variable_item_list));
    variable_item_list_reset(subghz->variable_item_list);
    subghz_scene_protocol_list_free_contexts();
}
