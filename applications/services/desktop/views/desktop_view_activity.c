#include "desktop_view_activity.h"
#include <furi.h>
#include <gui/elements.h>
#include <assets_icons.h>
#include <gui/canvas_i.h>
#include "../desktop_panel_i.h"

typedef struct {
    TumoSystemState state;
    uint16_t holds;
    uint8_t page;
    bool confirm, blocked;
    bool home, motion_enabled;
    uint32_t began;
    int16_t offset;
    int8_t direction;
    uint8_t icon_frame;
} DesktopActivityModel;

struct DesktopActivityView {
    View* view;
    void (*callback)(DesktopActivityAction, void*);
    void* context;
};

static void
    desktop_activity_icon(Canvas* canvas, int32_t x, int32_t y, const Icon* icon, uint8_t frame) {
    uint8_t count = icon_get_frame_count(icon);
    if(count)
        canvas_draw_bitmap(
            canvas,
            x,
            y,
            icon_get_width(icon),
            icon_get_height(icon),
            icon_get_frame_data(icon, frame % count));
}

static void desktop_activity_badge(
    Canvas* canvas,
    uint8_t center,
    const Icon* icon,
    const char* label,
    const char* value,
    uint8_t frame) {
    canvas_draw_rframe(canvas, center - 21, 25, 42, 26, 3);
    canvas_set_font(canvas, strlen(value) > 4 ? FontBatteryPercent : FontPrimary);
    canvas_draw_str_aligned(canvas, center, 36, AlignCenter, AlignBottom, value);
    canvas_set_font(canvas, FontSecondary);
    const int32_t x = center - (icon_get_width(icon) + 2 + canvas_string_width(canvas, label)) / 2;
    desktop_activity_icon(canvas, x, 47 - icon_get_height(icon), icon, frame);
    canvas_draw_str(canvas, x + icon_get_width(icon) + 2, 47, label);
}

static void desktop_activity_hero(
    Canvas* canvas,
    const Icon* icon,
    const char* title,
    const char* line1,
    const char* line2,
    uint8_t frame) {
    desktop_activity_icon(
        canvas, (36 - icon_get_width(icon)) / 2, 38 - icon_get_height(icon) / 2, icon, frame);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 40, 32, title);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 40, 41, line1);
    canvas_draw_str(canvas, 40, 49, line2);
}

static void desktop_activity_draw_page(Canvas* canvas, void* context) {
    const DesktopActivityModel* m = context;
    const TumoSystemState* s = &m->state;
    static const char* const titles[] = {"Activity", "Idle & sleep", "Recovery"};
    char value[24];
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 21, titles[m->page]);
    canvas_set_font(canvas, FontSecondary);
    snprintf(value, sizeof(value), "%u/3", m->page + 1U);
    canvas_draw_str_aligned(canvas, 126, 21, AlignRight, AlignBottom, value);

    const char* button = m->home ? "Apps" : NULL;
    if(!m->home && m->blocked) {
        desktop_activity_hero(
            canvas, &A_Clock_14, "Still busy", "Finish file tasks", "or close the app.", 0);
        button = "OK";
    } else if(!m->home && m->confirm) {
        desktop_activity_hero(
            canvas,
            &I_Warning_30x23,
            m->page == 0 ? "Disconnect?" : "Restart?",
            m->page == 0 ? "BLE link will close." : "Normal boot",
            "User files kept",
            0);
        button = "Confirm";
    } else if(m->page == 0) {
        snprintf(value, sizeof(value), "%u", s->writes);
        desktop_activity_badge(
            canvas,
            21,
            m->home && s->writes ? &A_Round_loader_8x8 : &I_SDcardMounted_11x8,
            "Files",
            value,
            m->icon_frame);
        desktop_activity_badge(
            canvas,
            64,
            m->home && s->transfer ? &A_Round_loader_8x8 : &I_Bluetooth_Idle_5x8,
            "Phone",
            s->transfer ? "Busy" : "Idle",
            m->icon_frame);
        snprintf(value, sizeof(value), "%u/%u", s->phone_previews, s->previews);
        desktop_activity_badge(canvas, 107, &I_Rpc_active_7x8, "Screen", value, 0);
        if(!m->home && !s->phone_allowed)
            button = "Allow preview";
        else if(!m->home && s->phone_previews)
            button = "Pause preview";
    } else if(m->page == 1) {
        snprintf(value, sizeof(value), "Sleep holds: %u", m->holds);
        desktop_activity_hero(
            canvas,
            &A_Clock_14,
            m->home ? (m->motion_enabled ? "Panel live" : "Panel static") :
                      (s->display_on ? "Backlight on" : "Backlight off"),
            value,
            s->foreground ? "App: running" : "App: idle",
            m->icon_frame);
    } else {
        snprintf(value, sizeof(value), "Early faults: %u/3", s->boot_crashes);
        desktop_activity_hero(
            canvas,
            s->recovery ? &I_Warning_30x23 : &I_FaceNormal_29x14,
            s->recovery ? "Safe start" : "Normal boot",
            value,
            "User files kept",
            0);
        if(!m->home && s->recovery) button = "Restart";
    }
    canvas_draw_icon(canvas, 4, 56, &I_ButtonLeftSmall_3x5);
    canvas_draw_icon(canvas, 121, 56, &I_ButtonRightSmall_3x5);
    if(button) elements_button_center(canvas, button);
}

static void desktop_activity_draw(Canvas* canvas, void* context) {
    const DesktopActivityModel* m = context;
    canvas_clear(canvas);
    const int32_t x = canvas->offset_x, y = canvas->offset_y;
    const size_t width = canvas_width(canvas), height = canvas_height(canvas);
    if(m->home && m->offset) canvas_frame_set(canvas, x + m->offset, y, width, height);
    desktop_activity_draw_page(canvas, context);
    if(m->home && m->offset) canvas_frame_set(canvas, x, y, width, height);
}

static bool desktop_activity_input(InputEvent* event, void* context) {
    DesktopActivityView* activity = context;
    if(event->type != InputTypeShort || event->key == InputKeyBack) return false;
    DesktopActivityAction action = DesktopActivityPrevious;
    bool notify = false;
    bool consumed = true;
    with_view_model(
        activity->view,
        DesktopActivityModel * m,
        {
            if(m->home) {
                consumed = event->key == InputKeyLeft || event->key == InputKeyRight;
                if(consumed) {
                    m->direction = event->key == InputKeyRight ? 1 : -1;
                    m->page = desktop_panel_page(m->page, m->direction);
                    m->began = furi_get_tick();
                    m->offset =
                        desktop_panel_offset(m->began, m->began, m->direction, m->motion_enabled);
                    if(!m->motion_enabled) m->direction = 0;
                    action = DesktopActivityHomeChanged;
                    notify = true;
                }
            } else if(event->key == InputKeyLeft || event->key == InputKeyRight) {
                if(event->key == InputKeyLeft && m->page == 0)
                    notify = true;
                else
                    m->page = event->key == InputKeyRight ? (m->page + 1U) % 3U : m->page - 1U;
                m->confirm = m->blocked = false;
            } else if(event->key == InputKeyOk) {
                if(m->blocked) {
                    m->blocked = false;
                    m->confirm = false;
                } else if(m->page == 0 && !m->state.phone_allowed) {
                    action = DesktopActivityAllowPhone;
                    notify = true;
                } else if(
                    (m->page == 0 && m->state.phone_previews) ||
                    (m->page == 2 && m->state.recovery)) {
                    action = m->page == 0 ? DesktopActivityPausePhone : DesktopActivityRestart;
                    notify = m->confirm;
                    m->confirm = !m->confirm;
                }
            }
        },
        consumed);
    if(notify) activity->callback(action, activity->context);
    return consumed;
}

DesktopActivityView*
    desktop_activity_alloc(void (*callback)(DesktopActivityAction, void*), void* context) {
    DesktopActivityView* activity = malloc(sizeof(*activity));
    activity->view = view_alloc();
    activity->callback = callback;
    activity->context = context;
    view_allocate_model(activity->view, ViewModelTypeLocking, sizeof(DesktopActivityModel));
    view_set_context(activity->view, activity);
    view_set_draw_callback(activity->view, desktop_activity_draw);
    view_set_input_callback(activity->view, desktop_activity_input);
    return activity;
}

View* desktop_activity_get_view(DesktopActivityView* activity) {
    return activity->view;
}

void desktop_activity_reset(DesktopActivityView* activity, bool recovery_page) {
    with_view_model(
        activity->view,
        DesktopActivityModel * m,
        {
            m->page = recovery_page ? 2 : 0;
            m->confirm = m->blocked = false;
            m->offset = m->direction = m->icon_frame = 0;
        },
        true);
}

void desktop_activity_update(
    DesktopActivityView* activity,
    const TumoSystemState* state,
    uint16_t holds) {
    bool changed;
    with_view_model(
        activity->view,
        DesktopActivityModel * m,
        {
            changed = memcmp(&m->state, state, offsetof(TumoSystemState, transfer_until)) != 0 ||
                      m->holds != holds;
            m->state = *state;
            m->holds = holds;
        },
        changed);
}

void desktop_activity_blocked(DesktopActivityView* activity) {
    with_view_model(
        activity->view,
        DesktopActivityModel * m,
        {
            m->blocked = true;
            m->confirm = false;
        },
        true);
}

void desktop_activity_set_home(DesktopActivityView* activity, bool home) {
    with_view_model(
        activity->view,
        DesktopActivityModel * m,
        {
            m->home = home;
            m->motion_enabled = false;
            m->confirm = m->blocked = false;
            m->offset = m->direction = m->icon_frame = 0;
        },
        true);
}

uint32_t desktop_activity_home_tick(
    DesktopActivityView* activity,
    uint32_t now,
    bool alive,
    bool animate) {
    uint32_t period = 0;
    bool changed = false;
    with_view_model(
        activity->view,
        DesktopActivityModel * m,
        {
            if(m->home) {
                bool awake = alive && animate && m->state.display_on && !m->state.recovery;
                m->motion_enabled = awake;
                int16_t offset = desktop_panel_offset(m->began, now, m->direction, awake);
                uint8_t frame = awake ? desktop_panel_icon_frame(now, 40) : 0;
                changed = offset != m->offset || frame != m->icon_frame;
                m->offset = offset;
                m->icon_frame = frame;
                if(!offset) m->direction = 0;
                period = desktop_panel_period(
                    awake, offset != 0, m->page, m->state.transfer, m->state.writes);
            }
        },
        changed);
    return period;
}
