#include "../subghz_i.h"
#include <flipper_application/flipper_application.h>
#include <furi/core/memmgr_heap.h>
#include <loader/firmware_api/firmware_api.h>

// Keep the installed FW Packages path and require the direct-launch contract.
#define RAW_EDIT_PATH    EXT_PATH("apps/ARF Tools/subghz_raw_edit.fap")
#define RAW_EDIT_VERSION ((1U << 16) | 9U)

enum SubmenuIndex {
    SubmenuIndexAutoDecode,
    SubmenuIndexDecode,
    SubmenuIndexEdit,
    SubmenuIndexDelete,
    SubmenuIndexNiceAnalysis,
    SubmenuIndexRawEditor,
};

void subghz_scene_more_raw_submenu_callback(void* context, uint32_t index) {
    SubGhz* subghz = context;
    view_dispatcher_send_custom_event(subghz->view_dispatcher, index);
}

static bool subghz_scene_more_raw_can_edit(SubGhz* subghz) {
    if(subghz_rx_key_state_get(subghz) != SubGhzRxKeyStateRAWLoad ||
       !furi_string_start_with(subghz->file_path, SUBGHZ_APP_FOLDER "/") ||
       !subghz_path_is_file(subghz->file_path) || memmgr_get_free_heap() < 4096 ||
       memmgr_heap_get_max_free_block() < 1024) {
        return false;
    }

    Storage* storage = furi_record_open(RECORD_STORAGE);
    bool available = false;
    if(storage_file_exists(storage, RAW_EDIT_PATH)) {
        FlipperApplication* app = flipper_application_alloc(storage, firmware_api_interface);
        available =
            flipper_application_preload_manifest(app, RAW_EDIT_PATH) ==
                FlipperApplicationPreloadStatusSuccess &&
            !flipper_application_is_plugin(app) &&
            flipper_application_get_manifest(app)->app_version == RAW_EDIT_VERSION;
        flipper_application_free(app);
    }
    furi_record_close(RECORD_STORAGE);
    return available;
}

void subghz_scene_more_raw_on_enter(void* context) {
    SubGhz* subghz = context;

    submenu_add_item(
        subghz->submenu,
        "Auto Decode",
        SubmenuIndexAutoDecode,
        subghz_scene_more_raw_submenu_callback,
        subghz);

    submenu_add_item(
        subghz->submenu,
        "Decode Current",
        SubmenuIndexDecode,
        subghz_scene_more_raw_submenu_callback,
        subghz);

    if(subghz_scene_more_raw_can_edit(subghz)) {
        submenu_add_item(
            subghz->submenu,
            "Edit RAW",
            SubmenuIndexRawEditor,
            subghz_scene_more_raw_submenu_callback,
            subghz);
    }

    submenu_add_item(
        subghz->submenu, "Nice O-Code", SubmenuIndexNiceAnalysis,
        subghz_scene_more_raw_submenu_callback, subghz);

    submenu_add_item(
        subghz->submenu,
        "Rename",
        SubmenuIndexEdit,
        subghz_scene_more_raw_submenu_callback,
        subghz);

    submenu_add_item(
        subghz->submenu,
        "Delete",
        SubmenuIndexDelete,
        subghz_scene_more_raw_submenu_callback,
        subghz);

    submenu_set_selected_item(
        subghz->submenu, scene_manager_get_scene_state(subghz->scene_manager, SubGhzSceneMoreRAW));

    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdMenu);
}

static bool subghz_scene_more_raw_analyze_nice(SubGhz* subghz) {
    /* Restore before scheduling; no radio or worker can survive the handoff. */
    if(!subghz_scene_decode_raw_cleanup(subghz)) {
        dialog_message_show_storage_error(subghz->dialogs, "Protocol pack\nrestore failed.");
        return true;
    }
    return subghz_launch_saved_analyzer(
        subghz, EXT_PATH("apps/Sub-GHz/nice_o_code.fap"));
}

bool subghz_scene_more_raw_on_event(void* context, SceneManagerEvent event) {
    SubGhz* subghz = context;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SubmenuIndexRawEditor) {
            if(!subghz_file_available(subghz)) return true;
            if(!subghz_scene_more_raw_can_edit(subghz)) {
                dialog_message_show_storage_error(subghz->dialogs, "RAW Edit unavailable.");
                return true;
            }
            if(!subghz_scene_decode_raw_cleanup(subghz)) {
                dialog_message_show_storage_error(subghz->dialogs, "Protocol pack\nrestore failed.");
                return true;
            }
            return subghz_launch_saved_analyzer(subghz, RAW_EDIT_PATH);
        }
        if(event.event == SubmenuIndexNiceAnalysis && subghz_file_available(subghz)) {
            return subghz_scene_more_raw_analyze_nice(subghz);
        }
        if(event.event == SubmenuIndexDelete) {
            if(subghz_file_available(subghz)) {
                scene_manager_set_scene_state(
                    subghz->scene_manager, SubGhzSceneReadRAW, SubGhzCustomEventManagerNoSet);
                scene_manager_set_scene_state(
                    subghz->scene_manager, SubGhzSceneMoreRAW, SubmenuIndexDelete);
                scene_manager_next_scene(subghz->scene_manager, SubGhzSceneDeleteRAW);
                return true;
            } else {
                if(!scene_manager_search_and_switch_to_previous_scene(
                       subghz->scene_manager, SubGhzSceneStart)) {
                    scene_manager_stop(subghz->scene_manager);
                    view_dispatcher_stop(subghz->view_dispatcher);
                }
            }
        } else if(event.event == SubmenuIndexEdit) {
            if(subghz_file_available(subghz)) {
                furi_string_reset(subghz->file_path_tmp);
                scene_manager_set_scene_state(
                    subghz->scene_manager, SubGhzSceneMoreRAW, SubmenuIndexEdit);
                scene_manager_next_scene(subghz->scene_manager, SubGhzSceneSaveName);
                return true;
            } else {
                if(!scene_manager_search_and_switch_to_previous_scene(
                       subghz->scene_manager, SubGhzSceneStart)) {
                    scene_manager_stop(subghz->scene_manager);
                    view_dispatcher_stop(subghz->view_dispatcher);
                }
            }
        } else if(event.event == SubmenuIndexAutoDecode || event.event == SubmenuIndexDecode) {
            if(subghz_file_available(subghz)) {
                subghz->decode_raw_auto = event.event == SubmenuIndexAutoDecode;
                scene_manager_set_scene_state(
                    subghz->scene_manager, SubGhzSceneMoreRAW, event.event);
                scene_manager_next_scene(subghz->scene_manager, SubGhzSceneDecodeRAW);
                return true;
            } else {
                if(!scene_manager_search_and_switch_to_previous_scene(
                       subghz->scene_manager, SubGhzSceneStart)) {
                    scene_manager_stop(subghz->scene_manager);
                    view_dispatcher_stop(subghz->view_dispatcher);
                }
            }
        }
    }
    return false;
}

void subghz_scene_more_raw_on_exit(void* context) {
    SubGhz* subghz = context;
    submenu_reset(subghz->submenu);
}
