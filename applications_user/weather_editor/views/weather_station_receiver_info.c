#include "weather_station_receiver.h"
#include "../weather_station_app_i.h"
#include "weather_editor_icons.h"
#include "../protocols/ws_generic.h"
#include <input/input.h>
#include <gui/elements.h>
#include <float_tools.h>
#include <string.h>

struct WSReceiverInfo {
    View* view;
    FuriTimer* timer;
    WSReceiverInfoCallback callback;
    void* callback_context;
};

typedef struct {
    uint32_t curr_ts;
    FuriString* protocol_name;
    WSBlockGeneric* generic;
    bool display_fahrenheit;
} WSReceiverInfoModel;

void ws_view_receiver_info_set_callback(
    WSReceiverInfo* ws_receiver_info, WSReceiverInfoCallback callback, void* context) {
    furi_assert(ws_receiver_info);
    ws_receiver_info->callback = callback;
    ws_receiver_info->callback_context = context;
}

void ws_view_receiver_info_update(
    WSReceiverInfo* ws_receiver_info, FlipperFormat* fff, bool display_fahrenheit) {
    furi_assert(ws_receiver_info);
    furi_assert(fff);

    with_view_model(
        ws_receiver_info->view,
        WSReceiverInfoModel * model,
        {
            flipper_format_rewind(fff);
            flipper_format_read_string(fff, "Protocol", model->protocol_name);

            ws_block_generic_deserialize(model->generic, fff);

            model->curr_ts = furi_hal_rtc_get_timestamp();
            model->display_fahrenheit = display_fahrenheit;
        },
        true);
}

void ws_view_receiver_info_draw(Canvas* canvas, WSReceiverInfoModel* model) {
    char buffer[64];
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontSecondary);

    // The protocol and channel own separate areas, even with a long protocol name.
    FuriString* title = furi_string_alloc_set(model->protocol_name);
    elements_string_fit_width(canvas, title, 101);
    canvas_draw_str(canvas, 2, 8, furi_string_get_cstr(title));
    furi_string_free(title);
    if(model->generic->channel != WS_NO_CHANNEL) {
        snprintf(buffer, sizeof(buffer), "C%u", model->generic->channel);
        canvas_draw_str_aligned(canvas, 126, 8, AlignRight, AlignBottom, buffer);
    }

    if(model->generic->id != WS_NO_ID) {
        snprintf(buffer, sizeof(buffer), "ID %lX", (unsigned long)model->generic->id);
        canvas_draw_str(canvas, 2, 19, buffer);
    }
    snprintf(buffer, sizeof(buffer), "%ub", model->generic->data_count_bit);
    canvas_draw_str(canvas, 72, 19, buffer);
    if(model->generic->battery_low != WS_NO_BATT) {
        canvas_draw_str_aligned(
            canvas, 126, 19, AlignRight, AlignBottom,
            model->generic->battery_low ? "B:LOW" : "B:OK");
    }

    snprintf(buffer, sizeof(buffer), "%llX", (unsigned long long)model->generic->data);
    canvas_draw_str(canvas, 2, 30, buffer);
    if(model->generic->btn != WS_NO_BTN) {
        snprintf(buffer, sizeof(buffer), "Btn%u", model->generic->btn);
        canvas_draw_str_aligned(canvas, 126, 30, AlignRight, AlignBottom, buffer);
    }

    elements_bold_rounded_frame(canvas, 0, 37, 127, 26);
    if(!float_is_equal(model->generic->temp, WS_NO_TEMPERATURE)) {
        const double temperature = model->display_fahrenheit ?
            locale_celsius_to_fahrenheit(model->generic->temp) : model->generic->temp;
        canvas_draw_icon(canvas, 4, 42, &I_Therm_7x16);
        snprintf(buffer, sizeof(buffer), "%.1f%s", temperature,
                 model->display_fahrenheit ? "F" : "C");
        canvas_draw_str(canvas, 15, 53, buffer);
    }
    if(model->generic->humidity != WS_NO_HUMIDITY) {
        canvas_draw_icon(canvas, 65, 43, &I_Humid_8x13);
        snprintf(buffer, sizeof(buffer), "%u%%", model->generic->humidity);
        canvas_draw_str(canvas, 77, 53, buffer);
    }
    if(model->generic->timestamp > 0 && model->curr_ts >= model->generic->timestamp) {
        const uint32_t seconds = model->curr_ts - model->generic->timestamp;
        if(seconds < 60) snprintf(buffer, sizeof(buffer), "%lus", (unsigned long)seconds);
        else if(seconds < 3600) snprintf(buffer, sizeof(buffer), "%lum", (unsigned long)(seconds / 60));
        else snprintf(buffer, sizeof(buffer), "Old");
        canvas_draw_str_aligned(canvas, 123, 53, AlignRight, AlignBottom, buffer);
    }
}

bool ws_view_receiver_info_input(InputEvent* event, void* context) {
    furi_assert(context);
    WSReceiverInfo* ws_receiver_info = context;

    if(event->key == InputKeyBack) return false;
    if(event->type == InputTypeShort && event->key == InputKeyOk) {
        if(ws_receiver_info->callback) {
            ws_receiver_info->callback(
                WSCustomEventViewReceiverInfoActions, ws_receiver_info->callback_context);
        }
        return true;
    }
    return true;
}

static void ws_view_receiver_info_enter(void* context) {
    furi_assert(context);
    WSReceiverInfo* ws_receiver_info = context;

    furi_timer_start(ws_receiver_info->timer, 1000);
}

static void ws_view_receiver_info_exit(void* context) {
    furi_assert(context);
    WSReceiverInfo* ws_receiver_info = context;

    furi_timer_stop(ws_receiver_info->timer);

    with_view_model(
        ws_receiver_info->view,
        WSReceiverInfoModel * model,
        { furi_string_reset(model->protocol_name); },
        false);
}

static void ws_view_receiver_info_timer(void* context) {
    WSReceiverInfo* ws_receiver_info = context;
    // Force redraw
    with_view_model(
        ws_receiver_info->view,
        WSReceiverInfoModel * model,
        { model->curr_ts = furi_hal_rtc_get_timestamp(); },
        true);
}

WSReceiverInfo* ws_view_receiver_info_alloc() {
    WSReceiverInfo* ws_receiver_info = malloc(sizeof(WSReceiverInfo));
    furi_check(ws_receiver_info);

    // View allocation and configuration
    ws_receiver_info->view = view_alloc();
    ws_receiver_info->callback = NULL;
    ws_receiver_info->callback_context = NULL;

    view_allocate_model(ws_receiver_info->view, ViewModelTypeLocking, sizeof(WSReceiverInfoModel));
    view_set_context(ws_receiver_info->view, ws_receiver_info);
    view_set_draw_callback(ws_receiver_info->view, (ViewDrawCallback)ws_view_receiver_info_draw);
    view_set_input_callback(ws_receiver_info->view, ws_view_receiver_info_input);
    view_set_enter_callback(ws_receiver_info->view, ws_view_receiver_info_enter);
    view_set_exit_callback(ws_receiver_info->view, ws_view_receiver_info_exit);

    with_view_model(
        ws_receiver_info->view,
        WSReceiverInfoModel * model,
        {
            model->generic = malloc(sizeof(WSBlockGeneric));
            furi_check(model->generic);
            memset(model->generic, 0, sizeof(WSBlockGeneric));
            model->protocol_name = furi_string_alloc();
        },
        true);

    ws_receiver_info->timer =
        furi_timer_alloc(ws_view_receiver_info_timer, FuriTimerTypePeriodic, ws_receiver_info);

    return ws_receiver_info;
}

void ws_view_receiver_info_free(WSReceiverInfo* ws_receiver_info) {
    furi_assert(ws_receiver_info);

    furi_timer_free(ws_receiver_info->timer);

    with_view_model(
        ws_receiver_info->view,
        WSReceiverInfoModel * model,
        {
            furi_string_free(model->protocol_name);
            free(model->generic);
        },
        false);

    view_free(ws_receiver_info->view);
    free(ws_receiver_info);
}

View* ws_view_receiver_info_get_view(WSReceiverInfo* ws_receiver_info) {
    furi_assert(ws_receiver_info);
    return ws_receiver_info->view;
}
