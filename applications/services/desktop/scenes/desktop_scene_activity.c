#include "../desktop_i.h"
#include "desktop_scene.h"
#include <bt/bt_service/bt.h>
#include <power/power_service/power.h>

void desktop_activity_callback(DesktopActivityAction action, void* context) {
    Desktop* desktop = context;
    /* Called by the view's input handler, outside its model lock. */
    view_dispatcher_send_custom_event(desktop->view_dispatcher, DesktopActivityEventBase + action);
}

void desktop_scene_activity_on_enter(void* context) {
    Desktop* desktop = context;
    desktop_activity_reset(desktop->activity, desktop->recovery_notice_pending);
    desktop->recovery_notice_pending = false;
    TumoSystemState state = furi_system_policy_snapshot();
    desktop_activity_update(desktop->activity, &state, furi_hal_power_insomnia_level());
    view_dispatcher_switch_to_view(desktop->view_dispatcher, DesktopViewIdActivity);
}

bool desktop_scene_activity_on_event(void* context, SceneManagerEvent event) {
    Desktop* desktop = context;
    if(event.type == SceneManagerEventTypeBack) {
        scene_manager_search_and_switch_to_previous_scene(
            desktop->scene_manager, DesktopSceneMain);
        return true;
    }
    if(event.type != SceneManagerEventTypeCustom) return false;
    switch(event.event) {
    case DesktopActivityEventBase + DesktopActivityPrevious:
        if(!scene_manager_search_and_switch_to_previous_scene(
               desktop->scene_manager, DesktopSceneQuickSettings))
            scene_manager_next_scene(desktop->scene_manager, DesktopSceneQuickSettings);
        break;
    case DesktopActivityEventBase + DesktopActivityPausePhone:
        if(furi_system_policy_pause_phone_preview()) {
            Bt* bt = furi_record_open(RECORD_BT);
            bt_disconnect(bt);
            furi_record_close(RECORD_BT);
        } else
            desktop_activity_blocked(desktop->activity);
        break;
    case DesktopActivityEventBase + DesktopActivityAllowPhone:
        if(!furi_system_policy_allow_phone_preview()) desktop_activity_blocked(desktop->activity);
        break;
    case DesktopActivityEventBase + DesktopActivityRestart:
        if(furi_system_policy_prepare_normal_restart()) {
            furi_hal_rtc_set_fault_data(0);
            Power* power = furi_record_open(RECORD_POWER);
            power_reboot(power, PowerBootModeNormal);
            furi_record_close(RECORD_POWER);
        } else
            desktop_activity_blocked(desktop->activity);
        break;
    default:
        return false;
    }
    return true;
}

void desktop_scene_activity_on_exit(void* context) {
    UNUSED(context);
}
