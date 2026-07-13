#include <furi.h>
#include <gui/elements.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <tumoflip_runtime/tumoflip_runtime.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    TumoFabricNodeEventRefresh = 1,
} TumoFabricNodeEvent;

typedef struct {
    bool runtime_ready;
    bool action_ok;
    TumoflipRuntimeFabricSnapshot snapshot;
} TumoFabricNodeModel;

typedef struct {
    Gui* gui;
    TumoflipRuntimeApi* runtime;
    ViewDispatcher* view_dispatcher;
    View* view;
    FuriTimer* refresh_timer;
} TumoFabricNodeApp;

static bool tumofabric_node_is_local(const TumoflipRuntimeFabricSnapshot* snapshot) {
    return snapshot->active && strcmp(snapshot->owner, "flipper") == 0;
}

static void tumofabric_node_refresh_model(TumoFabricNodeApp* app) {
    TumoflipRuntimeFabricSnapshot snapshot = {0};
    const bool ready = app->runtime && app->runtime->get_fabric_state &&
                       app->runtime->get_fabric_state(app->runtime, &snapshot);

    with_view_model(
        app->view,
        TumoFabricNodeModel * model,
        {
            model->runtime_ready = ready;
            if(ready) model->snapshot = snapshot;
        },
        true);
}

static void tumofabric_node_draw_status(Canvas* canvas, const TumoFabricNodeModel* model) {
    const bool active = model->runtime_ready && model->snapshot.active;
    const char* status = !model->runtime_ready ? "ERROR" : (active ? "ACTIVE" : "IDLE");

    canvas_draw_rframe(canvas, 91, 1, 36, 11, 2);
    if(active) {
        canvas_draw_rbox(canvas, 91, 1, 36, 11, 2);
        canvas_set_color(canvas, ColorWhite);
    }
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 109, 10, AlignCenter, AlignBottom, status);
    canvas_set_color(canvas, ColorBlack);
}

static void
    tumofabric_node_draw_row(Canvas* canvas, int32_t y, const char* label, const char* value) {
    canvas_draw_str(canvas, 2, y, label);
    canvas_draw_str_aligned(canvas, 126, y, AlignRight, AlignBottom, value);
}

static void tumofabric_node_draw(Canvas* canvas, void* context) {
    const TumoFabricNodeModel* model = context;
    canvas_clear(canvas);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "TumoFabric");
    tumofabric_node_draw_status(canvas, model);
    canvas_draw_line(canvas, 0, 14, 127, 14);

    if(!model->runtime_ready) {
        canvas_set_font(canvas, FontSecondary);
        tumofabric_node_draw_row(canvas, 28, "Runtime", "Unavailable");
        tumofabric_node_draw_row(canvas, 41, "Action", "Retry");
        elements_button_left(canvas, "Back");
        elements_button_right(canvas, "Retry");
        return;
    }

    const TumoflipRuntimeFabricSnapshot* snapshot = &model->snapshot;
    canvas_set_font(canvas, FontSecondary);
    if(snapshot->active) {
        char sequence[20];
        snprintf(sequence, sizeof(sequence), "Seq %lu", (unsigned long)snapshot->last_sequence);
        tumofabric_node_draw_row(
            canvas, 25, tumofabric_node_is_local(snapshot) ? "Flipper" : snapshot->owner, sequence);

        char value[8];
        snprintf(value, sizeof(value), "%d", snapshot->counter);
        canvas_set_font(canvas, FontBigNumbers);
        canvas_draw_str_aligned(canvas, 64, 47, AlignCenter, AlignBottom, value);
        canvas_set_font(canvas, FontSecondary);

        elements_button_left(canvas, "-1");
        elements_button_center(canvas, "Stop");
        elements_button_right(canvas, "+1");
    } else {
        tumofabric_node_draw_row(canvas, 28, "Session", "Stopped");
        tumofabric_node_draw_row(canvas, 41, "State", "RAM only");
        elements_button_left(canvas, "Back");
        elements_button_center(canvas, "Start");
    }
}

static void tumofabric_node_action(TumoFabricNodeApp* app, InputKey key) {
    TumoflipRuntimeFabricSnapshot snapshot = {0};
    const bool ready = app->runtime && app->runtime->get_fabric_state &&
                       app->runtime->get_fabric_state(app->runtime, &snapshot);
    if(!ready) {
        if(key == InputKeyLeft) view_dispatcher_stop(app->view_dispatcher);
        tumofabric_node_refresh_model(app);
        return;
    }

    bool action_ok = true;
    if(key == InputKeyLeft) {
        if(snapshot.active) {
            action_ok = app->runtime->step_local_fabric &&
                        app->runtime->step_local_fabric(app->runtime, -1);
        } else {
            view_dispatcher_stop(app->view_dispatcher);
            return;
        }
    } else if(key == InputKeyRight) {
        if(snapshot.active) {
            action_ok = app->runtime->step_local_fabric &&
                        app->runtime->step_local_fabric(app->runtime, 1);
        }
    } else if(key == InputKeyOk) {
        if(snapshot.active) {
            action_ok = app->runtime->cancel_fabric && app->runtime->cancel_fabric(app->runtime);
        } else {
            action_ok = app->runtime->open_local_fabric &&
                        app->runtime->open_local_fabric(app->runtime);
        }
    }

    with_view_model(
        app->view, TumoFabricNodeModel * model, { model->action_ok = action_ok; }, false);
    tumofabric_node_refresh_model(app);
}

static bool tumofabric_node_input(InputEvent* event, void* context) {
    TumoFabricNodeApp* app = context;
    if(event->type != InputTypeShort) return false;

    if(event->key == InputKeyBack) {
        view_dispatcher_stop(app->view_dispatcher);
        return true;
    }
    if(event->key == InputKeyLeft || event->key == InputKeyRight || event->key == InputKeyOk) {
        tumofabric_node_action(app, event->key);
        return true;
    }
    return false;
}

static bool tumofabric_node_custom_event(void* context, uint32_t event) {
    TumoFabricNodeApp* app = context;
    if(event == TumoFabricNodeEventRefresh) {
        tumofabric_node_refresh_model(app);
        return true;
    }
    return false;
}

static void tumofabric_node_timer(void* context) {
    TumoFabricNodeApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, TumoFabricNodeEventRefresh);
}

static TumoFabricNodeApp* tumofabric_node_alloc(void) {
    TumoFabricNodeApp* app = malloc(sizeof(TumoFabricNodeApp));
    memset(app, 0, sizeof(TumoFabricNodeApp));
    app->gui = furi_record_open(RECORD_GUI);
    app->runtime = furi_record_open(RECORD_TUMOFLIP_RUNTIME);
    app->view_dispatcher = view_dispatcher_alloc();
    app->view = view_alloc();
    app->refresh_timer = furi_timer_alloc(tumofabric_node_timer, FuriTimerTypePeriodic, app);

    view_allocate_model(app->view, ViewModelTypeLocking, sizeof(TumoFabricNodeModel));
    view_set_context(app->view, app);
    view_set_draw_callback(app->view, tumofabric_node_draw);
    view_set_input_callback(app->view, tumofabric_node_input);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, tumofabric_node_custom_event);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_add_view(app->view_dispatcher, 0U, app->view);
    return app;
}

static void tumofabric_node_free(TumoFabricNodeApp* app) {
    furi_timer_stop(app->refresh_timer);
    furi_timer_free(app->refresh_timer);
    view_dispatcher_remove_view(app->view_dispatcher, 0U);
    view_free(app->view);
    view_dispatcher_free(app->view_dispatcher);
    furi_record_close(RECORD_TUMOFLIP_RUNTIME);
    furi_record_close(RECORD_GUI);
    free(app);
}

int32_t tumofabric_node_app(void* context) {
    UNUSED(context);
    TumoFabricNodeApp* app = tumofabric_node_alloc();
    tumofabric_node_refresh_model(app);
    furi_timer_start(app->refresh_timer, furi_ms_to_ticks(500U));
    view_dispatcher_switch_to_view(app->view_dispatcher, 0U);
    view_dispatcher_run(app->view_dispatcher);
    tumofabric_node_free(app);
    return 0;
}
