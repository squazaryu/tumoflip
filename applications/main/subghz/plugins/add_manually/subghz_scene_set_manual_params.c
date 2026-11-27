#include <subghz/subghz_i.h>
#include <gui/modules/number_input.h>
#include "subghz_manual_params.h"

bool subghz_scene_set_type_proceed_advanced(SubGhz* subghz);

// Owned only while this scene's FAL is mapped. At most one input view exists.
static NumberInput* manual_input;
static bool manual_guard_time;
static int32_t manual_pending;

static uint32_t subghz_manual_number_back(void* context) {
    (void)context;
    return SubGhzViewIdVariableItemList;
}

static void subghz_manual_refresh(SubGhz* subghz) {
    VariableItemList* list = subghz->variable_item_list;
    int band = subghz_manual_band_index(subghz->gen_info->freq, subghz->gen_info->mod);
    if(band >= 0) {
        variable_item_set_item_label(
            variable_item_list_get(list, 0), subghz_manual_band_get((unsigned)band)->mod);
        variable_item_set_current_value_index(variable_item_list_get(list, 0), (uint8_t)band);
        variable_item_set_current_value_text(
            variable_item_list_get(list, 0), subghz_manual_band_get((unsigned)band)->name);
    }
    SubGhzManualTiming limits = {0};
    bool timing = subghz->gen_info->type == GenData &&
                  subghz_manual_timing(subghz->gen_info->data.name, &limits);
    char value[16];
    if(timing)
        snprintf(value, sizeof(value), "%lu us", (unsigned long)subghz->gen_info->data.te);
    else
        strcpy(value, "Fixed");
    variable_item_set_current_value_text(variable_item_list_get(list, 1), value);
    if(timing && limits.gt_min)
        snprintf(value, sizeof(value), "%lu x TE", (unsigned long)subghz->gen_info->data.gt);
    else
        strcpy(value, "Fixed");
    variable_item_set_current_value_text(variable_item_list_get(list, 2), value);
}

static void subghz_manual_radio_changed(VariableItem* item) {
    SubGhz* subghz = variable_item_get_context(item);
    const SubGhzManualBand* band =
        subghz_manual_band_get(variable_item_get_current_value_index(item));
    if(!band) return;
    subghz->gen_info->freq = band->frequency;
    subghz->gen_info->mod = band->mod;
    variable_item_set_item_label(item, band->mod);
    variable_item_set_current_value_text(item, band->name);
}

static void subghz_manual_number_result(void* context, int32_t value) {
    SubGhz* subghz = context;
    SubGhzManualTiming limits;
    if(subghz->gen_info->type != GenData ||
       !subghz_manual_timing(subghz->gen_info->data.name, &limits) ||
       !subghz_manual_value_valid(
           value,
           manual_guard_time ? limits.gt_min : limits.te_min,
           manual_guard_time ? limits.gt_max : limits.te_max))
        return;
    manual_pending = value;
    view_dispatcher_send_custom_event(subghz->view_dispatcher, SubGhzManualEventNumberDone);
}

static void subghz_manual_enter(void* context, uint32_t index) {
    SubGhz* subghz = context;
    if(index == 3) {
        view_dispatcher_send_custom_event(subghz->view_dispatcher, SubGhzManualEventContinue);
        return;
    }
    if(index != 1 && index != 2) return;
    SubGhzManualTiming limits;
    if(subghz->gen_info->type != GenData ||
       !subghz_manual_timing(subghz->gen_info->data.name, &limits) ||
       (index == 2 && !limits.gt_min))
        return;
    manual_guard_time = index == 2;
    if(!manual_input) {
        manual_input = number_input_alloc();
        if(!manual_input) {
            furi_string_set(subghz->error_str, "Not enough memory");
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneShowError);
            return;
        }
        view_set_previous_callback(number_input_get_view(manual_input), subghz_manual_number_back);
        view_dispatcher_add_view(
            subghz->view_dispatcher, SubGhzViewIdNumberInput, number_input_get_view(manual_input));
    }
    number_input_set_header_text(
        manual_input, manual_guard_time ? "Enter GT in TE" : "Enter TE in us");
    number_input_set_result_callback(
        manual_input,
        subghz_manual_number_result,
        subghz,
        (int32_t)(manual_guard_time ? subghz->gen_info->data.gt : subghz->gen_info->data.te),
        manual_guard_time ? limits.gt_min : limits.te_min,
        manual_guard_time ? limits.gt_max : limits.te_max);
    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdNumberInput);
}

void subghz_scene_set_manual_params_on_enter(void* context) {
    SubGhz* subghz = context;
    VariableItemList* list = subghz->variable_item_list;
    variable_item_list_reset(list);
    variable_item_list_add(
        list, "Radio", (uint8_t)subghz_manual_band_count(), subghz_manual_radio_changed, subghz);
    variable_item_list_add(list, "TE", 1, NULL, subghz);
    variable_item_list_add(list, "GT", 1, NULL, subghz);
    variable_item_list_add(list, "Continue", 1, NULL, subghz);
    subghz_manual_refresh(subghz);
    variable_item_list_set_enter_callback(list, subghz_manual_enter, subghz);
    variable_item_list_set_selected_item(list, 0);
    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdVariableItemList);
}

bool subghz_scene_set_manual_params_on_event(void* context, SceneManagerEvent event) {
    SubGhz* subghz = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event == SubGhzManualEventNumberDone) {
        SubGhzManualTiming limits;
        if(manual_input && subghz->gen_info->type == GenData &&
           subghz_manual_timing(subghz->gen_info->data.name, &limits) &&
           subghz_manual_value_valid(
               manual_pending,
               manual_guard_time ? limits.gt_min : limits.te_min,
               manual_guard_time ? limits.gt_max : limits.te_max)) {
            if(manual_guard_time)
                subghz->gen_info->data.gt = (uint32_t)manual_pending;
            else
                subghz->gen_info->data.te = (uint32_t)manual_pending;
            subghz_manual_refresh(subghz);
            view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdVariableItemList);
        }
        return true;
    }
    if(event.event == SubGhzManualEventContinue) {
        if(subghz_manual_band_index(subghz->gen_info->freq, subghz->gen_info->mod) < 0 ||
           subghz_setting_get_inx_preset_by_name(
               subghz_txrx_get_setting(subghz->txrx), subghz->gen_info->mod) < 0) {
            furi_string_set(subghz->error_str, "Radio preset missing");
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneShowError);
            return true;
        }
        return subghz_scene_set_type_proceed_advanced(subghz);
    }
    return false;
}

void subghz_scene_set_manual_params_on_exit(void* context) {
    SubGhz* subghz = context;
    if(manual_input) {
        // Removing an active view stops the dispatcher; switch before detaching.
        view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdWidget);
        view_dispatcher_remove_view(subghz->view_dispatcher, SubGhzViewIdNumberInput);
        number_input_free(manual_input);
        manual_input = NULL;
    }
    variable_item_list_set_enter_callback(subghz->variable_item_list, NULL, NULL);
    variable_item_list_reset(subghz->variable_item_list);
}
