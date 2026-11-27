#include "desktop_i.h"
#include "desktop_panel.h"
#include "desktop_panel_io_i.h"
#include "scenes/desktop_scene.h"
#include "views/desktop_view_panel.h"
#include <gui/gui_i.h>
#include <furi_hal.h>
#include <core/memmgr.h>
#include <tumoflip_crash_journal/crash_journal.h>

static bool desktop_panel_display_on(Desktop* desktop) {
    FURI_CRITICAL_ENTER();
    const unsigned layer = desktop->notification->display.index;
    const bool awake = layer < LayerMAX && desktop->notification->display.value[layer] != 0;
    FURI_CRITICAL_EXIT();
    return awake;
}

bool desktop_panel_wanted(Desktop* desktop) {
    return desktop->panel_ready && !desktop->panel_safe_boot && !desktop->locked &&
           !desktop->app_running && desktop->panel_settings.mode == DesktopModeDashboard &&
           scene_manager_get_current_scene(desktop->scene_manager) == DesktopSceneMain &&
           desktop_panel_display_on(desktop) &&
           !gui_active_view_port_count(desktop->gui, GuiLayerFullscreen) &&
           !gui_active_view_port_count(desktop->gui, GuiLayerWindow);
}

static void desktop_panel_refresh(Desktop* desktop) {
    DesktopPanelSnapshot snapshot = {0};
    snapshot.heap_kb = memmgr_get_free_heap() / 1024U;
    snapshot.sleep_holds = furi_hal_power_insomnia_level();
    snapshot.auto_lock_s = desktop->settings.auto_lock_delay_ms / 1000U;
    snapshot.bt_on = furi_hal_bt_is_active();
    FURI_CRITICAL_ENTER();
    snapshot.sd_state = desktop->panel_sd_state;
    FURI_CRITICAL_EXIT();
    TumoflipCrashReport report;
    snapshot.fault_valid = tumoflip_crash_journal_read(&report);
    if(snapshot.fault_valid) {
        snapshot.fault_commit = report.commit;
        strlcpy(
            snapshot.fault_name,
            tumoflip_crash_journal_kind_name(report.kind),
            sizeof(snapshot.fault_name));
    }
    /* Do not build a self-redraw loop from the GUI's transient insomnia hold. */
    bool changed;
    {
        FURI_CRITICAL_ENTER();
        DesktopPanelSnapshot before = desktop->panel_model.snapshot;
        before.sleep_holds = snapshot.sleep_holds;
        changed = memcmp(&before, &snapshot, sizeof(snapshot)) != 0;
        desktop->panel_model.snapshot = snapshot;
        FURI_CRITICAL_EXIT();
    }
    if(changed) view_request_update(&desktop->panel_view);
}

static void desktop_panel_bind(Desktop* desktop, bool panel) {
    if(panel == desktop->panel_attached) return;
    View* wallpaper = animation_manager_get_animation_view(desktop->animation_manager);
    view_stack_remove_view(desktop->main_view_stack, panel ? wallpaper : &desktop->panel_view);
    view_stack_add_view(desktop->main_view_stack, panel ? &desktop->panel_view : wallpaper);
    desktop->panel_attached = panel;
}

static void desktop_panel_stop_motion(Desktop* desktop) {
    if(desktop->panel_timer_armed) {
        furi_timer_stop(desktop->panel_motion_timer);
        furi_timer_flush();
        desktop->panel_timer_armed = false;
    }
}

void desktop_panel_pause(Desktop* desktop) {
    FURI_CRITICAL_ENTER();
    desktop->panel_model.active = false;
    desktop->panel_model.offset = 0;
    desktop->panel_model.direction = 0;
    FURI_CRITICAL_EXIT();
    desktop_panel_stop_motion(desktop);
}

void desktop_panel_sync(Desktop* desktop) {
    if(desktop->app_running || !desktop->panel_ready) {
        desktop_panel_pause(desktop);
        return;
    }
    if(desktop->locked) {
        desktop_panel_prepare_lock(desktop);
        return;
    }
    if(desktop->panel_settings.mode != DesktopModeDashboard || desktop->panel_safe_boot) {
        desktop_panel_pause(desktop);
        desktop_panel_bind(desktop, false);
        if(desktop->panel_stalled) {
            animation_manager_load_and_continue_animation(desktop->animation_manager);
            desktop->panel_stalled = false;
        }
        return;
    }
    if(!desktop_panel_wanted(desktop)) {
        desktop_panel_pause(desktop);
        return;
    }
    if(!desktop->panel_stalled) {
        animation_manager_unload_and_stall_animation(desktop->animation_manager);
        desktop->panel_stalled = true;
    }
    desktop_panel_refresh(desktop);
    bool was_active;
    bool moving;
    {
        FURI_CRITICAL_ENTER();
        was_active = desktop->panel_model.active;
        desktop->panel_model.active = true;
        desktop->panel_model.motion = desktop->panel_settings.motion;
        moving = desktop->panel_model.offset != 0;
        FURI_CRITICAL_EXIT();
    }
    desktop_panel_bind(desktop, true);
    if(!moving) desktop_panel_stop_motion(desktop);
    if(!was_active) view_request_update(&desktop->panel_view);
}

void desktop_panel_prepare_lock(Desktop* desktop) {
    desktop_panel_pause(desktop);
    if(desktop->panel_stalled) {
        animation_manager_load_and_continue_animation(desktop->animation_manager);
        desktop->panel_stalled = false;
    }
}

void desktop_panel_after_app(Desktop* desktop) {
    /* The ordinary BeforeApp path or Panel already stalled the wallpaper. */
    desktop->panel_stalled = true;
    desktop->app_running = false;
    desktop_panel_sync(desktop);
}

FavoriteApp* desktop_panel_favorite(Desktop* desktop, FavoriteAppShortcut shortcut) {
    if((unsigned)shortcut >= FavoriteAppNumber) return NULL;
    FavoriteApp* panel =
        desktop->panel_attached ? desktop_panel_held(&desktop->panel_settings, shortcut) : NULL;
    return panel ? panel : &desktop->settings.favorite_apps[shortcut];
}

static void desktop_panel_motion_tick(void* context) {
    Desktop* desktop = context;
    bool changed = false;
    FURI_CRITICAL_ENTER();
    DesktopPanelModel* model = &desktop->panel_model;
    if(model->active && model->direction) {
        const int16_t offset =
            desktop_panel_offset(model->began, furi_get_tick(), model->direction, true);
        changed = offset != model->offset;
        model->offset = offset;
        if(!offset) model->direction = 0;
    }
    FURI_CRITICAL_EXIT();
    /* No allocation, I/O, timer commands or blocking event-queue writes here. */
    if(changed) view_request_update(&desktop->panel_view);
}

void desktop_panel_move(Desktop* desktop, int8_t direction) {
    bool moving;
    FURI_CRITICAL_ENTER();
    DesktopPanelModel* model = &desktop->panel_model;
    if(!model->active) {
        FURI_CRITICAL_EXIT();
        return;
    }
    model->page = desktop_panel_page(model->page, direction);
    model->began = furi_get_tick();
    model->direction = model->motion ? direction : 0;
    model->offset = desktop_panel_offset(model->began, model->began, model->direction, true);
    moving = model->offset != 0;
    FURI_CRITICAL_EXIT();
    if(moving) {
        desktop->panel_timer_armed = true;
        furi_timer_start(desktop->panel_motion_timer, furi_ms_to_ticks(40));
    } else {
        desktop_panel_stop_motion(desktop);
    }
    view_request_update(&desktop->panel_view);
}

static bool desktop_panel_input(InputEvent* event, void* context) {
    Desktop* desktop = context;
    if(event->type != InputTypeShort ||
       (event->key != InputKeyLeft && event->key != InputKeyRight))
        return false;
    view_dispatcher_send_custom_event(
        desktop->view_dispatcher,
        event->key == InputKeyLeft ? DesktopPanelEventPrevious : DesktopPanelEventNext);
    return true;
}

void desktop_panel_reload(Desktop* desktop) {
    desktop_panel_defaults(&desktop->panel_settings, &desktop->settings);
    desktop_panel_load(&desktop->panel_settings);
}

void desktop_panel_init(Desktop* desktop) {
    memset(&desktop->panel_model, 0, sizeof(desktop->panel_model));
    memset(&desktop->panel_settings, 0, sizeof(desktop->panel_settings));
    desktop->panel_settings.motion = 1;
    desktop->panel_attached = desktop->panel_stalled = desktop->panel_timer_armed = false;
    desktop->panel_ready = false;
    desktop->panel_sd_state = 0;
    desktop->panel_safe_boot = !furi_hal_gpio_read(&gpio_button_back);
    view_init(&desktop->panel_view);
    desktop->panel_view.model_type = ViewModelTypeLockFree;
    desktop->panel_view.model = &desktop->panel_model;
    view_set_context(&desktop->panel_view, desktop);
    view_set_draw_callback(&desktop->panel_view, desktop_panel_draw);
    view_set_input_callback(&desktop->panel_view, desktop_panel_input);
    desktop->panel_motion_timer =
        furi_timer_alloc(desktop_panel_motion_tick, FuriTimerTypePeriodic, desktop);
    static const uint32_t capability = 2;
    furi_record_create(RECORD_DESKTOP_PANEL, (void*)&capability);
}
