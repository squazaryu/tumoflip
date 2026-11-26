#include "desktop_i.h"
#include "desktop_panel_io_i.h"

#include <cli/cli_vcp.h>

#include <gui/gui_i.h>

#include <locale/locale.h>
#include <storage/storage.h>

#include <assets_icons.h>

#include "scenes/desktop_scene.h"
#include "scenes/desktop_scene_locked.h"

#include "furi_hal_power.h"

#define TAG "Desktop"

static void desktop_auto_lock_arm(Desktop*);
static void desktop_auto_lock_inhibit(Desktop*);
static void desktop_start_auto_lock_timer(Desktop*);
static void desktop_apply_settings(Desktop*);
static void desktop_tick_event_callback(void* context);

FavoriteApp* desktop_favorite(Desktop* desktop, FavoriteAppShortcut shortcut) {
    if((unsigned)shortcut >= FavoriteAppNumber) return NULL;
    return desktop->panel.mode == DesktopModeDashboard ?
               desktop_panel_held(&desktop->panel, shortcut) :
               &desktop->settings.favorite_apps[shortcut];
}

static void desktop_dashboard_mount(Desktop* desktop) {
    bool dashboard = desktop->panel.mode == DesktopModeDashboard;
    if(dashboard != desktop->dashboard_mounted) {
        View* wallpaper = animation_manager_get_animation_view(desktop->animation_manager);
        View* panel = desktop_activity_get_view(desktop->dashboard);
        animation_manager_set_quiet(desktop->animation_manager, true);
        view_stack_remove_view(desktop->main_view_stack, dashboard ? wallpaper : panel);
        view_stack_add_view(desktop->main_view_stack, dashboard ? panel : wallpaper);
        desktop->dashboard_mounted = dashboard;
    }
}

static void desktop_reload_panel(Desktop* desktop) {
    desktop_panel_load(&desktop->panel);
    desktop_policy_changed(desktop);
}

static void desktop_dashboard_callback(DesktopActivityAction action, void* context) {
    if(action == DesktopActivityHomeChanged) desktop_policy_changed(context);
}

static void
    desktop_dashboard_refresh(Desktop* desktop, const TumoSystemState* state, uint32_t scene) {
    const bool main = scene == DesktopSceneMain && !desktop->locked && !desktop->app_running;
    /* Only swap the shared wallpaper view when Main actually owns its callbacks. */
    if(main) desktop_dashboard_mount(desktop);
    bool alive = main && desktop->panel.mode == DesktopModeDashboard;
    if(alive) desktop_activity_update(desktop->dashboard, state, furi_hal_power_insomnia_level());
    uint32_t period = desktop_activity_home_tick(
        desktop->dashboard,
        furi_get_tick(),
        alive,
        desktop->panel.motion && state->display_on && !state->recovery);
    if(period != desktop->dashboard_period) {
        if(period)
            furi_timer_start(desktop->dashboard_timer, furi_ms_to_ticks(period));
        else
            furi_timer_stop(desktop->dashboard_timer);
        desktop->dashboard_period = period;
    }
}

static void desktop_loader_callback(const void* message, void* context) {
    furi_assert(context);
    Desktop* desktop = context;
    const LoaderEvent* event = message;

    if(event->type == LoaderEventTypeApplicationBeforeLoad) {
        furi_system_policy_application(true);
        view_dispatcher_send_custom_event(desktop->view_dispatcher, DesktopGlobalBeforeAppStarted);
        furi_check(furi_semaphore_acquire(desktop->animation_semaphore, 3000) == FuriStatusOk);
    } else if(event->type == LoaderEventTypeNoMoreAppsInQueue) {
        furi_system_policy_application(false);
        view_dispatcher_send_custom_event(desktop->view_dispatcher, DesktopGlobalAfterAppFinished);
    }
}

static void desktop_storage_callback(const void* message, void* context) {
    furi_assert(context);
    Desktop* desktop = context;
    const StorageEvent* event = message;

    if(event->type == StorageEventTypeCardMount) {
        view_dispatcher_send_custom_event(desktop->view_dispatcher, DesktopGlobalReloadSettings);
    }
}

static void desktop_lock_icon_draw_callback(Canvas* canvas, void* context) {
    UNUSED(context);
    furi_assert(canvas);
    canvas_draw_icon(canvas, 0, 0, &I_Lock_7x8);
}

static void desktop_clock_update(Desktop* desktop) {
    furi_assert(desktop);

    DateTime curr_dt;
    furi_hal_rtc_get_datetime(&curr_dt);
    bool time_format_12 = locale_get_time_format() == LocaleTimeFormat12h;

    if(desktop->clock.hour != curr_dt.hour || desktop->clock.minute != curr_dt.minute ||
       desktop->clock.format_12 != time_format_12) {
        desktop->clock.format_12 = time_format_12;
        desktop->clock.hour = curr_dt.hour;
        desktop->clock.minute = curr_dt.minute;
        view_port_update(desktop->clock_viewport);
    }
}

static void desktop_clock_reconfigure(Desktop* desktop) {
    furi_assert(desktop);

    desktop_clock_update(desktop);

    if(desktop->settings.display_clock && !desktop->quiet) {
        furi_timer_start(desktop->update_clock_timer, furi_ms_to_ticks(1000));
    } else {
        furi_timer_stop(desktop->update_clock_timer);
    }

    view_port_enabled_set(desktop->clock_viewport, desktop->settings.display_clock);
}

static void desktop_update_policy(Desktop* desktop) {
    TumoSystemState state = furi_system_policy_snapshot();
    const bool quiet = tumo_system_model_quiet(&state);
    if(desktop->quiet != quiet) {
        desktop->quiet = quiet;
        desktop_clock_reconfigure(desktop);
    }
    animation_manager_set_quiet(
        desktop->animation_manager,
        quiet || desktop->panel.mode == DesktopModeDashboard || desktop->app_running);
    const uint32_t scene = scene_manager_get_current_scene(desktop->scene_manager);
    desktop_dashboard_refresh(desktop, &state, scene);
    const bool slow = quiet && (scene == DesktopSceneMain || scene == DesktopSceneLocked);
    /* This is Desktop's owned loop; changing the dispatcher default alone would
     * not reconfigure an already-running tick source. */
    furi_event_loop_tick_set(
        view_dispatcher_get_event_loop(desktop->view_dispatcher),
        slow ? 5000 : 500,
        desktop_tick_event_callback,
        desktop);
    if(scene == DesktopSceneActivity)
        desktop_activity_update(desktop->activity, &state, furi_hal_power_insomnia_level());
    if(desktop->recovery_notice_pending && !desktop->locked && !desktop->app_running &&
       scene == DesktopSceneMain)
        scene_manager_next_scene(desktop->scene_manager, DesktopSceneActivity);
}

void desktop_policy_changed(void* context) {
    Desktop* desktop = context;
    const uint8_t changed = 1;
    /* Producers may be Storage/Notification. Never block them on Desktop. */
    furi_message_queue_put(desktop->policy_events, &changed, 0);
}

static void desktop_policy_event(FuriEventLoopObject* object, void* context) {
    uint8_t changed;
    if(furi_message_queue_get(object, &changed, 0) == FuriStatusOk) desktop_update_policy(context);
}

static void desktop_clock_draw_callback(Canvas* canvas, void* context) {
    furi_assert(context);
    furi_assert(canvas);

    Desktop* desktop = context;

    canvas_set_font(canvas, FontPrimary);

    uint8_t hour = desktop->clock.hour;
    if(desktop->clock.format_12) {
        if(hour > 12) {
            hour -= 12;
        }
        if(hour == 0) {
            hour = 12;
        }
    }

    char buffer[20];
    if(furi_hal_rtc_is_flag_set(FuriHalRtcFlagDebug)) {
        snprintf(buffer, sizeof(buffer), "D %02u:%02u", hour, desktop->clock.minute);
    } else {
        snprintf(buffer, sizeof(buffer), "%02u:%02u", hour, desktop->clock.minute);
    }

    view_port_set_width(
        desktop->clock_viewport,
        canvas_string_width(canvas, buffer) - 1 + (desktop->clock.minute % 10 == 1));

    canvas_draw_str_aligned(canvas, 0, 8, AlignLeft, AlignBottom, buffer);
}

static void desktop_stealth_mode_icon_draw_callback(Canvas* canvas, void* context) {
    UNUSED(context);
    furi_assert(canvas);
    canvas_draw_icon(canvas, 0, 0, &I_Muted_8x8);
}

static bool desktop_custom_event_callback(void* context, uint32_t event) {
    furi_assert(context);
    Desktop* desktop = (Desktop*)context;

    if(event == DesktopGlobalBeforeAppStarted) {
        if(animation_manager_is_animation_loaded(desktop->animation_manager)) {
            if(desktop->shortcut_animation_retain_pending) {
                animation_manager_retain_and_stall_animation(desktop->animation_manager);
            } else {
                animation_manager_unload_and_stall_animation(desktop->animation_manager);
            }
        } else if(animation_manager_has_retained_animation(desktop->animation_manager)) {
            /* A queued second app can be much heavier than the preflight-approved shortcut FAP. */
            animation_manager_release_retained_animation(desktop->animation_manager);
        }
        desktop->shortcut_animation_retain_pending = false;

        desktop_auto_lock_inhibit(desktop);

        desktop->app_running = true;

        furi_semaphore_release(desktop->animation_semaphore);

    } else if(event == DesktopGlobalAfterAppFinished) {
        animation_manager_load_and_continue_animation(desktop->animation_manager);
        desktop_auto_lock_arm(desktop);
        desktop->app_running = false;
        desktop_update_policy(desktop);

    } else if(event == DesktopGlobalAutoLock) {
        if(!desktop->app_running && !desktop->locked) {
            // Disable AutoLock if usb_inhibit_autolock option enabled and device have active USB session.
            if((desktop->settings.usb_inhibit_auto_lock) && (furi_hal_usb_is_locked())) {
                return (0);
            }

            desktop_lock(desktop);
        }
    } else if(event == DesktopGlobalSaveSettings) {
        desktop_settings_save(&desktop->settings);
        desktop_apply_settings(desktop);
        desktop_reload_panel(desktop);

    } else if(event == DesktopGlobalReloadSettings) {
        desktop_settings_load(&desktop->settings);
        desktop_apply_settings(desktop);
        desktop_reload_panel(desktop);

    } else {
        return scene_manager_handle_custom_event(desktop->scene_manager, event);
    }

    return true;
}

static bool desktop_back_event_callback(void* context) {
    furi_assert(context);
    Desktop* desktop = (Desktop*)context;
    return scene_manager_handle_back_event(desktop->scene_manager);
}

static void desktop_tick_event_callback(void* context) {
    furi_assert(context);
    Desktop* app = context;
    desktop_update_policy(app);
    scene_manager_handle_tick_event(app->scene_manager);
}

static void desktop_input_event_callback(const void* value, void* context) {
    furi_assert(value);
    furi_assert(context);
    const InputEvent* event = value;
    Desktop* desktop = context;
    if(event->type == InputTypePress) {
        desktop_start_auto_lock_timer(desktop);
    }
}

static void desktop_auto_lock_timer_callback(void* context) {
    furi_assert(context);
    Desktop* desktop = context;
    view_dispatcher_send_custom_event(desktop->view_dispatcher, DesktopGlobalAutoLock);
}

static void desktop_start_auto_lock_timer(Desktop* desktop) {
    furi_timer_start(
        desktop->auto_lock_timer, furi_ms_to_ticks(desktop->settings.auto_lock_delay_ms));
}

static void desktop_stop_auto_lock_timer(Desktop* desktop) {
    furi_timer_stop(desktop->auto_lock_timer);
}

static void desktop_auto_lock_arm(Desktop* desktop) {
    if(desktop->settings.auto_lock_delay_ms) {
        if(!desktop->input_events_subscription) {
            desktop->input_events_subscription = furi_pubsub_subscribe(
                desktop->input_events_pubsub, desktop_input_event_callback, desktop);
        }
        desktop_start_auto_lock_timer(desktop);
    }
}

static void desktop_auto_lock_inhibit(Desktop* desktop) {
    desktop_stop_auto_lock_timer(desktop);
    if(desktop->input_events_subscription) {
        furi_pubsub_unsubscribe(desktop->input_events_pubsub, desktop->input_events_subscription);
        desktop->input_events_subscription = NULL;
    }
}

static void desktop_clock_timer_callback(void* context) {
    furi_assert(context);
    Desktop* desktop = context;

    const bool clock_enabled = gui_active_view_port_count(desktop->gui, GuiLayerStatusBarLeft) < 6;

    if(clock_enabled) {
        desktop_clock_update(desktop);
    }

    view_port_enabled_set(desktop->clock_viewport, clock_enabled);
}

static void desktop_apply_settings(Desktop* desktop) {
    desktop->in_transition = true;

    desktop_view_locked_set_skip_animation(
        desktop->locked_view, desktop->settings.lockscreen_skip_animation);
    desktop_clock_reconfigure(desktop);

    if(!desktop->app_running && !desktop->locked) {
        desktop_auto_lock_arm(desktop);
    }

    desktop->in_transition = false;
}

static void desktop_init_settings(Desktop* desktop) {
    furi_pubsub_subscribe(storage_get_pubsub(desktop->storage), desktop_storage_callback, desktop);

    if(storage_sd_status(desktop->storage) != FSE_OK) {
        FURI_LOG_D(TAG, "SD Card not ready, skipping settings");
        return;
    }

    desktop_settings_load(&desktop->settings);
    desktop_apply_settings(desktop);
}

static Desktop* desktop_alloc(void) {
    Desktop* desktop = malloc(sizeof(Desktop));
    memset(&desktop->settings, 0, sizeof(desktop->settings));
    memset(&desktop->panel, 0, sizeof(desktop->panel));
    desktop->dashboard_mounted = false;
    desktop->dashboard_period = 0;
    desktop->quiet = false;
    desktop->locked = false;
    desktop->recovery_notice_pending = furi_system_policy_is_recovery();

    desktop->animation_semaphore = furi_semaphore_alloc(1, 0);
    desktop->animation_manager = animation_manager_alloc();
    desktop->gui = furi_record_open(RECORD_GUI);
    desktop->scene_thread = furi_thread_alloc();
    desktop->view_dispatcher = view_dispatcher_alloc();
    desktop->scene_manager = scene_manager_alloc(&desktop_scene_handlers, desktop);

    view_dispatcher_attach_to_gui(
        desktop->view_dispatcher, desktop->gui, ViewDispatcherTypeDesktop);
    view_dispatcher_set_tick_event_callback(
        desktop->view_dispatcher, desktop_tick_event_callback, 500);

    view_dispatcher_set_event_callback_context(desktop->view_dispatcher, desktop);
    view_dispatcher_set_custom_event_callback(
        desktop->view_dispatcher, desktop_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(
        desktop->view_dispatcher, desktop_back_event_callback);

    desktop->lock_menu = desktop_lock_menu_alloc();
    desktop->quick_settings = desktop_quick_settings_alloc();
    desktop->activity = desktop_activity_alloc(desktop_activity_callback, desktop);
    desktop->dashboard = desktop_activity_alloc(desktop_dashboard_callback, desktop);
    desktop_activity_set_home(desktop->dashboard, true);
    desktop->debug_view = desktop_debug_alloc();
    desktop->popup = popup_alloc();
    desktop->locked_view = desktop_view_locked_alloc();
    desktop->pin_input_view = desktop_view_pin_input_alloc();
    desktop->pin_timeout_view = desktop_view_pin_timeout_alloc();
    desktop->slideshow_view = desktop_view_slideshow_alloc();

    desktop->main_view_stack = view_stack_alloc();
    desktop->main_view = desktop_main_alloc();
    View* dolphin_view = animation_manager_get_animation_view(desktop->animation_manager);
    view_stack_add_view(desktop->main_view_stack, desktop_main_get_view(desktop->main_view));
    view_stack_add_view(desktop->main_view_stack, dolphin_view);
    view_stack_add_view(
        desktop->main_view_stack, desktop_view_locked_get_view(desktop->locked_view));

    /* locked view (as animation view) attends in 2 scenes: main & locked,
     * because it has to draw "Unlocked" label on main scene */
    desktop->locked_view_stack = view_stack_alloc();
    view_stack_add_view(desktop->locked_view_stack, dolphin_view);
    view_stack_add_view(
        desktop->locked_view_stack, desktop_view_locked_get_view(desktop->locked_view));

    view_dispatcher_add_view(
        desktop->view_dispatcher,
        DesktopViewIdMain,
        view_stack_get_view(desktop->main_view_stack));
    view_dispatcher_add_view(
        desktop->view_dispatcher,
        DesktopViewIdLocked,
        view_stack_get_view(desktop->locked_view_stack));
    view_dispatcher_add_view(
        desktop->view_dispatcher,
        DesktopViewIdLockMenu,
        desktop_lock_menu_get_view(desktop->lock_menu));
    view_dispatcher_add_view(
        desktop->view_dispatcher,
        DesktopViewIdQuickSettings,
        desktop_quick_settings_get_view(desktop->quick_settings));
    view_dispatcher_add_view(
        desktop->view_dispatcher,
        DesktopViewIdActivity,
        desktop_activity_get_view(desktop->activity));
    view_dispatcher_add_view(
        desktop->view_dispatcher, DesktopViewIdDebug, desktop_debug_get_view(desktop->debug_view));
    view_dispatcher_add_view(
        desktop->view_dispatcher, DesktopViewIdPopup, popup_get_view(desktop->popup));
    view_dispatcher_add_view(
        desktop->view_dispatcher,
        DesktopViewIdPinTimeout,
        desktop_view_pin_timeout_get_view(desktop->pin_timeout_view));
    view_dispatcher_add_view(
        desktop->view_dispatcher,
        DesktopViewIdPinInput,
        desktop_view_pin_input_get_view(desktop->pin_input_view));
    view_dispatcher_add_view(
        desktop->view_dispatcher,
        DesktopViewIdSlideshow,
        desktop_view_slideshow_get_view(desktop->slideshow_view));

    // Lock icon
    desktop->lock_icon_viewport = view_port_alloc();
    view_port_set_width(desktop->lock_icon_viewport, icon_get_width(&I_Lock_7x8));
    view_port_draw_callback_set(
        desktop->lock_icon_viewport, desktop_lock_icon_draw_callback, desktop);
    view_port_enabled_set(desktop->lock_icon_viewport, false);
    gui_add_view_port(desktop->gui, desktop->lock_icon_viewport, GuiLayerStatusBarLeft);

    // Clock
    desktop->clock_viewport = view_port_alloc();
    view_port_set_width(desktop->clock_viewport, 25);
    view_port_draw_callback_set(desktop->clock_viewport, desktop_clock_draw_callback, desktop);
    view_port_enabled_set(desktop->clock_viewport, false);
    gui_add_view_port(desktop->gui, desktop->clock_viewport, GuiLayerStatusBarRight);

    // Stealth mode icon
    desktop->stealth_mode_icon_viewport = view_port_alloc();
    view_port_set_width(desktop->stealth_mode_icon_viewport, icon_get_width(&I_Muted_8x8));
    view_port_draw_callback_set(
        desktop->stealth_mode_icon_viewport, desktop_stealth_mode_icon_draw_callback, desktop);
    if(furi_hal_rtc_is_flag_set(FuriHalRtcFlagStealthMode)) {
        view_port_enabled_set(desktop->stealth_mode_icon_viewport, true);
    } else {
        view_port_enabled_set(desktop->stealth_mode_icon_viewport, false);
    }
    gui_add_view_port(desktop->gui, desktop->stealth_mode_icon_viewport, GuiLayerStatusBarLeft);

    // Unload animations before starting an application
    desktop->loader = furi_record_open(RECORD_LOADER);
    furi_pubsub_subscribe(loader_get_pubsub(desktop->loader), desktop_loader_callback, desktop);

    desktop->storage = furi_record_open(RECORD_STORAGE);
    desktop->notification = furi_record_open(RECORD_NOTIFICATION);
    desktop->input_events_pubsub = furi_record_open(RECORD_INPUT_EVENTS);

    desktop->auto_lock_timer =
        furi_timer_alloc(desktop_auto_lock_timer_callback, FuriTimerTypeOnce, desktop);

    desktop->status_pubsub = furi_pubsub_alloc();

    desktop->update_clock_timer =
        furi_timer_alloc(desktop_clock_timer_callback, FuriTimerTypePeriodic, desktop);

    desktop->app_running = loader_is_locked(desktop->loader);
    desktop->shortcut_animation_retain_pending = false;

    desktop->policy_events = furi_message_queue_alloc(1, sizeof(uint8_t));
    desktop->dashboard_timer =
        furi_timer_alloc(desktop_policy_changed, FuriTimerTypePeriodic, desktop);
    furi_event_loop_subscribe_message_queue(
        view_dispatcher_get_event_loop(desktop->view_dispatcher),
        desktop->policy_events,
        FuriEventLoopEventIn,
        desktop_policy_event,
        desktop);
    furi_system_policy_listen(desktop_policy_changed, desktop);
    furi_system_policy_application(desktop->app_running);

    furi_record_create(RECORD_DESKTOP, desktop);

    return desktop;
}

/*
 * Private API
 */

void desktop_lock(Desktop* desktop) {
    furi_assert(!desktop->locked);

    furi_hal_rtc_set_flag(FuriHalRtcFlagLock);

    if(desktop_pin_code_is_set()) {
        CliVcp* cli_vcp = furi_record_open(RECORD_CLI_VCP);
        cli_vcp_disable(cli_vcp);
        furi_record_close(RECORD_CLI_VCP);
    }

    desktop_auto_lock_inhibit(desktop);
    scene_manager_set_scene_state(
        desktop->scene_manager, DesktopSceneLocked, DesktopSceneLockedStateFirstEnter);
    scene_manager_next_scene(desktop->scene_manager, DesktopSceneLocked);

    DesktopStatus status = {.locked = true};
    furi_pubsub_publish(desktop->status_pubsub, &status);

    desktop->locked = true;
}

void desktop_unlock(Desktop* desktop) {
    furi_assert(desktop->locked);

    view_port_enabled_set(desktop->lock_icon_viewport, false);
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_set_lockdown(gui, false);
    furi_record_close(RECORD_GUI);
    desktop_view_locked_unlock(desktop->locked_view);
    scene_manager_search_and_switch_to_previous_scene(desktop->scene_manager, DesktopSceneMain);
    desktop_auto_lock_arm(desktop);
    furi_hal_rtc_reset_flag(FuriHalRtcFlagLock);
    furi_hal_rtc_set_pin_fails(0);

    if(desktop_pin_code_is_set()) {
        CliVcp* cli_vcp = furi_record_open(RECORD_CLI_VCP);
        cli_vcp_enable(cli_vcp);
        furi_record_close(RECORD_CLI_VCP);
    }

    DesktopStatus status = {.locked = false};
    furi_pubsub_publish(desktop->status_pubsub, &status);

    desktop->locked = false;
}

void desktop_set_stealth_mode_state(Desktop* desktop, bool enabled) {
    desktop->in_transition = true;

    if(enabled) {
        furi_hal_rtc_set_flag(FuriHalRtcFlagStealthMode);
    } else {
        furi_hal_rtc_reset_flag(FuriHalRtcFlagStealthMode);
    }

    view_port_enabled_set(desktop->stealth_mode_icon_viewport, enabled);

    desktop->in_transition = false;
}

/*
 *  Public API
 */

bool desktop_api_is_locked(Desktop* instance) {
    furi_assert(instance);
    return furi_hal_rtc_is_flag_set(FuriHalRtcFlagLock);
}

void desktop_api_unlock(Desktop* instance) {
    furi_assert(instance);
    view_dispatcher_send_custom_event(instance->view_dispatcher, DesktopGlobalApiUnlock);
}

FuriPubSub* desktop_api_get_status_pubsub(Desktop* instance) {
    furi_assert(instance);
    return instance->status_pubsub;
}

void desktop_api_reload_settings(Desktop* instance) {
    furi_assert(instance);
    view_dispatcher_send_custom_event(instance->view_dispatcher, DesktopGlobalReloadSettings);
}

void desktop_api_get_settings(Desktop* instance, DesktopSettings* settings) {
    furi_assert(instance);
    furi_assert(settings);

    *settings = instance->settings;
}

void desktop_api_set_settings(Desktop* instance, const DesktopSettings* settings) {
    furi_assert(instance);
    furi_assert(settings);

    instance->settings = *settings;
    view_dispatcher_send_custom_event(instance->view_dispatcher, DesktopGlobalSaveSettings);
}

/*
 * Application thread
 */

int32_t desktop_srv(void* p) {
    UNUSED(p);

    if(furi_hal_rtc_get_boot_mode() != FuriHalRtcBootModeNormal) {
        FURI_LOG_W(TAG, "Skipping start in special boot mode");

        furi_thread_suspend(furi_thread_get_current_id());
        return 0;
    }

    Desktop* desktop = desktop_alloc();

    desktop_init_settings(desktop);
    desktop_panel_defaults(&desktop->panel, &desktop->settings);
    desktop_reload_panel(desktop);

    scene_manager_next_scene(desktop->scene_manager, DesktopSceneMain);

    if(desktop_pin_code_is_set()) {
        desktop_lock(desktop);
    } else {
        CliVcp* cli_vcp = furi_record_open(RECORD_CLI_VCP);
        cli_vcp_enable(cli_vcp);
        furi_record_close(RECORD_CLI_VCP);
    }

    if(storage_file_exists(desktop->storage, SLIDESHOW_FS_PATH)) {
        scene_manager_next_scene(desktop->scene_manager, DesktopSceneSlideshow);
    }

    if(!furi_hal_version_do_i_belong_here()) {
        scene_manager_next_scene(desktop->scene_manager, DesktopSceneHwMismatch);
    }

    if(furi_hal_rtc_get_fault_data() && !furi_system_policy_is_recovery()) {
        scene_manager_next_scene(desktop->scene_manager, DesktopSceneFault);
    }

    uint8_t keys_total, keys_valid;
    if(!furi_hal_crypto_enclave_verify(&keys_total, &keys_valid)) {
        FURI_LOG_E(
            TAG,
            "Secure Enclave verification failed: total %hhu, valid %hhu",
            keys_total,
            keys_valid);

        scene_manager_next_scene(desktop->scene_manager, DesktopSceneSecureEnclave);
    }

    // Special case: autostart application is already running
    if(desktop->app_running && animation_manager_is_animation_loaded(desktop->animation_manager)) {
        animation_manager_unload_and_stall_animation(desktop->animation_manager);
    }

    view_dispatcher_run(desktop->view_dispatcher);

    // Should never get here (a service thread will crash automatically if it returns)
    return 0;
}
