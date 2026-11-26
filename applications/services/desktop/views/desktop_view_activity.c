#include "desktop_view_activity.h"
#include <furi.h>
#include <gui/elements.h>
#include <assets_icons.h>

typedef struct {
    TumoSystemState state;
    uint16_t holds;
    uint8_t page;
    bool confirm, blocked;
} DesktopActivityModel;

struct DesktopActivityView {
    View* view;
    void (*callback)(DesktopActivityAction, void*);
    void* context;
};

static void desktop_activity_badge(
    Canvas* canvas,
    uint8_t center,
    const Icon* icon,
    const char* label,
    const char* value) {
    canvas_draw_rframe(canvas, center - 21, 25, 42, 26, 3);
    canvas_set_font(canvas, strlen(value) > 4 ? FontBatteryPercent : FontPrimary);
    canvas_draw_str_aligned(canvas, center, 36, AlignCenter, AlignBottom, value);
    canvas_set_font(canvas, FontSecondary);
    const int32_t x = center - (icon_get_width(icon) + 2 + canvas_string_width(canvas, label)) / 2;
    canvas_draw_icon(canvas, x, 47 - icon_get_height(icon), icon);
    canvas_draw_str(canvas, x + icon_get_width(icon) + 2, 47, label);
}

static void desktop_activity_hero(
    Canvas* canvas,
    const Icon* icon,
    const char* title,
    const char* line1,
    const char* line2) {
    canvas_draw_icon(
        canvas, (36 - icon_get_width(icon)) / 2, 38 - icon_get_height(icon) / 2, icon);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 40, 32, title);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 40, 41, line1);
    canvas_draw_str(canvas, 40, 49, line2);
}

static void desktop_activity_draw(Canvas* canvas, void* context) {
    const DesktopActivityModel* m = context;
    const TumoSystemState* s = &m->state;
    static const char* const titles[] = {"Activity", "Idle & sleep", "Recovery"};
    char value[24];
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 21, titles[m->page]);
    canvas_set_font(canvas, FontSecondary);
    snprintf(value, sizeof(value), "%u/3", m->page + 1U);
    canvas_draw_str_aligned(canvas, 126, 21, AlignRight, AlignBottom, value);

    const char* button = NULL;
    if(m->blocked) {
        desktop_activity_hero(
            canvas, &A_Clock_14, "Still busy", "Finish file tasks", "or close the app.");
        button = "OK";
    } else if(m->confirm) {
        desktop_activity_hero(
            canvas,
            &I_Warning_30x23,
            m->page == 0 ? "Disconnect?" : "Restart?",
            m->page == 0 ? "BLE link will close." : "Normal boot",
            "User files kept");
        button = "Confirm";
    } else if(m->page == 0) {
        snprintf(value, sizeof(value), "%u", s->writes);
        desktop_activity_badge(canvas, 21, &I_SDcardMounted_11x8, "Files", value);
        desktop_activity_badge(
            canvas, 64, &I_Bluetooth_Idle_5x8, "Phone", s->transfer ? "Busy" : "Idle");
        snprintf(value, sizeof(value), "%u/%u", s->phone_previews, s->previews);
        desktop_activity_badge(canvas, 107, &I_Rpc_active_7x8, "Screen", value);
        if(!s->phone_allowed)
            button = "Allow preview";
        else if(s->phone_previews)
            button = "Pause preview";
    } else if(m->page == 1) {
        snprintf(value, sizeof(value), "Sleep holds: %u", m->holds);
        desktop_activity_hero(
            canvas,
            &A_Clock_14,
            tumo_system_model_quiet(s) ? "Quiet idle" : "Desktop live",
            value,
            s->foreground ? "App: running" : "App: idle");
    } else {
        snprintf(value, sizeof(value), "Early faults: %u/3", s->boot_crashes);
        desktop_activity_hero(
            canvas,
            s->recovery ? &I_Warning_30x23 : &I_FaceNormal_29x14,
            s->recovery ? "Safe start" : "Normal boot",
            value,
            "User files kept");
        if(s->recovery) button = "Restart";
    }
    canvas_draw_icon(canvas, 4, 56, &I_ButtonLeftSmall_3x5);
    canvas_draw_icon(canvas, 121, 56, &I_ButtonRightSmall_3x5);
    if(button) elements_button_center(canvas, button);
}

static bool desktop_activity_input(InputEvent* event, void* context) {
    DesktopActivityView* activity = context;
    if(event->type != InputTypeShort || event->key == InputKeyBack) return false;
    DesktopActivityAction action = DesktopActivityPrevious;
    bool notify = false;
    with_view_model(
        activity->view,
        DesktopActivityModel * m,
        {
            if(event->key == InputKeyLeft || event->key == InputKeyRight) {
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
        true);
    if(notify) activity->callback(action, activity->context);
    return true;
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
