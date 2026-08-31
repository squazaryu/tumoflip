#include "menu.h"
#include "menu_style.h"

#include <assets_icons.h>
#include <furi.h>
#include <gui/canvas_i.h>
#include <gui/elements.h>
#include <gui/icon_animation_i.h>
#include <gui/icon_i.h>
#include <m-array.h>
#include <string.h>

struct Menu {
    View* view;
};

typedef struct {
    const char* label;
    IconAnimation* icon;
    uint32_t index;
    MenuItemCallback callback;
    void* callback_context;
} MenuItem;

ARRAY_DEF(MenuItemArray, MenuItem, M_POD_OPLIST); //-V658

#define M_OPL_MenuItemArray_t() ARRAY_OPLIST(MenuItemArray, M_POD_OPLIST)

#define MENU_SCREEN_WIDTH  128U
#define MENU_SCREEN_HEIGHT 64U

#define MENU_HEADER_BASELINE        10U
#define MENU_HEADER_DIVIDER_Y       13U
#define MENU_HEADER_LABEL_X         4U
#define MENU_HEADER_LABEL_MAX_WIDTH 94U
#define MENU_HEADER_COUNTER_X       124U

#define MENU_MATRIX_PAGE_SIZE       6U
#define MENU_MATRIX_COLUMNS         3U
#define MENU_MATRIX_ROWS            2U
#define MENU_MATRIX_CELL_WIDTH      42U
#define MENU_MATRIX_CELL_HEIGHT     24U
#define MENU_MATRIX_FIRST_CENTER_X  22U
#define MENU_MATRIX_FIRST_CENTER_Y  26U
#define MENU_MATRIX_SELECTED_WIDTH  36U
#define MENU_MATRIX_SELECTED_HEIGHT 19U

#define MENU_RAIL_PREVIOUS_X        20U
#define MENU_RAIL_SELECTED_X        64U
#define MENU_RAIL_NEXT_X            108U
#define MENU_RAIL_ICON_CENTER_Y     36U
#define MENU_RAIL_SIDE_WIDTH        26U
#define MENU_RAIL_SIDE_HEIGHT       25U
#define MENU_RAIL_SELECTED_WIDTH    38U
#define MENU_RAIL_SELECTED_HEIGHT   34U
#define MENU_RAIL_CONNECTOR_X       3U
#define MENU_RAIL_CONNECTOR_Y       36U
#define MENU_RAIL_CONNECTOR_WIDTH   122U
#define MENU_RAIL_PROGRESS_Y        59U
#define MENU_RAIL_PROGRESS_STEP     3U
#define MENU_RAIL_MAX_DOTS          32U
#define MENU_LABEL_SCRATCH_CAPACITY 64U

typedef struct {
    MenuItemArray_t items;
    size_t position;
    size_t vertical_offset;
    MenuStyle style;
    FuriString* label_scratch;
    size_t label_scratch_capacity;
} MenuModel;

static void menu_process_direction(Menu* menu, InputKey key);
static void menu_process_ok(Menu* menu);

static size_t menu_wrap_position(size_t position, size_t count) {
    return count ? position % count : 0;
}

static const char* menu_compact_name(const char* label) {
    if(strcmp(label, "125 kHz RFID") == 0) return "RFID";
    if(strcmp(label, "Sub-GHz") == 0) return "RF";
    if(strcmp(label, "Infrared") == 0) return "IR";
    if(strcmp(label, "iButton") == 0) return "iBtn";
    if(strcmp(label, "Bad USB") == 0) return "USB";
    if(strcmp(label, "Settings") == 0) return "Setup";
    return label;
}

static void menu_centered_icon(
    Canvas* canvas,
    MenuItem* item,
    int32_t x,
    int32_t y,
    size_t width,
    size_t height) {
    canvas_draw_icon_animation(
        canvas,
        x + ((int32_t)width - item->icon->icon->width) / 2,
        y + ((int32_t)height - item->icon->icon->height) / 2,
        item->icon);
}

static void menu_draw_fit_label(
    Canvas* canvas,
    MenuModel* model,
    const char* label,
    int32_t x,
    int32_t y,
    Align horizontal,
    Align vertical,
    size_t width) {
    furi_string_set_str(model->label_scratch, label);
    elements_string_fit_width(canvas, model->label_scratch, width);
    canvas_draw_str_aligned(
        canvas, x, y, horizontal, vertical, furi_string_get_cstr(model->label_scratch));
}

static void menu_draw_header(Canvas* canvas, MenuModel* model) {
    const size_t count = MenuItemArray_size(model->items);
    MenuItem* item = MenuItemArray_get(model->items, model->position);

    furi_string_printf(
        model->label_scratch, "%u/%u", (unsigned int)(model->position + 1U), (unsigned int)count);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(
        canvas,
        MENU_HEADER_COUNTER_X,
        MENU_HEADER_BASELINE - 1U,
        AlignRight,
        AlignBottom,
        furi_string_get_cstr(model->label_scratch));

    const size_t counter_width =
        canvas_string_width(canvas, furi_string_get_cstr(model->label_scratch));
    const size_t label_area_width = MENU_HEADER_COUNTER_X - MENU_HEADER_LABEL_X - 5U;
    size_t label_width = counter_width < label_area_width ? label_area_width - counter_width : 24U;
    if(label_width > MENU_HEADER_LABEL_MAX_WIDTH) label_width = MENU_HEADER_LABEL_MAX_WIDTH;
    if(label_width < 24U) label_width = 24U;

    canvas_set_font(canvas, FontPrimary);
    menu_draw_fit_label(
        canvas,
        model,
        item->label,
        MENU_HEADER_LABEL_X,
        MENU_HEADER_BASELINE,
        AlignLeft,
        AlignBottom,
        label_width);
    canvas_draw_line(
        canvas, 2, MENU_HEADER_DIVIDER_Y, MENU_SCREEN_WIDTH - 3U, MENU_HEADER_DIVIDER_Y);
}

static void menu_draw_list(Canvas* canvas, MenuModel* model) {
    const size_t position = model->position;
    const size_t count = MenuItemArray_size(model->items);

    for(uint8_t i = 0; i < 3; i++) {
        canvas_set_font(canvas, i == 1 ? FontPrimary : FontSecondary);
        const size_t item_i = (position + count + i - 1) % count;
        MenuItem* item = MenuItemArray_get(model->items, item_i);
        menu_centered_icon(canvas, item, 4, 3 + 22 * i, 14, 14);
        menu_draw_fit_label(
            canvas, model, item->label, 22, 14 + 22 * i, AlignLeft, AlignBottom, 98);
    }

    elements_frame(canvas, 0, 21, 123, 21);
    elements_scrollbar(canvas, position, count);
}

static size_t menu_matrix_first(size_t position, size_t count) {
    if(!count) return 0;
    position %= count;
    return position - (position % MENU_MATRIX_PAGE_SIZE);
}

static void menu_draw_signal_matrix(Canvas* canvas, MenuModel* model) {
    const size_t position = model->position;
    const size_t count = MenuItemArray_size(model->items);
    const size_t first = menu_matrix_first(position, count);

    menu_draw_header(canvas, model);

    for(uint8_t i = 0; i < MENU_MATRIX_PAGE_SIZE; i++) {
        const size_t item_i = first + i;
        if(item_i >= count) continue;

        const uint8_t column = i / MENU_MATRIX_ROWS;
        const uint8_t row = i % MENU_MATRIX_ROWS;
        const uint8_t center_x = MENU_MATRIX_FIRST_CENTER_X + column * MENU_MATRIX_CELL_WIDTH;
        const uint8_t center_y = MENU_MATRIX_FIRST_CENTER_Y + row * MENU_MATRIX_CELL_HEIGHT;
        const bool selected = item_i == position;
        MenuItem* item = MenuItemArray_get(model->items, item_i);

        if(selected) {
            const uint8_t selected_x = center_x - MENU_MATRIX_SELECTED_WIDTH / 2U;
            const uint8_t selected_y = center_y - MENU_MATRIX_SELECTED_HEIGHT / 2U;
            elements_slightly_rounded_box(
                canvas,
                selected_x,
                selected_y,
                MENU_MATRIX_SELECTED_WIDTH,
                MENU_MATRIX_SELECTED_HEIGHT);
            canvas_set_color(canvas, ColorWhite);
        } else {
            canvas_draw_line(canvas, center_x - 2U, center_y + 10U, center_x + 2U, center_y + 10U);
        }

        menu_centered_icon(canvas, item, center_x - 7U, center_y - 7U, 14U, 14U);

        if(selected) canvas_set_color(canvas, ColorBlack);
    }
}

static void menu_draw_rail_side_item(Canvas* canvas, MenuItem* item, uint8_t center_x) {
    const uint8_t frame_x = center_x - MENU_RAIL_SIDE_WIDTH / 2U;
    const uint8_t frame_y = MENU_RAIL_ICON_CENTER_Y - MENU_RAIL_SIDE_HEIGHT / 2U;
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(
        canvas, frame_x + 1U, frame_y + 1U, MENU_RAIL_SIDE_WIDTH - 2U, MENU_RAIL_SIDE_HEIGHT - 2U);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_rframe(canvas, frame_x, frame_y, MENU_RAIL_SIDE_WIDTH, MENU_RAIL_SIDE_HEIGHT, 2);
    menu_centered_icon(
        canvas, item, frame_x, frame_y, MENU_RAIL_SIDE_WIDTH, MENU_RAIL_SIDE_HEIGHT);
}

static void menu_draw_signal_rail(Canvas* canvas, MenuModel* model) {
    const size_t position = model->position;
    const size_t count = MenuItemArray_size(model->items);

    menu_draw_header(canvas, model);
    if(count > 2U) {
        canvas_draw_line(
            canvas,
            MENU_RAIL_CONNECTOR_X,
            MENU_RAIL_CONNECTOR_Y,
            MENU_RAIL_CONNECTOR_X + MENU_RAIL_CONNECTOR_WIDTH - 1U,
            MENU_RAIL_CONNECTOR_Y);
    } else if(count == 2U) {
        canvas_draw_line(
            canvas,
            MENU_RAIL_SELECTED_X + MENU_RAIL_SELECTED_WIDTH / 2U - 1U,
            MENU_RAIL_CONNECTOR_Y,
            MENU_RAIL_NEXT_X - MENU_RAIL_SIDE_WIDTH / 2U,
            MENU_RAIL_CONNECTOR_Y);
    }

    if(count > 1U) {
        const size_t side_position = count == 2U ? (position + 1U) % count :
                                                   (position + count - 1U) % count;
        MenuItem* side_item = MenuItemArray_get(model->items, side_position);
        menu_draw_rail_side_item(
            canvas, side_item, count == 2U ? MENU_RAIL_NEXT_X : MENU_RAIL_PREVIOUS_X);
    }
    if(count > 2U) {
        MenuItem* next = MenuItemArray_get(model->items, (position + 1U) % count);
        menu_draw_rail_side_item(canvas, next, MENU_RAIL_NEXT_X);
    }

    const uint8_t selected_x = MENU_RAIL_SELECTED_X - MENU_RAIL_SELECTED_WIDTH / 2U;
    const uint8_t selected_y = MENU_RAIL_ICON_CENTER_Y - MENU_RAIL_SELECTED_HEIGHT / 2U;
    elements_slightly_rounded_box(
        canvas, selected_x, selected_y, MENU_RAIL_SELECTED_WIDTH, MENU_RAIL_SELECTED_HEIGHT);
    canvas_set_color(canvas, ColorWhite);
    menu_centered_icon(
        canvas,
        MenuItemArray_get(model->items, position),
        selected_x,
        selected_y,
        MENU_RAIL_SELECTED_WIDTH,
        MENU_RAIL_SELECTED_HEIGHT);
    canvas_set_color(canvas, ColorBlack);

    if(count <= MENU_RAIL_MAX_DOTS) {
        const uint8_t dots_width = count > 1U ? (count - 1U) * MENU_RAIL_PROGRESS_STEP + 1U : 1U;
        const uint8_t dots_x = (MENU_SCREEN_WIDTH - dots_width) / 2U;
        for(size_t i = 0; i < count; i++) {
            const uint8_t dot_x = dots_x + i * MENU_RAIL_PROGRESS_STEP;
            if(i == position) {
                canvas_draw_box(canvas, dot_x - 1U, MENU_RAIL_PROGRESS_Y - 1U, 3, 3);
            } else {
                canvas_draw_dot(canvas, dot_x, MENU_RAIL_PROGRESS_Y);
            }
        }
    } else {
        canvas_draw_line(canvas, 8, MENU_RAIL_PROGRESS_Y, 119, MENU_RAIL_PROGRESS_Y);
        const uint8_t progress_x = 8U + (111U * position) / (count - 1U);
        canvas_draw_box(canvas, progress_x - 1U, MENU_RAIL_PROGRESS_Y - 1U, 3, 3);
    }
}

static void menu_draw_vertical(Canvas* canvas, MenuModel* model) {
    const size_t position = model->position;
    const size_t count = MenuItemArray_size(model->items);
    size_t first = model->vertical_offset;

    if(first >= position || first + 7 <= position) {
        const size_t max_offset = count > 8 ? count - 8 : 0;
        first = position > 4 ? position - 4 : 0;
        if(first > max_offset) first = max_offset;
        model->vertical_offset = first;
    }

    canvas_set_orientation(canvas, CanvasOrientationVertical);
    canvas_set_font(canvas, FontSecondary);
    for(size_t i = 0; i < 8; i++) {
        const size_t item_i = first + i;
        if(item_i >= count) continue;

        const uint8_t y = 16 * i;
        const bool selected = item_i == position;
        MenuItem* item = MenuItemArray_get(model->items, item_i);

        if(selected) {
            elements_slightly_rounded_box(canvas, 0, y, 64, 16);
            canvas_set_color(canvas, ColorWhite);
        }

        menu_centered_icon(canvas, item, 0, y, 16, 16);
        menu_draw_fit_label(canvas, model, item->label, 17, y + 12, AlignLeft, AlignBottom, 46);
        if(selected) canvas_set_color(canvas, ColorBlack);
    }
    canvas_set_orientation(canvas, CanvasOrientationHorizontal);
}

static void menu_draw_wii_vertical(Canvas* canvas, MenuModel* model) {
    const size_t position = model->position;
    const size_t count = MenuItemArray_size(model->items);
    size_t first = 0;

    if(count > 8 && position >= 6) {
        first = position - (position % 2) - 4;
        const size_t max_offset = count > 8 ? count - 8 : 0;
        if(first > max_offset) first = max_offset - (max_offset % 2);
    }

    canvas_set_orientation(canvas, CanvasOrientationVertical);
    canvas_set_font(canvas, FontSecondary);
    for(uint8_t i = 0; i < 8; i++) {
        const size_t item_i = first + i;
        if(item_i >= count) continue;

        const uint8_t x = (i % 2) * 32;
        const uint8_t y = (i / 2) * 32;
        const bool selected = item_i == position;
        MenuItem* item = MenuItemArray_get(model->items, item_i);

        if(selected) {
            elements_slightly_rounded_box(canvas, x, y, 31, 30);
            canvas_set_color(canvas, ColorWhite);
        } else {
            elements_frame(canvas, x, y, 31, 30);
        }

        menu_centered_icon(canvas, item, x, y, 31, 19);
        menu_draw_fit_label(
            canvas,
            model,
            menu_compact_name(item->label),
            x + 15,
            y + 27,
            AlignCenter,
            AlignBottom,
            27);

        if(selected) canvas_set_color(canvas, ColorBlack);
    }
    canvas_set_orientation(canvas, CanvasOrientationHorizontal);
}

static void menu_draw_callback(Canvas* canvas, void* _model) {
    MenuModel* model = _model;
    canvas_clear(canvas);

    if(!MenuItemArray_size(model->items)) {
        elements_scrollbar(canvas, 0, 0);
        return;
    }

    switch(model->style) {
    case MenuStyleWii:
        menu_draw_signal_matrix(canvas, model);
        break;
    case MenuStyleDsi:
        menu_draw_signal_rail(canvas, model);
        break;
    case MenuStyleVertical:
        menu_draw_vertical(canvas, model);
        break;
    case MenuStyleWiiVertical:
        menu_draw_wii_vertical(canvas, model);
        break;
    case MenuStyleList:
    default:
        menu_draw_list(canvas, model);
        break;
    }
}

static bool menu_input_callback(InputEvent* event, void* context) {
    Menu* menu = context;
    bool consumed = true;

    if(event->type == InputTypeShort || event->type == InputTypeRepeat) {
        switch(event->key) {
        case InputKeyUp:
        case InputKeyDown:
        case InputKeyLeft:
        case InputKeyRight:
            menu_process_direction(menu, event->key);
            break;
        case InputKeyOk:
            if(event->type != InputTypeRepeat) menu_process_ok(menu);
            break;
        default:
            consumed = false;
            break;
        }
    } else {
        consumed = false;
    }

    return consumed;
}

static void menu_enter(void* context) {
    Menu* menu = context;
    with_view_model(
        menu->view,
        MenuModel * model,
        {
            model->style = menu_style_load();
            model->vertical_offset = 0;
            if(MenuItemArray_size(model->items)) {
                MenuItem* item = MenuItemArray_get(model->items, model->position);
                icon_animation_start(item->icon);
            }
        },
        true);
}

static void menu_exit(void* context) {
    Menu* menu = context;
    with_view_model(
        menu->view,
        MenuModel * model,
        {
            if(MenuItemArray_size(model->items)) {
                MenuItem* item = MenuItemArray_get(model->items, model->position);
                icon_animation_stop(item->icon);
            }
        },
        false);
}

Menu* menu_alloc(void) {
    Menu* menu = malloc(sizeof(Menu));
    menu->view = view_alloc();
    view_set_context(menu->view, menu);
    view_allocate_model(menu->view, ViewModelTypeLocking, sizeof(MenuModel));
    view_set_draw_callback(menu->view, menu_draw_callback);
    view_set_input_callback(menu->view, menu_input_callback);
    view_set_enter_callback(menu->view, menu_enter);
    view_set_exit_callback(menu->view, menu_exit);

    with_view_model(
        menu->view,
        MenuModel * model,
        {
            MenuItemArray_init(model->items);
            model->position = 0;
            model->vertical_offset = 0;
            model->style = MenuStyleList;
            model->label_scratch = furi_string_alloc();
            furi_string_reserve(model->label_scratch, MENU_LABEL_SCRATCH_CAPACITY);
            model->label_scratch_capacity = MENU_LABEL_SCRATCH_CAPACITY;
        },
        true);

    return menu;
}

void menu_free(Menu* menu) {
    furi_check(menu);

    menu_reset(menu);
    with_view_model(
        menu->view,
        MenuModel * model,
        {
            MenuItemArray_clear(model->items);
            furi_string_free(model->label_scratch);
            model->label_scratch = NULL;
            model->label_scratch_capacity = 0;
        },
        false);
    view_free(menu->view);
    free(menu);
}

View* menu_get_view(Menu* menu) {
    furi_check(menu);
    return menu->view;
}

void menu_add_item(
    Menu* menu,
    const char* label,
    const Icon* icon,
    uint32_t index,
    MenuItemCallback callback,
    void* context) {
    furi_check(menu);
    furi_check(label);

    with_view_model(
        menu->view,
        MenuModel * model,
        {
            size_t label_capacity = strlen(label) + 1U;
            if(label_capacity < MENU_LABEL_SCRATCH_CAPACITY) {
                label_capacity = MENU_LABEL_SCRATCH_CAPACITY;
            }
            if(label_capacity > model->label_scratch_capacity) {
                furi_string_reserve(model->label_scratch, label_capacity);
                model->label_scratch_capacity = label_capacity;
            }
            MenuItem* item = MenuItemArray_push_new(model->items);
            item->label = label;
            item->icon = icon ? icon_animation_alloc(icon) : icon_animation_alloc(&A_Plugins_14);
            view_tie_icon_animation(menu->view, item->icon);
            item->index = index;
            item->callback = callback;
            item->callback_context = context;
        },
        true);
}

void menu_reset(Menu* menu) {
    furi_check(menu);
    with_view_model(
        menu->view,
        MenuModel * model,
        {
            for
                M_EACH(item, model->items, MenuItemArray_t) {
                    icon_animation_stop(item->icon);
                    icon_animation_free(item->icon);
                }

            MenuItemArray_reset(model->items);
            model->position = 0;
            model->vertical_offset = 0;
            furi_string_reset(model->label_scratch);
            model->label_scratch_capacity = 0;
        },
        true);
}

static void menu_set_position(Menu* menu, size_t position) {
    with_view_model(
        menu->view,
        MenuModel * model,
        {
            const size_t count = MenuItemArray_size(model->items);
            if(!count) return;

            position = menu_wrap_position(position, count);
            if(position == model->position) return;

            MenuItem* item = MenuItemArray_get(model->items, model->position);
            icon_animation_stop(item->icon);

            item = MenuItemArray_get(model->items, position);
            icon_animation_start(item->icon);
            model->position = position;
        },
        true);
}

void menu_set_selected_item(Menu* menu, uint32_t index) {
    furi_check(menu);

    with_view_model(
        menu->view,
        MenuModel * model,
        {
            size_t position = 0;
            MenuItemArray_it_t it;
            for(MenuItemArray_it(it, model->items); !MenuItemArray_end_p(it);
                MenuItemArray_next(it)) {
                if(index == MenuItemArray_cref(it)->index) break;
                position++;
            }

            if(position >= MenuItemArray_size(model->items)) position = 0;
            model->position = position;
        },
        true);
}

static size_t menu_next_position(MenuStyle style, InputKey key, size_t position, size_t count) {
    if(!count) return 0;
    position %= count;

    if(style == MenuStyleWiiVertical) {
        switch(key) {
        case InputKeyUp:
            key = InputKeyRight;
            break;
        case InputKeyDown:
            key = InputKeyLeft;
            break;
        case InputKeyRight:
            key = InputKeyDown;
            break;
        case InputKeyLeft:
            key = InputKeyUp;
            break;
        default:
            break;
        }
    }

    if(style == MenuStyleWii) {
        const size_t column = position / MENU_MATRIX_ROWS;
        const size_t row = position % MENU_MATRIX_ROWS;

        if(key == InputKeyUp || key == InputKeyDown) {
            const size_t target = column * MENU_MATRIX_ROWS + (key == InputKeyDown ? 1U : 0U);
            return target < count ? target : position;
        }

        if(key == InputKeyLeft || key == InputKeyRight) {
            const size_t row_columns = (count + MENU_MATRIX_ROWS - 1U - row) / MENU_MATRIX_ROWS;
            const size_t next_column = key == InputKeyRight ?
                                           (column + 1U) % row_columns :
                                           (column + row_columns - 1U) % row_columns;
            return next_column * MENU_MATRIX_ROWS + row;
        }

        return position;
    }

    if(style == MenuStyleDsi || style == MenuStyleVertical) {
        if(key == InputKeyLeft) {
            return position > 0U ? position - 1U : count - 1U;
        }
        if(key == InputKeyRight) {
            return position + 1U < count ? position + 1U : 0U;
        }
        return position;
    }

    if(style == MenuStyleWiiVertical) {
        if(key == InputKeyUp) {
            if(position >= 2U) return position - 2U;
            const size_t column = position % 2U;
            const size_t rows_in_column = (count + 1U - column) / 2U;
            return (rows_in_column - 1U) * 2U + column;
        }
        if(key == InputKeyDown) {
            const size_t target = position + 2U;
            if(target < count) return target;
            const size_t column = position % 2U;
            return column < count ? column : 0U;
        }
        if(key == InputKeyLeft || key == InputKeyRight) {
            if(position % 2U) return position - 1U;
            if(position + 1U < count) return position + 1U;
        }
        return position;
    }

    if(key == InputKeyUp) return position > 0U ? position - 1U : count - 1U;
    if(key == InputKeyDown) return position + 1U < count ? position + 1U : 0U;
    return position;
}

static void menu_process_direction(Menu* menu, InputKey key) {
    size_t position = 0;
    with_view_model(
        menu->view,
        MenuModel * model,
        {
            position = menu_next_position(
                model->style, key, model->position, MenuItemArray_size(model->items));
        },
        false);
    menu_set_position(menu, position);
}

static void menu_process_ok(Menu* menu) {
    MenuItem* item = NULL;
    with_view_model(
        menu->view,
        MenuModel * model,
        {
            if(MenuItemArray_size(model->items)) {
                item = MenuItemArray_get(model->items, model->position);
            }
        },
        true);
    if(item && item->callback) {
        item->callback(item->callback_context, item->index);
    }
}
