#include "sweep_view.h"

#include <furi.h>
#include <gui/canvas.h>
#include <stdio.h>

struct SweepView {
    View* view;
    SweepViewCallback callback;
    void* callback_context;
};

typedef struct {
    char preset[20];
    uint8_t temp_start;
    uint8_t count;
} SweepViewModel;

static void sweep_view_draw_callback(Canvas* canvas, void* _model) {
    SweepViewModel* model = _model;
    char buf[48];

    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    canvas_set_font(canvas, FontPrimary);
    snprintf(buf, sizeof(buf), "Sweep: %s", model->preset);
    canvas_draw_str(canvas, 2, 11, buf);
    canvas_draw_line(canvas, 0, 14, 127, 14);

    canvas_set_font(canvas, FontSecondary);
    if(model->count == 0) {
        snprintf(buf, sizeof(buf), "Start temp: < %u >  (Up/Down)", model->temp_start);
        canvas_draw_str(canvas, 2, 26, buf);
        canvas_draw_str(canvas, 2, 38, "Set the remote to this config");
        canvas_draw_str(canvas, 2, 49, "at min temp. Press its TEMP -");
        canvas_draw_str(canvas, 2, 60, "once, then TEMP + each step.");
    } else {
        snprintf(buf, sizeof(buf), "Start temp: %u", model->temp_start);
        canvas_draw_str(canvas, 2, 26, buf);
        snprintf(
            buf,
            sizeof(buf),
            "Captured: %u - %u  (%u)",
            model->temp_start,
            model->temp_start + model->count - 1,
            model->count);
        canvas_draw_str(canvas, 2, 38, buf);
        canvas_draw_str(canvas, 2, 50, "Press TEMP + for next step");
        canvas_draw_str(canvas, 2, 62, "OK: done    <: undo last");
    }
}

static bool sweep_view_input_callback(InputEvent* event, void* context) {
    SweepView* sweep_view = context;
    if(event->type != InputTypeShort) return false;

    bool consumed = false;
    SweepViewEvent view_event = SweepViewEventDone;

    switch(event->key) {
    case InputKeyOk:
        view_event = SweepViewEventDone;
        consumed = true;
        break;
    case InputKeyLeft:
        view_event = SweepViewEventUndo;
        consumed = true;
        break;
    case InputKeyUp:
        view_event = SweepViewEventUp;
        consumed = true;
        break;
    case InputKeyDown:
        view_event = SweepViewEventDown;
        consumed = true;
        break;
    default:
        break;
    }

    if(consumed && sweep_view->callback) {
        sweep_view->callback(sweep_view->callback_context, view_event);
    }
    return consumed;
}

SweepView* sweep_view_alloc(void) {
    SweepView* sweep_view = malloc(sizeof(SweepView));
    sweep_view->callback = NULL;
    sweep_view->callback_context = NULL;
    sweep_view->view = view_alloc();
    view_set_context(sweep_view->view, sweep_view);
    view_allocate_model(sweep_view->view, ViewModelTypeLocking, sizeof(SweepViewModel));
    view_set_draw_callback(sweep_view->view, sweep_view_draw_callback);
    view_set_input_callback(sweep_view->view, sweep_view_input_callback);
    return sweep_view;
}

void sweep_view_free(SweepView* sweep_view) {
    view_free(sweep_view->view);
    free(sweep_view);
}

View* sweep_view_get_view(SweepView* sweep_view) {
    return sweep_view->view;
}

void sweep_view_set_callback(SweepView* sweep_view, SweepViewCallback callback, void* context) {
    sweep_view->callback = callback;
    sweep_view->callback_context = context;
}

void sweep_view_update(
    SweepView* sweep_view,
    const char* preset,
    uint8_t temp_start,
    uint8_t count) {
    with_view_model(
        sweep_view->view,
        SweepViewModel * model,
        {
            snprintf(model->preset, sizeof(model->preset), "%s", preset);
            model->temp_start = temp_start;
            model->count = count;
        },
        true);
}
