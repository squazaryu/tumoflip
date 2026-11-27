#include <applications.h>
#include <lib/toolbox/value_index.h>

#include "../desktop_settings_app.h"
#include "desktop_settings_scene.h"
#include "desktop_settings_scene_i.h"
#define MENU_STYLE_STATIC
#include <gui/modules/menu_style.h>
#include <power/power_service/power.h>

typedef enum {
    DesktopSettingsPinSetup = 0,
    DesktopSettingsPanelMode,
    DesktopSettingsShortcutSet,
    DesktopSettingsPanelMotion,
    DesktopSettingsAutoLockDelay,
    DesktopSettingsAutoPowerOff,
    DesktopSettingsLockAnimation,
    DesktopSettingsBatteryDisplay,
    DesktopSettingsClockDisplay,
    DesktopSettingsMainMenuStyle,
    DesktopSettingsChangeName,
    DesktopSettingsHappyMode,
    DesktopSettingsFavoriteLeftShort,
    DesktopSettingsFavoriteLeftLong,
    DesktopSettingsFavoriteRightShort,
    DesktopSettingsFavoriteRightLong,
    DesktopSettingsFavoriteOkLong,
} DesktopSettingsEntry;

uint32_t desktop_settings_entry_for_index(DesktopSettingsApp* app, uint32_t index) {
    return !app->supports_panel && index > 0 ? index + 3 : index;
}

static const char* const desktop_mode_text[] = {"Classic", "Panel"};
static const char* const desktop_motion_text[] = {"Off", "On"};

static void desktop_settings_scene_start_mode_changed(VariableItem* item) {
    DesktopSettingsApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    if(!app->supports_panel) return;
    if(index > DesktopModeDashboard) return;
    if(app->panel.mode != index) app->panel_dirty = true;
    app->panel.mode = index;
    app->shortcut_profile = index;
    app->menu_idx = DesktopSettingsPanelMode;
    variable_item_set_current_value_text(item, desktop_mode_text[index]);
    view_dispatcher_send_custom_event(app->view_dispatcher, DESKTOP_PANEL_REFRESH);
}

static void desktop_settings_scene_start_shortcut_set_changed(VariableItem* item) {
    DesktopSettingsApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    if(!app->supports_panel) return;
    if(index > DesktopModeDashboard) return;
    app->shortcut_profile = index;
    app->menu_idx = DesktopSettingsShortcutSet;
    variable_item_set_current_value_text(item, desktop_mode_text[index]);
    view_dispatcher_send_custom_event(app->view_dispatcher, DESKTOP_PANEL_REFRESH);
}

static void desktop_settings_scene_start_panel_motion_changed(VariableItem* item) {
    DesktopSettingsApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    if(!app->supports_panel) return;
    if(index > 1) return;
    if(app->panel.motion != index) app->panel_dirty = true;
    app->panel.motion = index;
    variable_item_set_current_value_text(item, desktop_motion_text[index]);
}

#define AUTO_LOCK_DELAY_COUNT 9
const char* const auto_lock_delay_text[AUTO_LOCK_DELAY_COUNT] = {
    "OFF",
    "10s",
    "15s",
    "30s",
    "60s",
    "90s",
    "2min",
    "5min",
    "10min",
};

const uint32_t auto_lock_delay_value[AUTO_LOCK_DELAY_COUNT] =
    {0, 10000, 15000, 30000, 60000, 90000, 120000, 300000, 600000};

#define USB_INHIBIT_AUTO_LOCK_DELAY_COUNT 2

const char* const usb_inhibit_auto_lock_delay_text[USB_INHIBIT_AUTO_LOCK_DELAY_COUNT] = {
    "OFF",
    "ON",
};

const uint32_t usb_inhibit_auto_lock_delay_value[USB_INHIBIT_AUTO_LOCK_DELAY_COUNT] = {0, 1};

#define LOCK_ANIMATION_COUNT 2
const char* const lock_animation_text[LOCK_ANIMATION_COUNT] = {
    "ON",
    "OFF",
};

const uint32_t lockscreen_skip_animation_value[LOCK_ANIMATION_COUNT] = {0, 1};

#define CLOCK_ENABLE_COUNT 2
const char* const clock_enable_text[CLOCK_ENABLE_COUNT] = {
    "OFF",
    "ON",
};

const uint32_t clock_enable_value[CLOCK_ENABLE_COUNT] = {0, 1};

#define BATTERY_VIEW_COUNT 6

const char* const battery_view_count_text[BATTERY_VIEW_COUNT] =
    {"Bar", "%", "Inv. %", "Retro 3", "Retro 5", "Bar %"};

const uint32_t displayBatteryPercentage_value[BATTERY_VIEW_COUNT] = {
    DISPLAY_BATTERY_BAR,
    DISPLAY_BATTERY_PERCENT,
    DISPLAY_BATTERY_INVERTED_PERCENT,
    DISPLAY_BATTERY_RETRO_3,
    DISPLAY_BATTERY_RETRO_5,
    DISPLAY_BATTERY_BAR_PERCENT};

static void desktop_settings_scene_start_var_list_enter_callback(void* context, uint32_t index) {
    DesktopSettingsApp* app = context;
    app->menu_idx = index;
    view_dispatcher_send_custom_event(
        app->view_dispatcher, desktop_settings_entry_for_index(app, index));
}

static void desktop_settings_scene_start_battery_view_changed(VariableItem* item) {
    DesktopSettingsApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);

    variable_item_set_current_value_text(item, battery_view_count_text[index]);
    app->settings.displayBatteryPercentage = index;
}

static void desktop_settings_scene_start_clock_enable_changed(VariableItem* item) {
    DesktopSettingsApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);

    variable_item_set_current_value_text(item, clock_enable_text[index]);
    app->settings.display_clock = index;
}

static void desktop_settings_scene_start_main_menu_style_changed(VariableItem* item) {
    const uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, menu_style_get_name(index));
    menu_style_save(index);
}

static void desktop_settings_scene_start_auto_lock_delay_changed(VariableItem* item) {
    DesktopSettingsApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);

    variable_item_set_current_value_text(item, auto_lock_delay_text[index]);
    app->settings.auto_lock_delay_ms = auto_lock_delay_value[index];
}

static void desktop_settings_scene_start_usb_inhibit_auto_lock_delay_changed(VariableItem* item) {
    DesktopSettingsApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);

    variable_item_set_current_value_text(item, usb_inhibit_auto_lock_delay_text[index]);
    app->settings.usb_inhibit_auto_lock = usb_inhibit_auto_lock_delay_value[index];
}

static void desktop_settings_scene_start_lock_animation_changed(VariableItem* item) {
    DesktopSettingsApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);

    variable_item_set_current_value_text(item, lock_animation_text[index]);
    app->settings.lockscreen_skip_animation = lockscreen_skip_animation_value[index];
}

void desktop_settings_scene_start_on_enter(void* context) {
    DesktopSettingsApp* app = context;
    VariableItemList* variable_item_list = app->variable_item_list;

    VariableItem* item;
    uint8_t value_index;

    variable_item_list_add(variable_item_list, "PIN Setup", 1, NULL, NULL);

    if(app->supports_panel) {
        item = variable_item_list_add(
            variable_item_list, "Desktop view", 2, desktop_settings_scene_start_mode_changed, app);
        variable_item_set_current_value_index(item, app->panel.mode);
        variable_item_set_current_value_text(item, desktop_mode_text[app->panel.mode]);
        item = variable_item_list_add(
            variable_item_list,
            "Edit shortcuts",
            2,
            desktop_settings_scene_start_shortcut_set_changed,
            app);
        variable_item_set_current_value_index(item, app->shortcut_profile);
        variable_item_set_current_value_text(item, desktop_mode_text[app->shortcut_profile]);
        item = variable_item_list_add(
            variable_item_list,
            "Panel motion",
            2,
            desktop_settings_scene_start_panel_motion_changed,
            app);
        variable_item_set_current_value_index(item, app->panel.motion);
        variable_item_set_current_value_text(item, desktop_motion_text[app->panel.motion]);
    }

    item = variable_item_list_add(
        variable_item_list,
        "Auto Lock Time",
        AUTO_LOCK_DELAY_COUNT,
        desktop_settings_scene_start_auto_lock_delay_changed,
        app);

    value_index = value_index_uint32(
        app->settings.auto_lock_delay_ms, auto_lock_delay_value, AUTO_LOCK_DELAY_COUNT);
    variable_item_set_current_value_index(item, value_index);
    variable_item_set_current_value_text(item, auto_lock_delay_text[value_index]);

    // USB connection Inhibit autolock OFF|ON|with opened RPC session
    item = variable_item_list_add(
        variable_item_list,
        "Auto Lock disarm by active USB session",
        USB_INHIBIT_AUTO_LOCK_DELAY_COUNT,
        desktop_settings_scene_start_usb_inhibit_auto_lock_delay_changed,
        app);

    value_index = value_index_uint32(
        app->settings.usb_inhibit_auto_lock,
        usb_inhibit_auto_lock_delay_value,
        USB_INHIBIT_AUTO_LOCK_DELAY_COUNT);
    variable_item_set_current_value_index(item, value_index);
    variable_item_set_current_value_text(item, usb_inhibit_auto_lock_delay_text[value_index]);

    item = variable_item_list_add(
        variable_item_list,
        "Lock Animation",
        LOCK_ANIMATION_COUNT,
        desktop_settings_scene_start_lock_animation_changed,
        app);

    value_index = value_index_uint32(
        app->settings.lockscreen_skip_animation,
        lockscreen_skip_animation_value,
        LOCK_ANIMATION_COUNT);
    variable_item_set_current_value_index(item, value_index);
    variable_item_set_current_value_text(item, lock_animation_text[value_index]);

    item = variable_item_list_add(
        variable_item_list,
        "Battery View",
        BATTERY_VIEW_COUNT,
        desktop_settings_scene_start_battery_view_changed,
        app);

    value_index = value_index_uint32(
        app->settings.displayBatteryPercentage,
        displayBatteryPercentage_value,
        BATTERY_VIEW_COUNT);
    variable_item_set_current_value_index(item, value_index);
    variable_item_set_current_value_text(item, battery_view_count_text[value_index]);

    item = variable_item_list_add(
        variable_item_list,
        "Show Clock",
        CLOCK_ENABLE_COUNT,
        desktop_settings_scene_start_clock_enable_changed,
        app);

    value_index =
        value_index_uint32(app->settings.display_clock, clock_enable_value, CLOCK_ENABLE_COUNT);
    variable_item_set_current_value_index(item, value_index);
    variable_item_set_current_value_text(item, clock_enable_text[value_index]);

    item = variable_item_list_add(
        variable_item_list,
        "Main Menu Style",
        MenuStyleCount,
        desktop_settings_scene_start_main_menu_style_changed,
        app);
    value_index = menu_style_load();
    variable_item_set_current_value_index(item, value_index);
    variable_item_set_current_value_text(item, menu_style_get_name(value_index));

    variable_item_list_add(variable_item_list, "Change Flipper Name", 0, NULL, app);

    variable_item_list_add(variable_item_list, "Happy Mode", 1, NULL, NULL);

    const bool panel = app->shortcut_profile == DesktopModeDashboard;
    item = variable_item_list_add(
        variable_item_list, panel ? "Left short" : "Favorite App - Left Short", 1, NULL, NULL);
    if(panel) variable_item_set_current_value_text(item, "Prev");
    variable_item_list_add(
        variable_item_list, panel ? "Left hold" : "Favorite App - Left Long", 1, NULL, NULL);
    item = variable_item_list_add(
        variable_item_list, panel ? "Right short" : "Favorite App - Right Short", 1, NULL, NULL);
    if(panel) variable_item_set_current_value_text(item, "Next");
    variable_item_list_add(
        variable_item_list, panel ? "Right hold" : "Favorite App - Right Long", 1, NULL, NULL);
    variable_item_list_add(
        variable_item_list, panel ? "OK hold" : "Favorite App - Ok Long", 1, NULL, NULL);

    variable_item_list_set_enter_callback(
        variable_item_list, desktop_settings_scene_start_var_list_enter_callback, app);
    variable_item_list_set_selected_item(variable_item_list, app->menu_idx);

    view_dispatcher_switch_to_view(app->view_dispatcher, DesktopSettingsAppViewVarItemList);
}

bool desktop_settings_scene_start_on_event(void* context, SceneManagerEvent event) {
    DesktopSettingsApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        switch(event.event) {
        case DESKTOP_PANEL_REFRESH:
            variable_item_list_reset(app->variable_item_list);
            desktop_settings_scene_start_on_enter(app);
            break;
        case DesktopSettingsPinSetup:
            scene_manager_next_scene(app->scene_manager, DesktopSettingsAppScenePinMenu);
            break;

            // case DesktopSettingsAutoLockDelay:
            // case DesktopSettingsLockAnimation:
            // case DesktopSettingsBatteryDisplay:
            // case DesktopSettingsClockDisplay:
            // Proces in default

        case DesktopSettingsChangeName:
            scene_manager_next_scene(app->scene_manager, DesktopSettingsAppSceneChangeName);
            break;

        case DesktopSettingsFavoriteLeftShort:
            if(app->shortcut_profile == DesktopModeDashboard) break;
            scene_manager_set_scene_state(
                app->scene_manager,
                DesktopSettingsAppSceneFavorite,
                SCENE_STATE_SET_FAVORITE_APP | FavoriteAppLeftShort);
            scene_manager_next_scene(app->scene_manager, DesktopSettingsAppSceneFavorite);
            break;
        case DesktopSettingsFavoriteLeftLong:
            scene_manager_set_scene_state(
                app->scene_manager,
                DesktopSettingsAppSceneFavorite,
                SCENE_STATE_SET_FAVORITE_APP | FavoriteAppLeftLong);
            scene_manager_next_scene(app->scene_manager, DesktopSettingsAppSceneFavorite);
            break;
        case DesktopSettingsFavoriteRightShort:
            if(app->shortcut_profile == DesktopModeDashboard) break;
            scene_manager_set_scene_state(
                app->scene_manager,
                DesktopSettingsAppSceneFavorite,
                SCENE_STATE_SET_FAVORITE_APP | FavoriteAppRightShort);
            scene_manager_next_scene(app->scene_manager, DesktopSettingsAppSceneFavorite);
            break;
        case DesktopSettingsFavoriteRightLong:
            scene_manager_set_scene_state(
                app->scene_manager,
                DesktopSettingsAppSceneFavorite,
                SCENE_STATE_SET_FAVORITE_APP | FavoriteAppRightLong);
            scene_manager_next_scene(app->scene_manager, DesktopSettingsAppSceneFavorite);
            break;
        case DesktopSettingsFavoriteOkLong:
            scene_manager_set_scene_state(
                app->scene_manager,
                DesktopSettingsAppSceneFavorite,
                SCENE_STATE_SET_FAVORITE_APP | FavoriteAppOkLong);
            scene_manager_next_scene(app->scene_manager, DesktopSettingsAppSceneFavorite);
            break;

        case DesktopSettingsHappyMode:
            scene_manager_next_scene(app->scene_manager, DesktopSettingsAppSceneHappyMode);
            break;

        default:
            break;
        }
        consumed = true;
    }
    return consumed;
}

void desktop_settings_scene_start_on_exit(void* context) {
    DesktopSettingsApp* app = context;
    variable_item_list_reset(app->variable_item_list);
    desktop_settings_save(&app->settings);

    // Trigger UI update in case we changed battery layout
    Power* power = furi_record_open(RECORD_POWER);
    power_trigger_ui_update(power);
    furi_record_close(RECORD_POWER);
}
