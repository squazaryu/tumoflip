#include "../bt_settings_app.h"
#include <furi_hal_bt.h>

enum BtSetting {
    BtSettingOff,
    BtSettingOn,
    BtSettingNum,
};

enum BtSettingIndex {
    BtSettingIndexSwitchBt,
    BtSettingIndexAppBridge,
    BtSettingIndexForgetDev,
};

const char* const bt_settings_text[BtSettingNum] = {
    "OFF",
    "ON",
};

static void bt_settings_scene_start_var_list_change_callback(VariableItem* item) {
    BtSettingsApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    uint8_t item_index = variable_item_list_get_selected_item_index(app->var_item_list);

    variable_item_set_current_value_text(item, bt_settings_text[index]);
    view_dispatcher_send_custom_event(app->view_dispatcher, (item_index << 8) | index);
}

static void bt_settings_scene_start_var_list_enter_callback(void* context, uint32_t index) {
    furi_assert(context);
    BtSettingsApp* app = context;
    if(index == BtSettingIndexForgetDev) {
        view_dispatcher_send_custom_event(
            app->view_dispatcher, BtSettingsCustomEventForgetDevices);
    }
}

void bt_settings_scene_start_on_enter(void* context) {
    BtSettingsApp* app = context;
    VariableItemList* var_item_list = app->var_item_list;
    VariableItem* item;

    if(furi_hal_bt_is_gatt_gap_supported()) {
        item = variable_item_list_add(
            var_item_list,
            "Bluetooth",
            BtSettingNum,
            bt_settings_scene_start_var_list_change_callback,
            app);
        if(app->settings.enabled) {
            variable_item_set_current_value_index(item, BtSettingOn);
            variable_item_set_current_value_text(item, bt_settings_text[BtSettingOn]);
        } else {
            variable_item_set_current_value_index(item, BtSettingOff);
            variable_item_set_current_value_text(item, bt_settings_text[BtSettingOff]);
        }
        item = variable_item_list_add(
            var_item_list,
            "App Bridge",
            BtSettingNum,
            bt_settings_scene_start_var_list_change_callback,
            app);
        if(app->settings.app_bridge_enabled) {
            variable_item_set_current_value_index(item, BtSettingOn);
            variable_item_set_current_value_text(item, bt_settings_text[BtSettingOn]);
        } else {
            variable_item_set_current_value_index(item, BtSettingOff);
            variable_item_set_current_value_text(item, bt_settings_text[BtSettingOff]);
        }
        variable_item_list_add(var_item_list, "Unpair All Devices", 1, NULL, NULL);
        variable_item_list_set_enter_callback(
            var_item_list, bt_settings_scene_start_var_list_enter_callback, app);
    } else {
        item = variable_item_list_add(var_item_list, "Bluetooth", 1, NULL, NULL);
        variable_item_set_current_value_text(item, "Broken");
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, BtSettingsAppViewVarItemList);
}

bool bt_settings_scene_start_on_event(void* context, SceneManagerEvent event) {
    BtSettingsApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == BtSettingsCustomEventForgetDevices) {
            scene_manager_next_scene(app->scene_manager, BtSettingsAppSceneForgetDevConfirm);
            return true;
        }

        const uint8_t item_index = event.event >> 8;
        const uint8_t value_index = event.event & 0xff;

        if(item_index == BtSettingIndexSwitchBt) {
            app->settings.enabled = value_index == BtSettingOn;
            consumed = true;
        } else if(item_index == BtSettingIndexAppBridge) {
            app->settings.app_bridge_enabled = value_index == BtSettingOn;
            consumed = true;
        }
    }

    return consumed;
}

void bt_settings_scene_start_on_exit(void* context) {
    BtSettingsApp* app = context;
    variable_item_list_reset(app->var_item_list);
}
