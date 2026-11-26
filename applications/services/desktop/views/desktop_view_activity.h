#pragma once
#include <gui/view.h>
#include <furi/core/system_policy_i.h>

typedef enum {
    DesktopActivityPrevious,
    DesktopActivityPausePhone,
    DesktopActivityAllowPhone,
    DesktopActivityRestart,
} DesktopActivityAction;

typedef struct DesktopActivityView DesktopActivityView;
DesktopActivityView*
    desktop_activity_alloc(void (*callback)(DesktopActivityAction, void*), void* context);
View* desktop_activity_get_view(DesktopActivityView* activity);
void desktop_activity_reset(DesktopActivityView* activity, bool recovery_page);
void desktop_activity_update(
    DesktopActivityView* activity,
    const TumoSystemState* state,
    uint16_t holds);
void desktop_activity_blocked(DesktopActivityView* activity);
