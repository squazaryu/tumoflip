#include "menu.h"
#include "menu_style.h"

#include <assets_icons.h>
#include <furi.h>
#include <gui/canvas_i.h>
#include <gui/elements.h>
#include <gui/icon_animation_i.h>
#include <gui/icon_i.h>
#include <m-array.h>

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

typedef struct {
    MenuItemArray_t items;
    size_t position;
    size_t vertical_offset;
    MenuStyle style;
} MenuModel;

static void menu_process_up(Menu* menu);
static void menu_process_down(Menu* menu);
static void menu_process_left(Menu* menu);
static void menu_process_right(Menu* menu);
static void menu_process_ok(Menu* menu);

#define menu_short_name(label) (label)

static size_t menu_wrap_position(size_t position, size_t count) {
    return count ? position % count : 0;
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
    const char* label,
    int32_t x,
    int32_t y,
    Align horizontal,
    Align vertical,
    size_t width) {
    FuriString* text = furi_string_alloc_set_str(label);
    elements_string_fit_width(canvas, text, width);
    canvas_draw_str_aligned(canvas, x, y, horizontal, vertical, furi_string_get_cstr(text));
    furi_string_free(text);
}

static void menu_draw_list(Canvas* canvas, MenuModel* model) {
    const size_t position = model->position;
    const size_t count = MenuItemArray_size(model->items);

    for(uint8_t i = 0; i < 3; i++) {
        canvas_set_font(canvas, i == 1 ? FontPrimary : FontSecondary);
        const size_t item_i = (position + count + i - 1) % count;
        MenuItem* item = MenuItemArray_get(model->items, item_i);
        menu_centered_icon(canvas, item, 4, 3 + 22 * i, 14, 14);
        canvas_draw_str(canvas, 22, 14 + 22 * i, item->label);
    }

    elements_frame(canvas, 0, 21, 123, 21);
    elements_scrollbar(canvas, position, count);
}

static void menu_draw_wii(Canvas* canvas, MenuModel* model) {
    const size_t position = model->position;
    const size_t count = MenuItemArray_size(model->items);
    size_t first = 0;

    if(count > 6 && position >= 4) {
        first = (position >= count - 2 + (count % 2)) ? position - (position % 2) - 4 :
                                                        position - (position % 2) - 2;
    }

    canvas_set_font(canvas, FontSecondary);
    for(uint8_t i = 0; i < 6; i++) {
        const size_t item_i = first + i;
        if(item_i >= count) continue;

        const uint8_t x = (i / 2) * 43 + 1;
        const uint8_t y = (i % 2) * 32;
        const bool selected = item_i == position;
        MenuItem* item = MenuItemArray_get(model->items, item_i);

        if(selected) {
            elements_slightly_rounded_box(canvas, x, y, 40, 30);
            canvas_set_color(canvas, ColorWhite);
        } else {
            elements_frame(canvas, x, y, 40, 30);
        }

        menu_centered_icon(canvas, item, x, y, 40, 19);
        menu_draw_fit_label(
            canvas,
            menu_short_name(item->label),
            x + 20,
            y + 27,
            AlignCenter,
            AlignBottom,
            36);

        if(selected) canvas_set_color(canvas, ColorBlack);
    }
}

static void menu_draw_dsi(Canvas* canvas, MenuModel* model) {
    const size_t position = model->position;
    const size_t count = MenuItemArray_size(model->items);

    for(int8_t i = -2; i <= 2; i++) {
        const size_t item_i = (position + count + i) % count;
        MenuItem* item = MenuItemArray_get(model->items, item_i);
        int32_t x = 64 + (30 * i);
        int32_t y = i == 0 ? 35 : 38;
        const size_t w = i == 0 ? 30 : 24;
        const size_t h = i == 0 ? 31 : 26;

        if(i == 0) {
            canvas_draw_frame(canvas, x - w / 2, y - h / 2, w, h + 5);
            canvas_set_font(canvas, FontPrimary);
            canvas_draw_str_aligned(canvas, 64, 12, AlignCenter, AlignBottom, item->label);
        } else {
            elements_slightly_rounded_frame(canvas, x - w / 2, y - h / 2, w, h);
        }

        menu_centered_icon(canvas, item, x - 8, y - 8, 16, 16);
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
        canvas_draw_str(canvas, 17, y + 12, menu_short_name(item->label));
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
            menu_short_name(item->label),
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
        menu_draw_wii(canvas, model);
        break;
    case MenuStyleDsi:
        menu_draw_dsi(canvas, model);
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
    InputKey key = event->key;

    if(event->type == InputTypeShort || event->type == InputTypeRepeat) {
        with_view_model(
            menu->view,
            MenuModel * model,
            {
                if(model->style == MenuStyleWiiVertical) {
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
            },
            false);

        switch(key) {
        case InputKeyUp:
            menu_process_up(menu);
            break;
        case InputKeyDown:
            menu_process_down(menu);
            break;
        case InputKeyLeft:
            menu_process_left(menu);
            break;
        case InputKeyRight:
            menu_process_right(menu);
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
        },
        true);

    return menu;
}

void menu_free(Menu* menu) {
    furi_check(menu);

    menu_reset(menu);
    with_view_model(menu->view, MenuModel * model, { MenuItemArray_clear(model->items); }, false);
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

static void menu_process_up(Menu* menu) {
    size_t position = 0;
    with_view_model(
        menu->view,
        MenuModel * model,
        {
            const size_t count = MenuItemArray_size(model->items);
            if(!count) return;

            position = model->position;
            switch(model->style) {
            case MenuStyleWii:
                position = (position % 2 || (position == count - 1 && count % 2)) ?
                               position - 1 :
                               position + 1;
                break;
            case MenuStyleDsi:
            case MenuStyleVertical:
                position = position > 0 ? position - 1 : count - 1;
                break;
            case MenuStyleWiiVertical:
                if(position >= 2) {
                    position -= 2;
                } else {
                    const size_t column = position % 2;
                    const size_t last_row = (count - 1) / 2;
                    position = last_row * 2 + column;
                    if(position >= count) position--;
                }
                break;
            default:
                position = position > 0 ? position - 1 : count - 1;
                break;
            }
            position = menu_wrap_position(position, count);
        },
        false);
    menu_set_position(menu, position);
}

static void menu_process_down(Menu* menu) {
    size_t position = 0;
    with_view_model(
        menu->view,
        MenuModel * model,
        {
            const size_t count = MenuItemArray_size(model->items);
            if(!count) return;

            position = model->position;
            switch(model->style) {
            case MenuStyleWii:
                position = (position % 2 || (position == count - 1 && count % 2)) ?
                               position - 1 :
                               position + 1;
                break;
            case MenuStyleDsi:
            case MenuStyleVertical:
                position = position < count - 1 ? position + 1 : 0;
                break;
            case MenuStyleWiiVertical:
                position += 2;
                if(position >= count) {
                    position %= 2;
                    if(position >= count) position = 0;
                }
                break;
            default:
                position = position < count - 1 ? position + 1 : 0;
                break;
            }
            position = menu_wrap_position(position, count);
        },
        false);
    menu_set_position(menu, position);
}

static void menu_process_left(Menu* menu) {
    size_t position = 0;
    with_view_model(
        menu->view,
        MenuModel * model,
        {
            const size_t count = MenuItemArray_size(model->items);
            if(!count) return;

            position = model->position;
            switch(model->style) {
            case MenuStyleWii:
                position = position < 2 ? (count % 2 ? count - 1 : count - 2 + position % 2) :
                                          position - 2;
                break;
            case MenuStyleDsi:
            case MenuStyleVertical:
                position = position > 0 ? position - 1 : count - 1;
                break;
            case MenuStyleWiiVertical:
                if(position % 2) {
                    position--;
                } else if(position + 1 < count) {
                    position++;
                }
                break;
            default:
                break;
            }
            position = menu_wrap_position(position, count);
        },
        false);
    menu_set_position(menu, position);
}

static void menu_process_right(Menu* menu) {
    size_t position = 0;
    with_view_model(
        menu->view,
        MenuModel * model,
        {
            const size_t count = MenuItemArray_size(model->items);
            if(!count) return;

            position = model->position;
            switch(model->style) {
            case MenuStyleWii:
                if(count % 2) {
                    position = position == count - 1 ? 0 :
                               position == count - 2 ? count - 1 :
                                                       position + 2;
                } else {
                    position += 2;
                }
                break;
            case MenuStyleDsi:
            case MenuStyleVertical:
                position = position < count - 1 ? position + 1 : 0;
                break;
            case MenuStyleWiiVertical:
                if(position % 2) {
                    position--;
                } else if(position + 1 < count) {
                    position++;
                }
                break;
            default:
                break;
            }
            position = menu_wrap_position(position, count);
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
