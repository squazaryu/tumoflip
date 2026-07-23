#include "../hermes_i.h"

static void hermes_scene_safety_callback(GuiButtonType button, InputType type, void* context) {
    HermesApp* app = context;
    if(type != InputTypeShort) return;

    const uint32_t event = (button == GuiButtonTypeRight) ?
                               HermesCustomEventSafetyContinue :
                               HermesCustomEventSafetyBack;
    view_dispatcher_send_custom_event(app->view_dispatcher, event);
}

void hermes_scene_safety_on_enter(void* context) {
    HermesApp* app = context;
    Widget* widget = app->widget;

    widget_reset(widget);
    widget_add_string_element(
        widget, 64, 3, AlignCenter, AlignTop, FontPrimary, "UART Safety");
    widget_add_text_box_element(
        widget,
        4,
        15,
        120,
        34,
        AlignCenter,
        AlignTop,
        "3.3V TTL only\nGND first. Never connect RS-232.\nDetection keeps TX released.",
        false);
    widget_add_button_element(
        widget, GuiButtonTypeLeft, "Back", hermes_scene_safety_callback, app);
    widget_add_button_element(
        widget, GuiButtonTypeRight, "Continue", hermes_scene_safety_callback, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, HermesViewWidget);
}

bool hermes_scene_safety_on_event(void* context, SceneManagerEvent event) {
    HermesApp* app = context;
    if(event.type == SceneManagerEventTypeBack) {
        view_dispatcher_stop(app->view_dispatcher);
        return true;
    }
    if(event.type != SceneManagerEventTypeCustom) return false;

    if(event.event == HermesCustomEventSafetyBack) {
        view_dispatcher_stop(app->view_dispatcher);
        return true;
    }
    if(event.event == HermesCustomEventSafetyContinue) {
        scene_manager_next_scene(app->scene_manager, app->safety_target);
        return true;
    }
    return false;
}

void hermes_scene_safety_on_exit(void* context) {
    HermesApp* app = context;
    widget_reset(app->widget);
}
