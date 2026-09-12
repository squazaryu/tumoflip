// App-local copy of the firmware VariableItemList; only its layout is customized.
#include "weather_variable_item_list.h"
#include <gui/elements.h>
#include <gui/canvas.h>
#include <furi.h>
#include <assets_icons.h>
#include <m-array.h>
#include <stdint.h>

struct WeatherVariableItem {
    const char* label;
    uint8_t current_value_index;
    FuriString* current_value_text;
    uint8_t values_count;
    WeatherVariableItemChangeCallback change_callback;
    bool locked;
    FuriString* locked_message;
    void* context;
};

ARRAY_DEF(WeatherVariableItemArray, WeatherVariableItem, M_POD_OPLIST); //-V658

struct WeatherVariableItemList {
    View* view;
    WeatherVariableItemListEnterCallback callback;
    void* context;
    FuriTimer* scroll_timer;
    FuriTimer* locked_timer;
};

typedef struct {
    WeatherVariableItemArray_t items;
    uint8_t position;
    uint8_t window_position;
    size_t scroll_counter;
    bool locked_message_visible;
} WeatherVariableItemListModel;

static void weather_variable_item_list_process_up(WeatherVariableItemList* weather_variable_item_list);
static void weather_variable_item_list_process_down(WeatherVariableItemList* weather_variable_item_list);
static void weather_variable_item_list_process_left(WeatherVariableItemList* weather_variable_item_list);
static void weather_variable_item_list_process_right(WeatherVariableItemList* weather_variable_item_list);
static void weather_variable_item_list_process_ok(WeatherVariableItemList* weather_variable_item_list);

static void weather_variable_item_list_draw_callback(Canvas* canvas, void* _model) {
    WeatherVariableItemListModel* model = _model;

    const uint8_t item_height = 16;
    const uint8_t item_width = 123;

    canvas_clear(canvas);

    uint8_t position = 0;
    WeatherVariableItemArray_it_t it;

    canvas_set_font(canvas, FontSecondary);
    for(WeatherVariableItemArray_it(it, model->items); !WeatherVariableItemArray_end_p(it);
        WeatherVariableItemArray_next(it)) {
        uint8_t item_position = position - model->window_position;
        uint8_t items_on_screen = 4;
        uint8_t y_offset = 0;

        if(item_position < items_on_screen) {
            const WeatherVariableItem* item = WeatherVariableItemArray_cref(it);
            uint8_t item_y = y_offset + (item_position * item_height);
            uint8_t item_text_y = item_y + item_height - 4;
            size_t scroll_counter = 0;

            if(position == model->position) {
                canvas_set_color(canvas, ColorBlack);
                elements_slightly_rounded_box(canvas, 0, item_y + 1, item_width, item_height - 2);
                canvas_set_color(canvas, ColorWhite);
                scroll_counter = model->scroll_counter;
                if(scroll_counter < 1) {
                    scroll_counter = 0;
                } else {
                    scroll_counter -= 1;
                }
            } else {
                canvas_set_color(canvas, ColorBlack);
            }

            const bool adjustable = item->values_count > 1;
            const uint8_t value_right = adjustable ? 110 : 118;
            const uint8_t value_width = MIN(
                canvas_string_width(canvas, furi_string_get_cstr(item->current_value_text)),
                60U);
            const uint8_t value_left = value_right - value_width;
            const uint8_t label_width = value_left - (adjustable ? 19U : 12U);
            if(item->current_value_index == 0 && furi_string_empty(item->current_value_text)) {
                // Only left text, no right text
                canvas_draw_str(canvas, 6, item_text_y, item->label);
            } else {
                elements_scrollable_text_line_str(
                    canvas,
                    6,
                    item_text_y,
                    label_width,
                    item->label,
                    scroll_counter,
                    false,
                    false);
            }

            if(item->locked) {
                canvas_draw_icon(canvas, 110, item_text_y - 8, &I_Lock_7x8);
            } else {
                if(item->current_value_index > 0) {
                    canvas_draw_str(canvas, value_left - 7, item_text_y, "<");
                }

                elements_scrollable_text_line_str(
                    canvas,
                    value_left,
                    item_text_y,
                    MAX(value_width, 1U),
                    furi_string_get_cstr(item->current_value_text),
                    scroll_counter,
                    false,
                    false);

                if(item->current_value_index < (item->values_count - 1)) {
                    canvas_draw_str(canvas, 115, item_text_y, ">");
                }
            }
        }

        position++;
    }

    elements_scrollbar(canvas, model->position, WeatherVariableItemArray_size(model->items));

    if(model->locked_message_visible) {
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_box(canvas, 8, 10, 110, 48);
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_icon(canvas, 10, 14, &I_WarningDolphin_45x42);
        canvas_draw_rframe(canvas, 8, 8, 112, 50, 3);
        canvas_draw_rframe(canvas, 9, 9, 110, 48, 2);
        elements_multiline_text_aligned(
            canvas,
            84,
            32,
            AlignCenter,
            AlignCenter,
            furi_string_get_cstr(
                WeatherVariableItemArray_get(model->items, model->position)->locked_message));
    }
}

void weather_variable_item_list_set_selected_item(WeatherVariableItemList* weather_variable_item_list, uint8_t index) {
    furi_check(weather_variable_item_list);
    with_view_model(
        weather_variable_item_list->view,
        WeatherVariableItemListModel * model,
        {
            uint8_t position = index;
            if(position >= WeatherVariableItemArray_size(model->items)) {
                position = 0;
            }

            model->position = position;
            model->window_position = position;

            if(model->window_position > 0) {
                model->window_position -= 1;
            }

            if(WeatherVariableItemArray_size(model->items) <= 4) {
                model->window_position = 0;
            } else {
                if(model->window_position >= (WeatherVariableItemArray_size(model->items) - 4)) {
                    model->window_position = (WeatherVariableItemArray_size(model->items) - 4);
                }
            }
        },
        true);
}

uint8_t weather_variable_item_list_get_selected_item_index(WeatherVariableItemList* weather_variable_item_list) {
    furi_check(weather_variable_item_list);
    WeatherVariableItemListModel* model = view_get_model(weather_variable_item_list->view);
    uint8_t idx = model->position;
    view_commit_model(weather_variable_item_list->view, false);
    return idx;
}

static bool weather_variable_item_list_input_callback(InputEvent* event, void* context) {
    WeatherVariableItemList* weather_variable_item_list = context;
    furi_assert(weather_variable_item_list);
    bool consumed = false;

    bool locked_message_visible = false;
    with_view_model(
        weather_variable_item_list->view,
        WeatherVariableItemListModel * model,
        { locked_message_visible = model->locked_message_visible; },
        false);

    if((!(event->type == InputTypePress) && !(event->type == InputTypeRelease)) &&
       locked_message_visible) {
        with_view_model(
            weather_variable_item_list->view,
            WeatherVariableItemListModel * model,
            { model->locked_message_visible = false; },
            true);
        consumed = true;
    } else if(event->type == InputTypeShort) {
        switch(event->key) {
        case InputKeyUp:
            consumed = true;
            weather_variable_item_list_process_up(weather_variable_item_list);
            break;
        case InputKeyDown:
            consumed = true;
            weather_variable_item_list_process_down(weather_variable_item_list);
            break;
        case InputKeyLeft:
            consumed = true;
            weather_variable_item_list_process_left(weather_variable_item_list);
            break;
        case InputKeyRight:
            consumed = true;
            weather_variable_item_list_process_right(weather_variable_item_list);
            break;
        case InputKeyOk:
            weather_variable_item_list_process_ok(weather_variable_item_list);
            break;
        default:
            break;
        }
    } else if(event->type == InputTypeRepeat) {
        switch(event->key) {
        case InputKeyUp:
            consumed = true;
            weather_variable_item_list_process_up(weather_variable_item_list);
            break;
        case InputKeyDown:
            consumed = true;
            weather_variable_item_list_process_down(weather_variable_item_list);
            break;
        case InputKeyLeft:
            consumed = true;
            weather_variable_item_list_process_left(weather_variable_item_list);
            break;
        case InputKeyRight:
            consumed = true;
            weather_variable_item_list_process_right(weather_variable_item_list);
            break;
        default:
            break;
        }
    }

    return consumed;
}

void weather_variable_item_list_process_up(WeatherVariableItemList* weather_variable_item_list) {
    with_view_model(
        weather_variable_item_list->view,
        WeatherVariableItemListModel * model,
        {
            uint8_t items_on_screen = 4;
            if(model->position > 0) {
                model->position--;

                if((model->position == model->window_position) && (model->window_position > 0)) {
                    model->window_position--;
                }
            } else {
                model->position = WeatherVariableItemArray_size(model->items) - 1;
                if(model->position > (items_on_screen - 1)) {
                    model->window_position = model->position - (items_on_screen - 1);
                }
            }
            model->scroll_counter = 0;
        },
        true);
}

void weather_variable_item_list_process_down(WeatherVariableItemList* weather_variable_item_list) {
    with_view_model(
        weather_variable_item_list->view,
        WeatherVariableItemListModel * model,
        {
            uint8_t items_on_screen = 4;
            if(model->position < (WeatherVariableItemArray_size(model->items) - 1)) {
                model->position++;
                if((model->position - model->window_position) > (items_on_screen - 2) &&
                   model->window_position <
                       (WeatherVariableItemArray_size(model->items) - items_on_screen)) {
                    model->window_position++;
                }
            } else {
                model->position = 0;
                model->window_position = 0;
            }
            model->scroll_counter = 0;
        },
        true);
}

WeatherVariableItem* weather_variable_item_list_get_selected_item(WeatherVariableItemListModel* model) {
    WeatherVariableItem* item = NULL;

    WeatherVariableItemArray_it_t it;
    uint8_t position = 0;
    for(WeatherVariableItemArray_it(it, model->items); !WeatherVariableItemArray_end_p(it);
        WeatherVariableItemArray_next(it)) {
        if(position == model->position) {
            break;
        }
        position++;
    }

    item = WeatherVariableItemArray_ref(it);

    furi_assert(item);
    return item;
}

void weather_variable_item_list_process_left(WeatherVariableItemList* weather_variable_item_list) {
    with_view_model(
        weather_variable_item_list->view,
        WeatherVariableItemListModel * model,
        {
            WeatherVariableItem* item = weather_variable_item_list_get_selected_item(model);
            if(item->locked) {
                model->locked_message_visible = true;
                furi_timer_start(
                    weather_variable_item_list->locked_timer, furi_kernel_get_tick_frequency() * 3);
            } else if(item->current_value_index > 0) {
                item->current_value_index--;
                model->scroll_counter = 0;
                if(item->change_callback) {
                    item->change_callback(item);
                }
            }
        },
        true);
}

void weather_variable_item_list_process_right(WeatherVariableItemList* weather_variable_item_list) {
    with_view_model(
        weather_variable_item_list->view,
        WeatherVariableItemListModel * model,
        {
            WeatherVariableItem* item = weather_variable_item_list_get_selected_item(model);
            if(item->locked) {
                model->locked_message_visible = true;
                furi_timer_start(
                    weather_variable_item_list->locked_timer, furi_kernel_get_tick_frequency() * 3);
            } else if(item->current_value_index < (item->values_count - 1)) {
                item->current_value_index++;
                model->scroll_counter = 0;
                if(item->change_callback) {
                    item->change_callback(item);
                }
            }
        },
        true);
}

void weather_variable_item_list_process_ok(WeatherVariableItemList* weather_variable_item_list) {
    with_view_model(
        weather_variable_item_list->view,
        WeatherVariableItemListModel * model,
        {
            WeatherVariableItem* item = weather_variable_item_list_get_selected_item(model);
            if(item->locked) {
                model->locked_message_visible = true;
                furi_timer_start(
                    weather_variable_item_list->locked_timer, furi_kernel_get_tick_frequency() * 3);
            } else if(weather_variable_item_list->callback) {
                weather_variable_item_list->callback(weather_variable_item_list->context, model->position);
            }
        },
        true);
}

static void weather_variable_item_list_scroll_timer_callback(void* context) {
    WeatherVariableItemList* weather_variable_item_list = context;
    with_view_model(
        weather_variable_item_list->view,
        WeatherVariableItemListModel * model,
        { model->scroll_counter++; },
        true);
}

void weather_variable_item_list_locked_timer_callback(void* context) {
    furi_assert(context);
    WeatherVariableItemList* weather_variable_item_list = context;

    with_view_model(
        weather_variable_item_list->view,
        WeatherVariableItemListModel * model,
        { model->locked_message_visible = false; },
        true);
}

WeatherVariableItemList* weather_variable_item_list_alloc(void) {
    WeatherVariableItemList* weather_variable_item_list = malloc(sizeof(WeatherVariableItemList));
    weather_variable_item_list->view = view_alloc();
    view_set_context(weather_variable_item_list->view, weather_variable_item_list);
    view_allocate_model(
        weather_variable_item_list->view, ViewModelTypeLocking, sizeof(WeatherVariableItemListModel));
    view_set_draw_callback(weather_variable_item_list->view, weather_variable_item_list_draw_callback);
    view_set_input_callback(weather_variable_item_list->view, weather_variable_item_list_input_callback);

    weather_variable_item_list->locked_timer = furi_timer_alloc(
        weather_variable_item_list_locked_timer_callback, FuriTimerTypeOnce, weather_variable_item_list);

    with_view_model(
        weather_variable_item_list->view,
        WeatherVariableItemListModel * model,
        {
            WeatherVariableItemArray_init(model->items);
            model->position = 0;
            model->window_position = 0;
            model->scroll_counter = 0;
        },
        true);
    weather_variable_item_list->scroll_timer = furi_timer_alloc(
        weather_variable_item_list_scroll_timer_callback, FuriTimerTypePeriodic, weather_variable_item_list);
    furi_timer_start(weather_variable_item_list->scroll_timer, 333);

    return weather_variable_item_list;
}

void weather_variable_item_list_free(WeatherVariableItemList* weather_variable_item_list) {
    furi_check(weather_variable_item_list);

    with_view_model(
        weather_variable_item_list->view,
        WeatherVariableItemListModel * model,
        {
            WeatherVariableItemArray_it_t it;
            for(WeatherVariableItemArray_it(it, model->items); !WeatherVariableItemArray_end_p(it);
                WeatherVariableItemArray_next(it)) {
                furi_string_free(WeatherVariableItemArray_ref(it)->current_value_text);
                furi_string_free(WeatherVariableItemArray_ref(it)->locked_message);
            }
            WeatherVariableItemArray_clear(model->items);
        },
        false);
    furi_timer_stop(weather_variable_item_list->scroll_timer);
    furi_timer_free(weather_variable_item_list->scroll_timer);
    furi_timer_stop(weather_variable_item_list->locked_timer);
    furi_timer_free(weather_variable_item_list->locked_timer);
    view_free(weather_variable_item_list->view);
    free(weather_variable_item_list);
}

void weather_variable_item_list_reset(WeatherVariableItemList* weather_variable_item_list) {
    furi_check(weather_variable_item_list);

    with_view_model(
        weather_variable_item_list->view,
        WeatherVariableItemListModel * model,
        {
            WeatherVariableItemArray_it_t it;
            for(WeatherVariableItemArray_it(it, model->items); !WeatherVariableItemArray_end_p(it);
                WeatherVariableItemArray_next(it)) {
                furi_string_free(WeatherVariableItemArray_ref(it)->current_value_text);
                furi_string_free(WeatherVariableItemArray_ref(it)->locked_message);
            }
            WeatherVariableItemArray_reset(model->items);
        },
        false);
}

View* weather_variable_item_list_get_view(WeatherVariableItemList* weather_variable_item_list) {
    furi_check(weather_variable_item_list);
    return weather_variable_item_list->view;
}

WeatherVariableItem* weather_variable_item_list_add(
    WeatherVariableItemList* weather_variable_item_list,
    const char* label,
    uint8_t values_count,
    WeatherVariableItemChangeCallback change_callback,
    void* context) {
    WeatherVariableItem* item = NULL;
    furi_check(label);
    furi_check(weather_variable_item_list);

    with_view_model(
        weather_variable_item_list->view,
        WeatherVariableItemListModel * model,
        {
            item = WeatherVariableItemArray_push_new(model->items);
            item->label = label;
            item->values_count = values_count;
            item->change_callback = change_callback;
            item->context = context;
            item->current_value_index = 0;
            item->current_value_text = furi_string_alloc();
            item->locked = false;
            item->locked_message = furi_string_alloc();
        },
        true);

    return item;
}

WeatherVariableItem* weather_variable_item_list_get(WeatherVariableItemList* weather_variable_item_list, uint8_t position) {
    WeatherVariableItem* item = NULL;
    furi_assert(weather_variable_item_list);

    with_view_model(
        weather_variable_item_list->view,
        WeatherVariableItemListModel * model,
        {
            if(position < WeatherVariableItemArray_size(model->items)) {
                item = WeatherVariableItemArray_get(model->items, position);
            }
        },
        true);

    return item;
}

void weather_variable_item_list_set_enter_callback(
    WeatherVariableItemList* weather_variable_item_list,
    WeatherVariableItemListEnterCallback callback,
    void* context) {
    furi_check(callback);
    with_view_model(
        weather_variable_item_list->view,
        WeatherVariableItemListModel * model,
        {
            UNUSED(model);
            weather_variable_item_list->callback = callback;
            weather_variable_item_list->context = context;
        },
        false);
}

void weather_variable_item_set_current_value_index(WeatherVariableItem* item, uint8_t current_value_index) {
    furi_check(item);
    item->current_value_index = current_value_index;
}

void weather_variable_item_set_values_count(WeatherVariableItem* item, uint8_t values_count) {
    furi_check(item);
    item->values_count = values_count;
}

void weather_variable_item_set_item_label(WeatherVariableItem* item, const char* label) {
    furi_check(item);
    furi_check(label);
    item->label = label;
}

void weather_variable_item_set_current_value_text(WeatherVariableItem* item, const char* current_value_text) {
    furi_check(item);
    furi_string_set(item->current_value_text, current_value_text);
}

void weather_variable_item_set_locked(WeatherVariableItem* item, bool locked, const char* locked_message) {
    item->locked = locked;
    if(locked) {
        furi_assert(locked_message);
        furi_string_set(item->locked_message, locked_message);
    }
}

uint8_t weather_variable_item_get_current_value_index(WeatherVariableItem* item) {
    furi_check(item);
    return item->current_value_index;
}

void* weather_variable_item_get_context(WeatherVariableItem* item) {
    furi_check(item);
    return item->context;
}
