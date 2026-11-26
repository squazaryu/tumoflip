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

static void desktop_activity_row(Canvas* canvas, uint8_t y, const char* label, const char* value) {
    canvas_draw_str(canvas, 2, y, label);
    canvas_draw_str_aligned(canvas, 126, y, AlignRight, AlignBottom, value);
}

static void desktop_activity_draw(Canvas* canvas, void* context) {
    const DesktopActivityModel* m = context;
    const TumoSystemState* s = &m->state;
    static const char* const titles[] = {"System activity", "Idle & sleep", "Recovery"};
    char value[16];
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 21, titles[m->page]);
    canvas_set_font(canvas, FontSecondary);
    snprintf(value, sizeof(value), "%u/3", m->page + 1U);
    canvas_draw_str_aligned(canvas, 126, 21, AlignRight, AlignBottom, value);
    canvas_draw_line(canvas, 2, 24, 125, 24);

    const char* button = NULL;
    if(m->blocked) {
        canvas_draw_str(canvas, 2, 36, "Busy. Finish file tasks");
        canvas_draw_str(canvas, 2, 47, "or close the running app.");
    } else if(m->confirm) {
        canvas_draw_str(
            canvas, 2, 35, m->page == 0 ? "Bluetooth will disconnect." : "Restart in normal mode?");
        canvas_draw_str(canvas, 2, 46, "OK confirms; arrows cancel.");
        button = "Confirm";
    } else if(m->page == 0) {
        snprintf(value, sizeof(value), "%u", s->writes);
        desktop_activity_row(canvas, 31, "File writers", value);
        desktop_activity_row(canvas, 40, "Phone transfer", s->transfer ? "Active" : "Idle");
        snprintf(value, sizeof(value), "%u / %u", s->phone_previews, s->previews);
        desktop_activity_row(canvas, 49, "Preview P/H", value);
        if(!s->phone_allowed)
            button = "Allow preview";
        else if(s->phone_previews)
            button = "Pause preview";
    } else if(m->page == 1) {
        desktop_activity_row(
            canvas, 31, "Desktop", tumo_system_model_quiet(s) ? "Paused" : "Live");
        snprintf(value, sizeof(value), "%u", m->holds);
        desktop_activity_row(canvas, 40, "Sleep holds", value);
        desktop_activity_row(canvas, 49, "App running", s->foreground ? "Yes" : "No");
    } else {
        desktop_activity_row(canvas, 31, "Boot", s->recovery ? "Recovery" : "Normal");
        snprintf(value, sizeof(value), "%u/3", s->boot_crashes);
        desktop_activity_row(canvas, 40, "Early crashes", value);
        desktop_activity_row(canvas, 49, "User data", "Unchanged");
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
                m->blocked = false;
                if(m->page == 0 && !m->state.phone_allowed) {
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
