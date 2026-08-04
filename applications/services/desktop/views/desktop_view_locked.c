#include <furi.h>
#include <furi_hal.h>

#include <gui/elements.h>
#include <gui/icon.h>
#include <gui/view.h>
#include <locale/locale.h>

#include <assets_icons.h>

#include "../desktop_i.h"
#include "desktop_view_locked.h"

#define DOOR_MOVING_INTERVAL_MS           (1000 / 16)
#define LOCKED_HINT_TIMEOUT_MS            (1000)
#define UNLOCKED_HINT_TIMEOUT_MS          (2000)
#define LOCKSCREEN_CLOCK_UPDATE_MS        (1000)
#define LOCKSCREEN_BATTERY_UPDATE_SECONDS (30)

#define DOOR_OFFSET_START (-55)
#define DOOR_OFFSET_END   (0)

#define DOOR_L_FINAL_POS (0)
#define DOOR_R_FINAL_POS (60)

#define UNLOCK_CNT         (3)
#define UNLOCK_RST_TIMEOUT (600)

struct DesktopViewLocked {
    View* view;
    DesktopViewLockedCallback callback;
    void* context;

    FuriTimer* timer;
    FuriTimer* clock_timer;
    uint8_t lock_count;
    uint32_t lock_lastpress;
    uint8_t battery_refresh_tick;
    bool skip_animation;
    bool clock_enabled;
};

typedef enum {
    DesktopViewLockedStateUnlocked,
    DesktopViewLockedStateLocked,
    DesktopViewLockedStateDoorsClosing,
    DesktopViewLockedStateLockedHintShown,
    DesktopViewLockedStateUnlockedHintShown
} DesktopViewLockedState;

typedef struct {
    bool pin_locked;
    bool clock_enabled;
    DateTime clock_datetime;
    uint8_t battery_pct;
    int8_t door_offset;
    DesktopViewLockedState view_state;
} DesktopViewLockedModel;

void desktop_view_locked_set_callback(
    DesktopViewLocked* locked_view,
    DesktopViewLockedCallback callback,
    void* context) {
    furi_assert(locked_view);
    furi_assert(callback);
    locked_view->callback = callback;
    locked_view->context = context;
}

static void locked_view_timer_callback(void* context) {
    DesktopViewLocked* locked_view = context;
    locked_view->callback(DesktopLockedEventUpdate, locked_view->context);
}

static void locked_view_clock_timer_callback(void* context) {
    DesktopViewLocked* locked_view = context;
    if(locked_view->callback) {
        locked_view->callback(DesktopLockedEventClockUpdate, locked_view->context);
    }
}

static void desktop_view_locked_refresh_clock(DesktopViewLocked* locked_view) {
    if(!locked_view->clock_enabled) return;

    DesktopViewLockedModel* model = view_get_model(locked_view->view);
    furi_hal_rtc_get_datetime(&model->clock_datetime);
    if(locked_view->battery_refresh_tick == 0) {
        model->battery_pct = MIN(furi_hal_power_get_pct(), 100);
    }
    locked_view->battery_refresh_tick =
        (locked_view->battery_refresh_tick + 1) % LOCKSCREEN_BATTERY_UPDATE_SECONDS;
    view_commit_model(locked_view->view, true);
}

static void desktop_view_locked_clock_start(DesktopViewLocked* locked_view) {
    if(!locked_view->clock_enabled) return;

    locked_view->battery_refresh_tick = 0;
    desktop_view_locked_refresh_clock(locked_view);
    furi_timer_start(locked_view->clock_timer, furi_ms_to_ticks(LOCKSCREEN_CLOCK_UPDATE_MS));
}

static void desktop_view_locked_clock_stop(DesktopViewLocked* locked_view) {
    furi_timer_stop(locked_view->clock_timer);
}

static void desktop_view_locked_doors_draw(Canvas* canvas, DesktopViewLockedModel* model) {
    int32_t offset = model->door_offset;
    int32_t door_left_x = DOOR_L_FINAL_POS + offset;
    int32_t door_right_x = DOOR_R_FINAL_POS - offset;
    size_t height = icon_get_height(&I_DoorLeft_70x55);
    canvas_draw_icon(canvas, door_left_x, canvas_height(canvas) - height, &I_DoorLeft_70x55);
    canvas_draw_icon(canvas, door_right_x, canvas_height(canvas) - height, &I_DoorRight_70x55);
}

static bool desktop_view_locked_doors_move(DesktopViewLockedModel* model) {
    bool stop = false;
    if(model->door_offset < DOOR_OFFSET_END) {
        model->door_offset = CLAMP(model->door_offset + 5, DOOR_OFFSET_END, DOOR_OFFSET_START);
        stop = true;
    }

    return stop;
}

static void desktop_view_locked_clock_draw(Canvas* canvas, DesktopViewLockedModel* model) {
    const DateTime* datetime = &model->clock_datetime;
    const bool format_12h = locale_get_time_format() == LocaleTimeFormat12h;
    uint8_t hour = datetime->hour;
    const char* meridian = NULL;

    if(format_12h) {
        meridian = hour >= 12 ? "PM" : "AM";
        hour %= 12;
        if(hour == 0) hour = 12;
    }

    char time[12];
    snprintf(time, sizeof(time), "%02u:%02u:%02u", hour, datetime->minute, datetime->second);

    char date[16];
    const LocaleDateFormat date_format = locale_get_date_format();
    if(date_format == LocaleDateFormatMDY) {
        snprintf(
            date, sizeof(date), "%02u.%02u.%04u", datetime->month, datetime->day, datetime->year);
    } else if(date_format == LocaleDateFormatYMD) {
        snprintf(
            date, sizeof(date), "%04u.%02u.%02u", datetime->year, datetime->month, datetime->day);
    } else {
        snprintf(
            date, sizeof(date), "%02u.%02u.%04u", datetime->day, datetime->month, datetime->year);
    }

    char details[28];
    if(format_12h) {
        snprintf(details, sizeof(details), "%s  %u%%  %s", date, model->battery_pct, meridian);
    } else {
        snprintf(details, sizeof(details), "%s  %u%%", date, model->battery_pct);
    }

    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(
        canvas,
        0,
        STATUS_BAR_Y_SHIFT,
        canvas_width(canvas),
        canvas_height(canvas) - STATUS_BAR_Y_SHIFT);
    canvas_set_color(canvas, ColorBlack);

    canvas_set_font(canvas, FontBigNumbers);
    canvas_draw_str_aligned(canvas, 64, 34, AlignCenter, AlignCenter, time);

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 53, AlignCenter, AlignCenter, details);
}

static void desktop_view_locked_clock_clear(Canvas* canvas) {
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(
        canvas,
        0,
        STATUS_BAR_Y_SHIFT,
        canvas_width(canvas),
        canvas_height(canvas) - STATUS_BAR_Y_SHIFT);
    canvas_set_color(canvas, ColorBlack);
}

static void desktop_view_locked_update_hint_icon_timeout(DesktopViewLocked* locked_view) {
    DesktopViewLockedModel* model = view_get_model(locked_view->view);
    const bool change_state = (model->view_state == DesktopViewLockedStateLocked) &&
                              !model->pin_locked;
    if(change_state) {
        model->view_state = DesktopViewLockedStateLockedHintShown;
    }
    view_commit_model(locked_view->view, change_state);
    furi_timer_start(locked_view->timer, LOCKED_HINT_TIMEOUT_MS);
}

void desktop_view_locked_update(DesktopViewLocked* locked_view) {
    DesktopViewLockedModel* model = view_get_model(locked_view->view);
    DesktopViewLockedState view_state = model->view_state;

    if(view_state == DesktopViewLockedStateDoorsClosing &&
       !desktop_view_locked_doors_move(model)) {
        locked_view->callback(DesktopLockedEventDoorsClosed, locked_view->context);
        model->view_state = DesktopViewLockedStateLocked;
    } else if(view_state == DesktopViewLockedStateLockedHintShown) {
        model->view_state = DesktopViewLockedStateLocked;
    } else if(view_state == DesktopViewLockedStateUnlockedHintShown) {
        model->view_state = DesktopViewLockedStateUnlocked;
    }

    view_commit_model(locked_view->view, true);

    if(view_state != DesktopViewLockedStateDoorsClosing) {
        furi_timer_stop(locked_view->timer);
    }
}

static void desktop_view_locked_draw(Canvas* canvas, void* model) {
    DesktopViewLockedModel* m = model;
    DesktopViewLockedState view_state = m->view_state;
    canvas_set_color(canvas, ColorBlack);

    if(m->clock_enabled && view_state == DesktopViewLockedStateLocked) {
        desktop_view_locked_clock_draw(canvas, m);
    } else if(m->clock_enabled && view_state == DesktopViewLockedStateLockedHintShown) {
        desktop_view_locked_clock_clear(canvas);
    }

    if(view_state == DesktopViewLockedStateDoorsClosing) {
        desktop_view_locked_doors_draw(canvas, m);
        canvas_set_font(canvas, FontPrimary);
        elements_multiline_text_framed(canvas, 42, 30 + STATUS_BAR_Y_SHIFT, "Locked");
    } else if(view_state == DesktopViewLockedStateLockedHintShown) {
        canvas_set_font(canvas, FontSecondary);
        elements_bold_rounded_frame(canvas, 14, 2 + STATUS_BAR_Y_SHIFT, 99, 48);
        elements_multiline_text(canvas, 65, 20 + STATUS_BAR_Y_SHIFT, "To unlock\npress:");
        canvas_draw_icon(canvas, 65, 36 + STATUS_BAR_Y_SHIFT, &I_Pin_back_arrow_10x8);
        canvas_draw_icon(canvas, 80, 36 + STATUS_BAR_Y_SHIFT, &I_Pin_back_arrow_10x8);
        canvas_draw_icon(canvas, 95, 36 + STATUS_BAR_Y_SHIFT, &I_Pin_back_arrow_10x8);
        canvas_draw_icon(canvas, 16, 7 + STATUS_BAR_Y_SHIFT, &I_WarningDolphin_45x42);
        canvas_draw_dot(canvas, 17, 61);
    } else if(view_state == DesktopViewLockedStateUnlockedHintShown) {
        canvas_set_font(canvas, FontPrimary);
        elements_multiline_text_framed(canvas, 42, 30 + STATUS_BAR_Y_SHIFT, "Unlocked");
    }
}

View* desktop_view_locked_get_view(DesktopViewLocked* locked_view) {
    furi_assert(locked_view);
    return locked_view->view;
}

static bool desktop_view_locked_input(InputEvent* event, void* context) {
    furi_assert(event);
    furi_assert(context);

    bool is_changed = false;
    const uint32_t press_time = furi_get_tick();
    DesktopViewLocked* locked_view = context;
    DesktopViewLockedModel* model = view_get_model(locked_view->view);
    if(model->view_state == DesktopViewLockedStateUnlockedHintShown &&
       event->type == InputTypePress) {
        model->view_state = DesktopViewLockedStateUnlocked;
        is_changed = true;
    }
    const DesktopViewLockedState view_state = model->view_state;
    const bool pin_locked = model->pin_locked;
    view_commit_model(locked_view->view, is_changed);

    if(view_state == DesktopViewLockedStateUnlocked) {
        return false;
    } else if(view_state == DesktopViewLockedStateLocked && pin_locked) {
        locked_view->callback(DesktopLockedEventShowPinInput, locked_view->context);
    } else if(
        view_state == DesktopViewLockedStateLocked ||
        view_state == DesktopViewLockedStateLockedHintShown) {
        if(press_time - locked_view->lock_lastpress > UNLOCK_RST_TIMEOUT) {
            locked_view->lock_lastpress = press_time;
            locked_view->lock_count = 0;
        }

        desktop_view_locked_update_hint_icon_timeout(locked_view);

        if(event->key == InputKeyBack) {
            if(event->type == InputTypeShort) {
                locked_view->lock_lastpress = press_time;
                locked_view->lock_count++;
                if(locked_view->lock_count == UNLOCK_CNT) {
                    locked_view->callback(DesktopLockedEventUnlocked, locked_view->context);
                }
            }
        } else {
            locked_view->lock_count = 0;
        }

        locked_view->lock_lastpress = press_time;
    }

    return true;
}

DesktopViewLocked* desktop_view_locked_alloc(void) {
    DesktopViewLocked* locked_view = malloc(sizeof(DesktopViewLocked));
    locked_view->view = view_alloc();
    locked_view->callback = NULL;
    locked_view->context = NULL;
    locked_view->battery_refresh_tick = 0;
    locked_view->skip_animation = false;
    locked_view->clock_enabled = false;
    locked_view->timer =
        furi_timer_alloc(locked_view_timer_callback, FuriTimerTypePeriodic, locked_view);
    locked_view->clock_timer =
        furi_timer_alloc(locked_view_clock_timer_callback, FuriTimerTypePeriodic, locked_view);

    view_allocate_model(locked_view->view, ViewModelTypeLocking, sizeof(DesktopViewLockedModel));
    view_set_context(locked_view->view, locked_view);
    view_set_draw_callback(locked_view->view, desktop_view_locked_draw);
    view_set_input_callback(locked_view->view, desktop_view_locked_input);

    return locked_view;
}

void desktop_view_locked_free(DesktopViewLocked* locked_view) {
    furi_assert(locked_view);
    furi_timer_free(locked_view->clock_timer);
    furi_timer_free(locked_view->timer);
    view_free(locked_view->view);
    free(locked_view);
}

void desktop_view_locked_set_skip_animation(DesktopViewLocked* locked_view, bool skip_animation) {
    furi_assert(locked_view);
    locked_view->skip_animation = skip_animation;
}

void desktop_view_locked_set_clock_enabled(DesktopViewLocked* locked_view, bool clock_enabled) {
    furi_assert(locked_view);
    locked_view->clock_enabled = clock_enabled;

    DesktopViewLockedModel* model = view_get_model(locked_view->view);
    model->clock_enabled = clock_enabled;
    const DesktopViewLockedState view_state = model->view_state;
    view_commit_model(locked_view->view, true);

    if(!clock_enabled) {
        desktop_view_locked_clock_stop(locked_view);
    } else if(
        view_state == DesktopViewLockedStateLocked ||
        view_state == DesktopViewLockedStateDoorsClosing ||
        view_state == DesktopViewLockedStateLockedHintShown) {
        desktop_view_locked_clock_start(locked_view);
    }
}

void desktop_view_locked_update_clock(DesktopViewLocked* locked_view) {
    furi_assert(locked_view);
    desktop_view_locked_refresh_clock(locked_view);
}

void desktop_view_locked_close_doors(DesktopViewLocked* locked_view) {
    DesktopViewLockedModel* model = view_get_model(locked_view->view);
    furi_assert(model->view_state == DesktopViewLockedStateLocked);

    if(locked_view->skip_animation) {
        model->door_offset = DOOR_OFFSET_END;
        view_commit_model(locked_view->view, true);
        locked_view->callback(DesktopLockedEventDoorsClosed, locked_view->context);
        return;
    }

    model->view_state = DesktopViewLockedStateDoorsClosing;
    model->door_offset = DOOR_OFFSET_START;
    view_commit_model(locked_view->view, true);
    furi_timer_start(locked_view->timer, DOOR_MOVING_INTERVAL_MS);
}

void desktop_view_locked_lock(DesktopViewLocked* locked_view, bool pin_locked) {
    DesktopViewLockedModel* model = view_get_model(locked_view->view);
    furi_assert(model->view_state == DesktopViewLockedStateUnlocked);
    model->view_state = DesktopViewLockedStateLocked;
    model->pin_locked = pin_locked;
    view_commit_model(locked_view->view, true);

    desktop_view_locked_clock_start(locked_view);
}

void desktop_view_locked_unlock(DesktopViewLocked* locked_view) {
    desktop_view_locked_clock_stop(locked_view);
    locked_view->lock_count = 0;
    DesktopViewLockedModel* model = view_get_model(locked_view->view);
    model->view_state = DesktopViewLockedStateUnlockedHintShown;
    model->pin_locked = false;
    view_commit_model(locked_view->view, true);
    furi_timer_start(locked_view->timer, UNLOCKED_HINT_TIMEOUT_MS);
}

bool desktop_view_locked_is_locked_hint_visible(DesktopViewLocked* locked_view) {
    DesktopViewLockedModel* model = view_get_model(locked_view->view);
    const DesktopViewLockedState view_state = model->view_state;
    view_commit_model(locked_view->view, false);
    return view_state == DesktopViewLockedStateLockedHintShown ||
           view_state == DesktopViewLockedStateLocked;
}
