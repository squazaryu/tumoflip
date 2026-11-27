/**
 * @file view_i.h
 * GUI: internal View API
 */

#pragma once

#include "view.h"
#include <furi.h>

typedef struct {
    FuriMutex* mutex;
    uint8_t data[];
} ViewModelLocking;

struct View {
    ViewDrawCallback draw_callback;
    ViewInputCallback input_callback;
    ViewCustomCallback custom_callback;

    ViewModelType model_type;
    ViewNavigationCallback previous_callback;
    ViewCallback enter_callback;
    ViewCallback exit_callback;
    ViewOrientation orientation;

    ViewUpdateCallback update_callback;
    void* update_callback_context;

    void* model;
    void* context;
};

/** Initialize View (for internal use) */
void view_init(View* view);

/** Publish the redraw callback and context as a single binding. */
void view_set_update_handler(View* view, ViewUpdateCallback callback, void* context);

/** Take a consistent binding and invoke it outside the critical section.
 *  Stop update producers before freeing their view or callback context.
 */
void view_request_update(View* view);

/** IconAnimation tie callback */
void view_icon_animation_callback(IconAnimation* instance, void* context);

/** Unlock model */
void view_unlock_model(View* view);

/** Draw Callback for View dispatcher */
void view_draw(View* view, Canvas* canvas);

/** Input Callback for View dispatcher */
bool view_input(View* view, InputEvent* event);

/** Custom Callback for View dispatcher */
bool view_custom(View* view, uint32_t event);

/** Previous Callback for View dispatcher */
uint32_t view_previous(View* view);

/** Enter Callback for View dispatcher */
void view_enter(View* view);

/** Exit Callback for View dispatcher */
void view_exit(View* view);
