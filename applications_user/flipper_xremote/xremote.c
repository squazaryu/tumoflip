/*!
 *  @file flipper-xremote/xremote.c
    @license This project is released under the GNU GPLv3 License
 *  @copyright (c) 2023 Sandro Kalatozishvili (s.kalatoz@gmail.com)
 *
 * @brief Entrypoint and factory of the XRemote main app.
 */

#include "xremote.h"
#include "xremote_learn.h"
#include "xremote_settings.h"
#include "xremote_analyzer.h"
#include "xremote_designer.h"
#include "xremote_device_profiles.h"

#include "views/xremote_about_view.h"

#include <dialogs/dialogs.h>
#include <loader/loader.h>
#include <storage/storage.h>
#include <toolbox/saved_struct.h>

#include <string.h>

#define TAG "XRemote"
#define XREMOTE_AC_COMPONENT_PATH \
    EXT_PATH("apps_data/tumoflip_xremote/components/tumoflip_xremote_ac.fap")
#define XREMOTE_COCKPIT_PATH EXT_PATH("apps/Module One/Diagnostics/cockpit.fap")
#define XREMOTE_COCKPIT_ARG_PREFIX "cockpit:"

void xremote_get_version(char* version, size_t length) {
    snprintf(
        version,
        length,
        "%d.%d.%d",
        XREMOTE_VERSION_MAJ,
        XREMOTE_VERSION_MIN,
        XREMOTE_BUILD_NUMBER);
}

static uint32_t xremote_view_exit_callback(void* context) {
    UNUSED(context);
    return XRemoteViewSubmenu;
}

static uint32_t xremote_exit_callback(void* context) {
    UNUSED(context);
    return VIEW_NONE;
}

static void xremote_child_clear_callback(void* context) {
    xremote_app_assert_void(context);
    xremote_app_free((XRemoteApp*)context);
}

static XRemoteApp* xremote_about_alloc(XRemoteAppContext* app_ctx) {
    XRemoteApp* app = xremote_app_alloc(app_ctx);
    xremote_app_view_alloc(app, XRemoteViewAbout, xremote_about_view_alloc);
    xremote_app_view_set_previous_callback(app, xremote_view_exit_callback);
    return app;
}

static const char* xremote_cockpit_return_arg(const XRemoteApp* app) {
    const char* argument = app->app_ctx->app_argument;
    if(!argument) return NULL;

    const size_t prefix_length = strlen(XREMOTE_COCKPIT_ARG_PREFIX);
    if(strncmp(argument, XREMOTE_COCKPIT_ARG_PREFIX, prefix_length) != 0) return NULL;

    const char* selected_item = argument + prefix_length;
    if(selected_item[0] == '\0') return NULL;
    for(const char* cursor = selected_item; *cursor; cursor++) {
        if(*cursor < '0' || *cursor > '9') return NULL;
    }
    return selected_item;
}

static void xremote_launch_component(XRemoteApp* app, const char* path) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    const bool installed = storage_file_exists(storage, path);
    furi_record_close(RECORD_STORAGE);
    if(!installed) {
        DialogsApp* dialogs = furi_record_open(RECORD_DIALOGS);
        dialog_message_show_storage_error(dialogs, "XRemote component\nis not installed");
        furi_record_close(RECORD_DIALOGS);
        return;
    }

    Loader* loader = furi_record_open(RECORD_LOADER);
    loader_clear_launch_queue(loader);
    loader_enqueue_launch(loader, path, NULL, LoaderDeferredLaunchFlagGui);

    FuriString* self_path = furi_string_alloc();
    if(loader_get_application_launch_path(loader, self_path)) {
        loader_enqueue_launch(
            loader,
            furi_string_get_cstr(self_path),
            app->app_ctx->app_argument,
            LoaderDeferredLaunchFlagGui);
    }
    furi_string_free(self_path);

    const char* cockpit_selected_item = xremote_cockpit_return_arg(app);
    if(cockpit_selected_item) {
        loader_enqueue_launch(
            loader,
            XREMOTE_COCKPIT_PATH,
            cockpit_selected_item,
            LoaderDeferredLaunchFlagGui);
    }
    furi_record_close(RECORD_LOADER);
    view_dispatcher_stop(app->app_ctx->view_dispatcher);
}

void xremote_submenu_callback(void* context, uint32_t index) {
    furi_assert(context);
    XRemoteApp* app = (XRemoteApp*)context;

    /* Cleanup previous app first */
    xremote_app_user_context_free(app);
    XRemoteApp* child = NULL;

    /* Allocate child app and view based on submenu selection */
    if(index == XRemoteViewLearn)
        child = xremote_learn_alloc(app->app_ctx);
    else if(index == XRemoteViewDesigner)
        child = xremote_designer_alloc(app->app_ctx);
    else if(index == XRemoteViewAcSmart) {
        xremote_launch_component(app, XREMOTE_AC_COMPONENT_PATH);
        return;
    }
    else if(index == XRemoteViewDeviceProfiles)
        child = xremote_device_profiles_alloc(app->app_ctx);
    else if(index == XRemoteViewAnalyzer)
        child = xremote_analyzer_alloc(app->app_ctx);
    else if(index == XRemoteViewSettings)
        child = xremote_settings_alloc(app->app_ctx);
    else if(index == XRemoteViewAbout)
        child = xremote_about_alloc(app->app_ctx);

    if(child != NULL) {
        /* Switch to the view of newely allocated app */
        xremote_app_set_user_context(app, child, xremote_child_clear_callback);
        xremote_app_switch_to_view(child, index);
    }
}

static bool xremote_infra_settings_load(bool is_otg_enabled) {
    InfraredSettings settings = {0};
    bool infrared_app_settings_loaded = saved_struct_load(
        INFRARED_SETTINGS_PATH,
        &settings,
        sizeof(InfraredSettings),
        INFRARED_SETTINGS_MAGIC,
        INFRARED_SETTINGS_VERSION);

    if(infrared_app_settings_loaded) {
        if(settings.tx_pin < FuriHalInfraredTxPinMax) {
            furi_hal_infrared_set_tx_output(settings.tx_pin);
            if(settings.otg_enabled != is_otg_enabled) {
                if(settings.otg_enabled) {
                    furi_hal_power_enable_otg();
                } else {
                    furi_hal_power_disable_otg();
                }
            }
        } else {
            FuriHalInfraredTxPin tx_pin_detected = furi_hal_infrared_detect_tx_output();
            furi_hal_infrared_set_tx_output(tx_pin_detected);
            if(tx_pin_detected != FuriHalInfraredTxPinInternal) {
                furi_hal_power_enable_otg();
            }
        }
    }

    return infrared_app_settings_loaded;
}

static void xremote_infra_settings_restore(bool was_otg_enabled) {
    furi_hal_infrared_set_tx_output(FuriHalInfraredTxPinInternal);
    if(furi_hal_power_is_otg_enabled() != was_otg_enabled) {
        if(was_otg_enabled) {
            furi_hal_power_enable_otg();
        } else {
            furi_hal_power_disable_otg();
        }
    }
}

int32_t xremote_main(void* p) {
    /* Allocate context and main application */
    XRemoteAppContext* context = xremote_app_context_alloc(p);
    XRemoteApp* app = xremote_app_alloc(context);

    /* Allocate and build the menu */
    xremote_app_submenu_alloc(app, XRemoteViewSubmenu, xremote_exit_callback);
    xremote_app_submenu_add(app, "Learn", XRemoteViewLearn, xremote_submenu_callback);
    xremote_app_submenu_add(app, "Designer", XRemoteViewDesigner, xremote_submenu_callback);
    xremote_app_submenu_add(app, "AC Smart", XRemoteViewAcSmart, xremote_submenu_callback);
    xremote_app_submenu_add(app, "Profiles", XRemoteViewDeviceProfiles, xremote_submenu_callback);
    xremote_app_submenu_add(app, "Analyzer", XRemoteViewAnalyzer, xremote_submenu_callback);
    xremote_app_submenu_add(app, "Settings", XRemoteViewSettings, xremote_submenu_callback);
    xremote_app_submenu_add(app, "About", XRemoteViewAbout, xremote_submenu_callback);

    /* Load infrared settings and save OTG state */
    bool is_otg_enabled = furi_hal_power_is_otg_enabled();
    bool infra_settings_loaded = xremote_infra_settings_load(is_otg_enabled);

    /* Switch to main menu by default and run disparcher*/
    xremote_app_switch_to_view(app, XRemoteViewSubmenu);
    view_dispatcher_run(app->app_ctx->view_dispatcher);

    /* Restore infrared settings and OTG state */
    if(infra_settings_loaded) xremote_infra_settings_restore(is_otg_enabled);

    /* Cleanup and exit */
    xremote_app_free(app);
    xremote_app_context_free(context);
    return 0;
}
