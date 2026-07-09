#pragma once

#include <gui/view.h>

typedef struct SweepView SweepView;

typedef enum {
    SweepViewEventDone,
    SweepViewEventUndo,
    SweepViewEventUp,
    SweepViewEventDown,
} SweepViewEvent;

typedef void (*SweepViewCallback)(void* context, SweepViewEvent event);

SweepView* sweep_view_alloc(void);
void sweep_view_free(SweepView* sweep_view);
View* sweep_view_get_view(SweepView* sweep_view);
void sweep_view_set_callback(SweepView* sweep_view, SweepViewCallback callback, void* context);
void sweep_view_update(SweepView* sweep_view, const char* preset, uint8_t temp_start, uint8_t count);
