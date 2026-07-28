#include <furi.h>
#include <furi_hal.h>
#include <applications.h>
#include <assets_icons.h>
#include <core/memmgr.h>
#include <core/memmgr_heap.h>
#include <flipper_application/flipper_application.h>
#include <loader/loader.h>
#include <loader/firmware_api/firmware_api.h>
#include <storage/storage.h>

#include "../desktop_i.h"
#include "../views/desktop_events.h"
#include "../views/desktop_view_main.h"
#include "desktop_scene.h"

#define TAG "DesktopSrv"

#define DESKTOP_SHORTCUT_RETAIN_MAX_FAP_BYTES   (8U * 1024U)
#define DESKTOP_SHORTCUT_RETAIN_MAX_STACK_BYTES (4U * 1024U)
#define DESKTOP_SHORTCUT_RETAIN_MIN_FREE_HEAP   (64U * 1024U)
#define DESKTOP_SHORTCUT_RETAIN_MIN_HEAP_BLOCK  (48U * 1024U)
#define DESKTOP_SHORTCUT_RETAIN_ARF_HUB_PATH    EXT_PATH("apps/ARF Tools/arf_subghz_full.fap")
#define DESKTOP_SHORTCUT_RETAIN_ARF_HUB_NAME    "ARF Sub-GHz Full"

static void desktop_scene_main_new_idle_animation_callback(void* context) {
    furi_assert(context);
    Desktop* desktop = context;
    view_dispatcher_send_custom_event(
        desktop->view_dispatcher, DesktopAnimationEventNewIdleAnimation);
}

static void desktop_scene_main_check_animation_callback(void* context) {
    furi_assert(context);
    Desktop* desktop = context;
    view_dispatcher_send_custom_event(
        desktop->view_dispatcher, DesktopAnimationEventCheckAnimation);
}

static void desktop_scene_main_interact_animation_callback(void* context) {
    furi_assert(context);
    Desktop* desktop = context;
    view_dispatcher_send_custom_event(
        desktop->view_dispatcher, DesktopAnimationEventInteractAnimation);
}

#ifdef APP_ARCHIVE
static void
    desktop_switch_to_app(Desktop* desktop, const FlipperInternalApplication* flipper_app) {
    furi_assert(desktop);
    furi_assert(flipper_app);
    furi_assert(flipper_app->app);
    furi_assert(flipper_app->name);

    if(furi_thread_get_state(desktop->scene_thread) != FuriThreadStateStopped) {
        FURI_LOG_E("Desktop", "Thread is already running");
        return;
    }

    FuriHalRtcHeapTrackMode mode = furi_hal_rtc_get_heap_track_mode();
    if(mode > FuriHalRtcHeapTrackModeNone) {
        furi_thread_enable_heap_trace(desktop->scene_thread);
    } else {
        furi_thread_disable_heap_trace(desktop->scene_thread);
    }

    furi_thread_set_name(desktop->scene_thread, flipper_app->name);
    furi_thread_set_stack_size(desktop->scene_thread, flipper_app->stack_size);
    furi_thread_set_callback(desktop->scene_thread, flipper_app->app);

    furi_thread_start(desktop->scene_thread);
}
#endif

static inline bool desktop_scene_main_check_none(const char* str) {
    return (str[1] == '\0' && str[0] == '?');
}

static bool desktop_scene_main_path_ends_with(const char* path, const char* suffix) {
    const size_t path_len = strlen(path);
    const size_t suffix_len = strlen(suffix);
    return path_len >= suffix_len && !strcmp(path + path_len - suffix_len, suffix);
}

static bool desktop_scene_main_is_apps_folder_target(const char* path) {
    const char apps_path[] = EXT_PATH("apps");
    const size_t apps_path_len = strlen(apps_path);

    if(strcmp(path, apps_path) == 0) return true;
    return strncmp(path, apps_path, apps_path_len) == 0 && path[apps_path_len] == '/' &&
           !desktop_scene_main_path_ends_with(path, ".fap") &&
           !desktop_scene_main_path_ends_with(path, ".js");
}

static bool
    desktop_scene_main_shortcut_can_retain_animation(Desktop* desktop, const char* target) {
    /*
     * FAP size and declared stack do not bound runtime heap use. Keep this
     * optimization restricted to the measured lightweight first-hop hub.
     */
    if(strcmp(target, DESKTOP_SHORTCUT_RETAIN_ARF_HUB_PATH) != 0) return false;
    if(!desktop_scene_main_path_ends_with(target, ".fap")) return false;
    if((memmgr_get_free_heap() < DESKTOP_SHORTCUT_RETAIN_MIN_FREE_HEAP) ||
       (memmgr_heap_get_max_free_block() < DESKTOP_SHORTCUT_RETAIN_MIN_HEAP_BLOCK)) {
        return false;
    }

    FileInfo file_info;
    if((storage_common_stat(desktop->storage, target, &file_info) != FSE_OK) ||
       file_info_is_dir(&file_info) || (file_info.size > DESKTOP_SHORTCUT_RETAIN_MAX_FAP_BYTES)) {
        return false;
    }

    FlipperApplication* app = flipper_application_alloc(desktop->storage, firmware_api_interface);
    const FlipperApplicationPreloadStatus preload_status =
        flipper_application_preload_manifest(app, target);
    uint16_t stack_size = UINT16_MAX;
    bool expected_app = false;
    if(preload_status == FlipperApplicationPreloadStatusSuccess) {
        const FlipperApplicationManifest* manifest = flipper_application_get_manifest(app);
        stack_size = manifest->stack_size;
        expected_app = strcmp(manifest->name, DESKTOP_SHORTCUT_RETAIN_ARF_HUB_NAME) == 0;
    }
    const bool retain = expected_app && (stack_size <= DESKTOP_SHORTCUT_RETAIN_MAX_STACK_BYTES);
    flipper_application_free(app);

    if(retain) {
        FURI_LOG_I(
            TAG,
            "Retain shortcut animation for '%s' (%llu bytes, stack %u)",
            target,
            (unsigned long long)file_info.size,
            stack_size);
    }
    return retain;
}

static void
    desktop_scene_main_launch_target(Desktop* desktop, const char* target, bool from_shortcut) {
    desktop->shortcut_animation_retain_pending =
        from_shortcut && desktop_scene_main_shortcut_can_retain_animation(desktop, target);
    if(desktop_scene_main_is_apps_folder_target(target)) {
        loader_start_detached_with_gui_error(desktop->loader, LOADER_APPLICATIONS_NAME, target);
    } else {
        loader_start_detached_with_gui_error(desktop->loader, target, NULL);
    }
}

static void desktop_scene_main_open_app_or_profile(Desktop* desktop, FavoriteApp* application) {
    desktop->shortcut_animation_retain_pending = false;
    bool load_ok = false;
    if(strlen(application->name_or_path) > 0) {
        if(!desktop_scene_main_check_none(application->name_or_path)) {
            desktop_scene_main_launch_target(desktop, application->name_or_path, true);
        }
        load_ok = true;
    }
    // In case of "default" setting
    if(!load_ok) {
        loader_start_detached_with_gui_error(desktop->loader, "Passport", NULL);
    }
}

static void desktop_scene_main_start_favorite(Desktop* desktop, FavoriteApp* application) {
    desktop->shortcut_animation_retain_pending = false;
    if(strlen(application->name_or_path) > 0) {
        if(!desktop_scene_main_check_none(application->name_or_path)) {
            desktop_scene_main_launch_target(desktop, application->name_or_path, true);
        }
    } else {
        loader_start_detached_with_gui_error(desktop->loader, LOADER_APPLICATIONS_NAME, NULL);
    }
}

void desktop_scene_main_callback(DesktopEvent event, void* context) {
    Desktop* desktop = (Desktop*)context;
    if(desktop->in_transition) return;
    view_dispatcher_send_custom_event(desktop->view_dispatcher, event);
}

void desktop_scene_main_on_enter(void* context) {
    Desktop* desktop = (Desktop*)context;
    DesktopMainView* main_view = desktop->main_view;
    desktop->shortcut_animation_retain_pending = false;

    animation_manager_set_context(desktop->animation_manager, desktop);
    animation_manager_set_new_idle_callback(
        desktop->animation_manager, desktop_scene_main_new_idle_animation_callback);
    animation_manager_set_check_callback(
        desktop->animation_manager, desktop_scene_main_check_animation_callback);
    animation_manager_set_interact_callback(
        desktop->animation_manager, desktop_scene_main_interact_animation_callback);

    desktop_main_set_callback(main_view, desktop_scene_main_callback, desktop);

    view_dispatcher_switch_to_view(desktop->view_dispatcher, DesktopViewIdMain);
}

bool desktop_scene_main_on_event(void* context, SceneManagerEvent event) {
    Desktop* desktop = (Desktop*)context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        switch(event.event) {
        case DesktopMainEventOpenMenu: {
            desktop->shortcut_animation_retain_pending = false;
            Loader* loader = furi_record_open(RECORD_LOADER);
            loader_show_menu(loader);
            furi_record_close(RECORD_LOADER);
            consumed = true;
        } break;

        case DesktopMainEventLock:
            scene_manager_set_scene_state(desktop->scene_manager, DesktopSceneLockMenu, 0);
            desktop_lock(desktop);
            consumed = true;
            break;

        case DesktopMainEventOpenLockMenu:
            scene_manager_next_scene(desktop->scene_manager, DesktopSceneLockMenu);
            consumed = true;
            break;

        case DesktopMainEventOpenDebug:
            scene_manager_next_scene(desktop->scene_manager, DesktopSceneDebug);
            consumed = true;
            break;

        case DesktopMainEventOpenArchive:
#ifdef APP_ARCHIVE
            desktop_switch_to_app(desktop, &FLIPPER_ARCHIVE);
#endif
            consumed = true;
            break;

        case DesktopMainEventOpenPowerOff: {
            desktop->shortcut_animation_retain_pending = false;
            loader_start_detached_with_gui_error(desktop->loader, "Power", "off");
            consumed = true;
            break;
        }

        case DesktopMainEventOpenFavoriteLeftShort:
            desktop_scene_main_start_favorite(
                desktop, &desktop->settings.favorite_apps[FavoriteAppLeftShort]);
            consumed = true;
            break;
        case DesktopMainEventOpenFavoriteLeftLong:
            desktop_scene_main_start_favorite(
                desktop, &desktop->settings.favorite_apps[FavoriteAppLeftLong]);
            consumed = true;
            break;
        case DesktopMainEventOpenFavoriteRightShort:
            desktop_scene_main_start_favorite(
                desktop, &desktop->settings.favorite_apps[FavoriteAppRightShort]);
            consumed = true;
            break;
        case DesktopMainEventOpenFavoriteRightLong:
            desktop_scene_main_start_favorite(
                desktop, &desktop->settings.favorite_apps[FavoriteAppRightLong]);
            consumed = true;
            break;
        case DesktopMainEventOpenFavoriteOkLong:
            desktop_scene_main_start_favorite(
                desktop, &desktop->settings.favorite_apps[FavoriteAppOkLong]);
            consumed = true;
            break;

        case DesktopAnimationEventCheckAnimation:
            animation_manager_check_blocking_process(desktop->animation_manager);
            consumed = true;
            break;
        case DesktopAnimationEventNewIdleAnimation:
            animation_manager_new_idle_process(desktop->animation_manager);
            consumed = true;
            break;
        case DesktopAnimationEventInteractAnimation:
            if(!animation_manager_interact_process(desktop->animation_manager)) {
                desktop_scene_main_open_app_or_profile(
                    desktop, &desktop->settings.favorite_apps[FavoriteAppRightShort]);
            }
            consumed = true;
            break;

        case DesktopLockedEventUpdate:
            desktop_view_locked_update(desktop->locked_view);
            consumed = true;
            break;

        default:
            break;
        }
    }

    return consumed;
}

void desktop_scene_main_on_exit(void* context) {
    Desktop* desktop = (Desktop*)context;

    animation_manager_set_new_idle_callback(desktop->animation_manager, NULL);
    animation_manager_set_check_callback(desktop->animation_manager, NULL);
    animation_manager_set_interact_callback(desktop->animation_manager, NULL);
    animation_manager_set_context(desktop->animation_manager, desktop);
}
