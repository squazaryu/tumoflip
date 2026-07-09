#pragma once

#include <gui/view.h>

typedef struct LearnView LearnView;

typedef enum {
    LearnViewEventOk,
    LearnViewEventRetry,
    LearnViewEventSkip,
} LearnViewEvent;

typedef void (*LearnViewCallback)(void* context, LearnViewEvent event);

LearnView* learn_view_alloc(void);
void learn_view_free(LearnView* learn_view);
View* learn_view_get_view(LearnView* learn_view);
void learn_view_set_callback(LearnView* learn_view, LearnViewCallback callback, void* context);
void learn_view_set_button(LearnView* learn_view, const char* label, uint8_t index, uint8_t total);
void learn_view_set_captured(LearnView* learn_view, bool captured, const char* info);
