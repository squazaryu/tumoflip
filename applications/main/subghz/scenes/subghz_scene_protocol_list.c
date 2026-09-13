#include "../subghz_i.h"
#include <lib/subghz/subghz_protocol_registry.h>
#include <subghz_radio_broker/subghz_radio_broker.h>
#include <stdlib.h>

#define TAG "SubGhzSceneProtocolList"

typedef struct {
    SubGhz* subghz;
    const char* protocol_name;
    bool compatible;
} SubGhzProtocolListItemContext;

static SubGhzProtocolListItemContext* protocol_list_item_contexts = NULL;

static void subghz_scene_protocol_list_free_contexts(void) {
    free(protocol_list_item_contexts);
    protocol_list_item_contexts = NULL;
}

static void subghz_scene_protocol_list_item_changed(VariableItem* item) {
    SubGhzProtocolListItemContext* item_context = variable_item_get_context(item);
    if(!item_context || !item_context->subghz || !item_context->protocol_name ||
       !item_context->compatible) {
        return;
    }

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

static const char* subghz_scene_protocol_list_validation_text(
    SubGhzRadioBrokerValidation validation,
    const SubGhzProtocol* protocol) {
    switch(validation) {
    case SubGhzRadioBrokerValidationUnsupportedBand:
        if(protocol->flag & SubGhzProtocolFlag_315) return "315";
        if(protocol->flag & SubGhzProtocolFlag_433) return "433";
        if(protocol->flag & SubGhzProtocolFlag_868) return "868";
        return "BAND";
    case SubGhzRadioBrokerValidationUnsupportedPreset: {
        const bool am = protocol->flag & SubGhzProtocolFlag_AM;
        const bool fm = protocol->flag & SubGhzProtocolFlag_FM;
        if(am && !fm) return "AM";
        if(fm && !am) return "FM";
        return "MODE";
    }
    case SubGhzRadioBrokerValidationReceiveUnsupported:
        return "TX";
    case SubGhzRadioBrokerValidationUnsupportedDevice:
        return "RADIO";
    default:
        return "N/A";
    }
}

static SubGhzRadioBrokerValidation
    subghz_scene_protocol_list_validate(SubGhz* subghz, const SubGhzProtocol* protocol) {
    if(!(protocol->flag & SubGhzProtocolFlag_Decodable) || !protocol->decoder) {
        return SubGhzRadioBrokerValidationReceiveUnsupported;
    }

    if(subghz->last_settings->hopping_mode != SubGhzHoppingModeOff) {
        return SubGhzRadioBrokerValidationOk;
    }

    const uint32_t frequency = subghz->last_settings->frequency;
    const SubGhzProtocolFlag required_band = frequency <= 361000000U ? SubGhzProtocolFlag_315 :
                                             frequency <= 481000000U ? SubGhzProtocolFlag_433 :
                                                                       SubGhzProtocolFlag_868;
    if(!(protocol->flag & required_band)) {
        return SubGhzRadioBrokerValidationUnsupportedBand;
    }

    SubGhzSetting* setting = subghz_txrx_get_setting(subghz->txrx);
    const char* preset_name =
        subghz_setting_get_preset_name(setting, subghz->last_settings->preset_index);
    const char* preset_short = subghz_txrx_get_preset_name(subghz->txrx, preset_name);
    const bool modulation_matches =
        (preset_short[0] == 'A' && (protocol->flag & SubGhzProtocolFlag_AM)) ||
        (preset_short[0] == 'F' && (protocol->flag & SubGhzProtocolFlag_FM)) ||
        (preset_short[0] == 'C' &&
         (protocol->flag & (SubGhzProtocolFlag_AM | SubGhzProtocolFlag_FM)));
    return modulation_matches ? SubGhzRadioBrokerValidationOk :
                                SubGhzRadioBrokerValidationUnsupportedPreset;
}

void subghz_scene_protocol_list_on_enter(void* context) {
    SubGhz* subghz = context;
    VariableItemList* list = subghz->variable_item_list;
    variable_item_list_reset(list);
    subghz_scene_protocol_list_free_contexts();

    size_t protocol_count = subghz_protocol_registry_count(&subghz_protocol_registry);
    protocol_list_item_contexts = malloc(sizeof(SubGhzProtocolListItemContext) * protocol_count);
    furi_check(protocol_list_item_contexts);

    for(size_t i = 0; i < protocol_count; i++) {
        const SubGhzProtocol* protocol =
            subghz_protocol_registry_get_by_index(&subghz_protocol_registry, i);
        if(!protocol) continue;

        protocol_list_item_contexts[i].subghz = subghz;
        protocol_list_item_contexts[i].protocol_name = protocol->name;
        const SubGhzRadioBrokerValidation validation =
            subghz_scene_protocol_list_validate(subghz, protocol);
        protocol_list_item_contexts[i].compatible = validation == SubGhzRadioBrokerValidationOk;

        VariableItem* item = variable_item_list_add(
            list,
            protocol->name,
            protocol_list_item_contexts[i].compatible ? 2 : 1,
            protocol_list_item_contexts[i].compatible ? subghz_scene_protocol_list_item_changed :
                                                        NULL,
            &protocol_list_item_contexts[i]);

        if(protocol_list_item_contexts[i].compatible) {
            bool is_disabled = subghz_last_settings_protocol_filter_contains(
                subghz->last_settings, protocol->name);
            variable_item_set_current_value_index(item, is_disabled ? 1 : 0);
            variable_item_set_current_value_text(item, is_disabled ? "OFF" : "ON");
        } else {
            variable_item_set_current_value_index(item, 0);
            variable_item_set_current_value_text(
                item, subghz_scene_protocol_list_validation_text(validation, protocol));
        }
    }

    variable_item_list_set_selected_item(
        list, scene_manager_get_scene_state(subghz->scene_manager, SubGhzSceneProtocolList));

    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdVariableItemList);
}

bool subghz_scene_protocol_list_on_event(void* context, SceneManagerEvent event) {
    SubGhz* subghz = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        scene_manager_set_scene_state(subghz->scene_manager, SubGhzSceneProtocolList, event.event);
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
