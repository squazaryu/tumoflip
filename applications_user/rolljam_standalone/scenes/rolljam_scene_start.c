// scenes/rolljam_scene_start.c
#include "../rolljam_app_i.h"
#include "../helpers/rolljam_storage.h"

//#include "rolljam_standalone_icons.h"

#define TAG "RollJamSceneStart"

typedef enum {
    SubmenuIndexRollJamReceiver,
#ifdef ENABLE_DUAL_RX_SCENE
    SubmenuIndexRollJamDualReceiver,
#endif
#ifdef ENABLE_SHIELD_RX_SCENE
    SubmenuIndexRollJamShieldReceiver,
#endif
    SubmenuIndexRollJamSaved,
    SubmenuIndexRollJamReceiverConfig,
#ifdef ENABLE_SUB_DECODE_SCENE
    SubmenuIndexRollJamSubDecode,
#endif
#ifdef ENABLE_TIMING_TUNER_SCENE
    SubmenuIndexRollJamTimingTuner,
#endif
    SubmenuIndexRollJamAbout,
} SubmenuIndex;

// Forward declaration
static void rolljam_scene_start_open_saved_captures(RollJamApp* app);

static void rolljam_scene_start_submenu_callback(void* context, uint32_t index) {
    furi_check(context);
    RollJamApp* app = context;

    // Handle "Saved Captures" directly here, not via custom event
    if(index == SubmenuIndexRollJamSaved) {
        rolljam_scene_start_open_saved_captures(app);
    } else {
        view_dispatcher_send_custom_event(app->view_dispatcher, index);
    }
}

static void rolljam_scene_start_open_saved_captures(RollJamApp* app) {
    FURI_LOG_I(TAG, "[1] Opening saved captures browser");
    FURI_LOG_I(TAG, "[1a] ROLLJAM_APP_FOLDER = %s", ROLLJAM_APP_FOLDER);

    // Check and create folder
    FURI_LOG_D(TAG, "[2] Opening storage");
    Storage* storage = furi_record_open(RECORD_STORAGE);

    if(!storage) {
        FURI_LOG_E(TAG, "[2a] Failed to open storage!");
        return;
    }

    FURI_LOG_D(TAG, "[3] Checking folder exists");

    if(!storage_dir_exists(storage, ROLLJAM_APP_FOLDER)) {
        FURI_LOG_I(TAG, "[4] Creating folder");
        storage_simply_mkdir(storage, ROLLJAM_APP_FOLDER);
    }

#ifndef REMOVE_LOGS
    bool folder_ok = storage_dir_exists(storage, ROLLJAM_APP_FOLDER);
    FURI_LOG_D(TAG, "[5] Folder exists: %s", folder_ok ? "yes" : "no");
#endif

    furi_record_close(RECORD_STORAGE);
    FURI_LOG_D(TAG, "[6] Storage closed");

    // Check file_path
    FURI_LOG_D(TAG, "[7] Checking app->file_path");
    if(!app->file_path) {
        FURI_LOG_E(TAG, "[7a] app->file_path is NULL!");
        return;
    }

    // Set starting path
    FURI_LOG_D(TAG, "[8] Setting file_path");
    furi_string_set(app->file_path, ROLLJAM_APP_FOLDER);
    FURI_LOG_D(TAG, "[9] file_path set to: %s", furi_string_get_cstr(app->file_path));

    // Configure file browser
    FURI_LOG_D(TAG, "[10] Creating browser_options");
    DialogsFileBrowserOptions browser_options;

    FURI_LOG_D(TAG, "[11] Calling dialog_file_browser_set_basic_options");
    dialog_file_browser_set_basic_options(&browser_options, ".psf", NULL);

    FURI_LOG_D(TAG, "[12] Setting browser_options fields");
    browser_options.base_path = ROLLJAM_APP_FOLDER;
    browser_options.skip_assets = true;
    browser_options.hide_dot_files = true;

    FURI_LOG_D(TAG, "[13] Checking app->dialogs");
    FURI_LOG_D(TAG, "[13a] app->dialogs = %p", (void*)app->dialogs);

    if(!app->dialogs) {
        FURI_LOG_E(TAG, "[13b] dialogs is NULL! Trying to open...");
        app->dialogs = furi_record_open(RECORD_DIALOGS);
        if(!app->dialogs) {
            FURI_LOG_E(TAG, "[13c] Still NULL after open attempt!");
            return;
        }
        FURI_LOG_I(TAG, "[13d] dialogs opened successfully");
    }

    FURI_LOG_I(TAG, "[14] === CALLING dialog_file_browser_show ===");
    FURI_LOG_D(TAG, "[14a] dialogs=%p, file_path=%p", (void*)app->dialogs, (void*)app->file_path);

    bool file_selected =
        dialog_file_browser_show(app->dialogs, app->file_path, app->file_path, &browser_options);

    FURI_LOG_I(TAG, "[15] === RETURNED from dialog_file_browser_show ===");
    FURI_LOG_D(TAG, "[15a] file_selected = %d", file_selected);

    if(file_selected) {
        FURI_LOG_I(TAG, "[16] File selected: %s", furi_string_get_cstr(app->file_path));

        if(app->loaded_file_path) {
            FURI_LOG_D(TAG, "[17] Freeing old loaded_file_path");
            furi_string_free(app->loaded_file_path);
        }

        FURI_LOG_D(TAG, "[18] Allocating new loaded_file_path");
        app->loaded_file_path = furi_string_alloc_set(app->file_path);

        FURI_LOG_D(TAG, "[19] Navigating to SavedInfo scene");
        scene_manager_next_scene(app->scene_manager, RollJamSceneSavedInfo);
    } else {
        FURI_LOG_I(TAG, "[16] File browser cancelled or empty");
    }

    FURI_LOG_I(TAG, "[20] open_saved_captures complete");
}

void rolljam_scene_start_on_enter(void* context) {
    furi_check(context);
    RollJamApp* app = context;
    FURI_LOG_I("RollJam", "Entering Start Scene");

    rolljam_release_shared_radio_state(app);
    FURI_LOG_I("RollJam", "Radio state released");

#ifdef ENABLE_SHIELD_RX_SCENE
    submenu_add_item(
        app->submenu,
        "RollJam!",
        SubmenuIndexRollJamShieldReceiver,
        rolljam_scene_start_submenu_callback,
        app);
#endif

    submenu_add_item(
        app->submenu,
        "Saved Captures",
        SubmenuIndexRollJamSaved,
        rolljam_scene_start_submenu_callback,
        app);

    submenu_add_item(
        app->submenu,
        "Settings",
        SubmenuIndexRollJamReceiverConfig,
        rolljam_scene_start_submenu_callback,
        app);

    submenu_add_item(
        app->submenu,
        "About",
        SubmenuIndexRollJamAbout,
        rolljam_scene_start_submenu_callback,
        app);

    submenu_set_selected_item(
        app->submenu, scene_manager_get_scene_state(app->scene_manager, RollJamSceneStart));

    FURI_LOG_I("RollJam", "Switching to Submenu View");
    view_dispatcher_switch_to_view(app->view_dispatcher, RollJamViewSubmenu);
}

bool rolljam_scene_start_on_event(void* context, SceneManagerEvent event) {
    furi_check(context);
    RollJamApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SubmenuIndexRollJamAbout) {
            scene_manager_next_scene(app->scene_manager, RollJamSceneAbout);
            consumed = true;
        }
#ifdef ENABLE_SHIELD_RX_SCENE
        else if(event.event == SubmenuIndexRollJamShieldReceiver) {
            scene_manager_next_scene(app->scene_manager, RollJamSceneShieldReceiver);
            consumed = true;
        }
#endif
        else if(event.event == SubmenuIndexRollJamReceiverConfig) {
            scene_manager_next_scene(app->scene_manager, RollJamSceneShieldReceiverConfig);
            consumed = true;
        }
        scene_manager_set_scene_state(app->scene_manager, RollJamSceneStart, event.event);
    }

    return consumed;
}

void rolljam_scene_start_on_exit(void* context) {
    furi_check(context);
    RollJamApp* app = context;
    submenu_reset(app->submenu);
}
