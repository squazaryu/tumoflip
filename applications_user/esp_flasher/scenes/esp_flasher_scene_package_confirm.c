#include "../esp_flasher_app_i.h"

static void package_confirm_callback(GuiButtonType result, InputType type, void* context) {
    if(type != InputTypeShort) return;
    EspFlasherApp* app = context;
    view_dispatcher_send_custom_event(
        app->view_dispatcher,
        result == GuiButtonTypeRight ? EspFlasherEventPackageFlash : EspFlasherEventPackageBack);
}

void esp_flasher_scene_package_confirm_on_enter(void* context) {
    EspFlasherApp* app = context;
    widget_add_text_box_element(
        app->widget, 0, 0, 128, 12, AlignCenter, AlignTop, "\e#Flash verified package?", false);
    widget_add_text_scroll_element(app->widget, 2, 12, 124, 40, app->package_summary);
    widget_add_button_element(
        app->widget, GuiButtonTypeLeft, "Back", package_confirm_callback, app);
    widget_add_button_element(
        app->widget, GuiButtonTypeRight, "Flash", package_confirm_callback, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, EspFlasherAppViewWidget);
}

bool esp_flasher_scene_package_confirm_on_event(void* context, SceneManagerEvent event) {
    EspFlasherApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event == EspFlasherEventPackageBack) {
        app->package_mode = false;
        scene_manager_previous_scene(app->scene_manager);
        return true;
    }
    if(event.event == EspFlasherEventPackageFlash) {
        app->reset = false;
        app->boot = true;
        app->quickflash = true;
        // Physical C5 acceptance showed that programming at 921600 can complete while
        // the following ROM MD5 command times out. Keep the exact C5 package profile at
        // the conservative UART rate; other accepted package profiles retain turbo.
        app->turbospeed =
            app->package_plan.target != EspFlashPackageTargetEsp32C5;
        scene_manager_next_scene(app->scene_manager, EspFlasherSceneConsoleOutput);
        return true;
    }
    return false;
}

void esp_flasher_scene_package_confirm_on_exit(void* context) {
    EspFlasherApp* app = context;
    widget_reset(app->widget);
}
