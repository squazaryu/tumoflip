#include "../weather_station_app_i.h"
#include "../helpers/weather_station_types.h"
#include "../protocols/protocol_items.h"

void weather_station_scene_about_widget_callback(
    GuiButtonType result,
    InputType type,
    void* context) {
    WeatherStationApp* app = context;
    if(type == InputTypeShort) {
        view_dispatcher_send_custom_event(app->view_dispatcher, result);
    }
}

void weather_station_scene_about_on_enter(void* context) {
    WeatherStationApp* app = context;
    if(!app || !weather_station_ensure_widget(app)) return;

    FuriString* temp_str = furi_string_alloc();
    furi_string_printf(temp_str, "\e#Information\n");
    furi_string_cat_printf(temp_str, "Version: %s\n", FAP_VERSION);
    furi_string_cat_printf(temp_str, "Author: %s\n\n", WS_DEVELOPED);


    furi_string_cat_printf(temp_str, "\e#RX protocols\n");
    for(size_t i = 0U; i < weather_station_protocol_registry.size; i++) {
        const SubGhzProtocol* protocol = weather_station_protocol_registry.items[i];
        if(protocol && protocol->name) furi_string_cat_printf(temp_str, "%s\n", protocol->name);
    }

    widget_add_text_box_element(
        app->widget,
        0,
        0,
        128,
        14,
        AlignCenter,
        AlignBottom,
        "\e#\e!                                                      \e!\n",
        false);
    widget_add_text_box_element(
        app->widget,
        0,
        2,
        128,
        14,
        AlignCenter,
        AlignBottom,
        "\e#\e!         Weather Editor       \e!\n",
        false);
    widget_add_text_scroll_element(app->widget, 0, 16, 128, 44, furi_string_get_cstr(temp_str));
    app->lab_about_taps = 0U;
    app->lab_about_last_tick = 0U;
    furi_string_free(temp_str);

    view_dispatcher_switch_to_view(app->view_dispatcher, WeatherStationViewWidget);
    weather_station_release_inactive_gui_views(app, WeatherStationViewWidget);
}

bool weather_station_scene_about_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void weather_station_scene_about_on_exit(void* context) {
    WeatherStationApp* app = context;
    if(app->widget) widget_reset(app->widget);
}
