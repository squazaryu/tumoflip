#include "learn_view.h"

#include <furi.h>
#include <gui/canvas.h>
#include <stdio.h>

struct LearnView {
    View* view;
    LearnViewCallback callback;
    void* callback_context;
};

typedef struct {
    char label[16];
    uint8_t index;
    uint8_t total;
    bool captured;
    char info[48];
} LearnViewModel;

static void learn_view_draw_callback(Canvas* canvas, void* _model) {
    LearnViewModel* model = _model;
    char buf[16];

    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 11, model->label);
    snprintf(buf, sizeof(buf), "%u/%u", model->index, model->total);
    canvas_draw_str_aligned(canvas, 126, 11, AlignRight, AlignBottom, buf);
    canvas_draw_line(canvas, 0, 14, 127, 14);

    canvas_set_font(canvas, FontSecondary);
    if(!model->captured) {
        canvas_draw_str(canvas, 2, 27, "Point the AC's remote at the");
        canvas_draw_str(canvas, 2, 38, "IR port and press the");
        canvas_draw_str(canvas, 2, 49, "matching button.");
        canvas_draw_str(canvas, 2, 62, "Right: skip this button");
    } else {
        canvas_draw_str(canvas, 2, 30, "Captured:");
        canvas_draw_str(canvas, 2, 42, model->info);
        canvas_draw_str(canvas, 2, 62, "OK: save    <: redo    >: skip");
    }
}

static bool learn_view_input_callback(InputEvent* event, void* context) {
    LearnView* learn_view = context;
    if(event->type != InputTypeShort) return false;

    bool captured = false;
    with_view_model(
        learn_view->view, LearnViewModel * model, { captured = model->captured; }, false);

    bool consumed = false;
    LearnViewEvent view_event = LearnViewEventSkip;

    switch(event->key) {
    case InputKeyOk:
        if(captured) {
            view_event = LearnViewEventOk;
            consumed = true;
        }
        break;
    case InputKeyLeft:
        if(captured) {
            view_event = LearnViewEventRetry;
            consumed = true;
        }
        break;
    case InputKeyRight:
        view_event = LearnViewEventSkip;
        consumed = true;
        break;
    default:
        break;
    }

    if(consumed && learn_view->callback) {
        learn_view->callback(learn_view->callback_context, view_event);
    }
    return consumed;
}

LearnView* learn_view_alloc(void) {
    LearnView* learn_view = malloc(sizeof(LearnView));
    learn_view->callback = NULL;
    learn_view->callback_context = NULL;
    learn_view->view = view_alloc();
    view_set_context(learn_view->view, learn_view);
    view_allocate_model(learn_view->view, ViewModelTypeLocking, sizeof(LearnViewModel));
    view_set_draw_callback(learn_view->view, learn_view_draw_callback);
    view_set_input_callback(learn_view->view, learn_view_input_callback);
    return learn_view;
}

void learn_view_free(LearnView* learn_view) {
    view_free(learn_view->view);
    free(learn_view);
}

View* learn_view_get_view(LearnView* learn_view) {
    return learn_view->view;
}

void learn_view_set_callback(LearnView* learn_view, LearnViewCallback callback, void* context) {
    learn_view->callback = callback;
    learn_view->callback_context = context;
}

void learn_view_set_button(LearnView* learn_view, const char* label, uint8_t index, uint8_t total) {
    with_view_model(
        learn_view->view,
        LearnViewModel * model,
        {
            snprintf(model->label, sizeof(model->label), "%s", label);
            model->index = index;
            model->total = total;
        },
        true);
}

void learn_view_set_captured(LearnView* learn_view, bool captured, const char* info) {
    with_view_model(
        learn_view->view,
        LearnViewModel * model,
        {
            model->captured = captured;
            snprintf(model->info, sizeof(model->info), "%s", info ? info : "");
        },
        true);
}
