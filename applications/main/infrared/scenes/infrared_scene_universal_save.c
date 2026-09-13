#include "../infrared_app_i.h"

#include "common/infrared_scene_universal_common.h"

#define TAG "InfraredUniversalSave"

typedef enum {
    SubmenuIndexSaveAsNewRemote,
    SubmenuIndexAddToExistingRemote,
} SubmenuIndex;

static void infrared_scene_universal_save_submenu_callback(void* context, uint32_t index) {
    InfraredApp* infrared = context;
    view_dispatcher_send_custom_event(infrared->view_dispatcher, index);
}

static InfraredErrorCode infrared_scene_universal_save_append_preserving(
    InfraredRemote* remote,
    const InfraredSignal* signal,
    const char* signal_name) {
    FuriString* original_path = furi_string_alloc_set(infrared_remote_get_path(remote));
    FuriString* backup_path = furi_string_alloc();
    Storage* storage = furi_record_open(RECORD_STORAGE);

    FS_Error status;
    do {
        furi_string_printf(
            backup_path,
            "%s.tumobak%08lx",
            furi_string_get_cstr(original_path),
            (unsigned long)rand());
        status = storage_common_stat(storage, furi_string_get_cstr(backup_path), NULL);
    } while(status == FSE_OK || status == FSE_EXIST);

    InfraredErrorCode error = InfraredErrorCodeNone;
    FileInfo original_info;
    FileInfo backup_info;
    status = storage_common_stat(storage, furi_string_get_cstr(original_path), &original_info);
    if(status == FSE_OK) {
        status = storage_common_copy(
            storage, furi_string_get_cstr(original_path), furi_string_get_cstr(backup_path));
    }
    if(status == FSE_OK) {
        status = storage_common_stat(storage, furi_string_get_cstr(backup_path), &backup_info);
    }
    if(status != FSE_OK || backup_info.size != original_info.size) {
        storage_common_remove(storage, furi_string_get_cstr(backup_path));
        error = InfraredErrorCodeFileOperationFailed;
    } else {
        error = infrared_remote_append_signal(remote, signal, signal_name);
        if(INFRARED_ERROR_PRESENT(error)) {
            // Rename restores the pre-write copy over a partial append.
            status = storage_common_rename(
                storage, furi_string_get_cstr(backup_path), furi_string_get_cstr(original_path));
            if(status == FSE_OK) {
                infrared_remote_load(remote, furi_string_get_cstr(original_path));
            } else {
                // The backup is intentionally retained if restoration fails.
                FURI_LOG_E(
                    TAG,
                    "Restore failed (%d), backup kept at %s",
                    status,
                    furi_string_get_cstr(backup_path));
            }
        } else {
            storage_common_remove(storage, furi_string_get_cstr(backup_path));
        }
    }

    furi_record_close(RECORD_STORAGE);
    furi_string_free(backup_path);
    furi_string_free(original_path);
    return error;
}

static void infrared_scene_universal_save_add_to_existing(InfraredApp* infrared) {
    DialogsFileBrowserOptions browser_options;
    dialog_file_browser_set_basic_options(&browser_options, INFRARED_APP_EXTENSION, &I_ir_10px);
    browser_options.base_path = INFRARED_APP_FOLDER;

    if(furi_string_empty(infrared->file_path)) {
        furi_string_set(infrared->file_path, INFRARED_APP_FOLDER);
    }

    // Cancelling the browser returns to this choice without changing any file.
    if(!dialog_file_browser_show(
           infrared->dialogs, infrared->file_path, infrared->file_path, &browser_options)) {
        return;
    }

    InfraredErrorCode error =
        infrared_remote_load(infrared->remote, furi_string_get_cstr(infrared->file_path));
    if(!INFRARED_ERROR_PRESENT(error)) {
        error = infrared_scene_universal_save_append_preserving(
            infrared->remote, infrared->current_signal, infrared->text_store[1]);
    }

    if(INFRARED_ERROR_PRESENT(error)) {
        infrared_show_error_message(
            infrared, "Failed to add to\n\"%s\"", furi_string_get_cstr(infrared->file_path));
    } else {
        scene_manager_next_scene(infrared->scene_manager, InfraredSceneUniversalSaveDone);
    }
}

void infrared_scene_universal_save_on_enter(void* context) {
    InfraredApp* infrared = context;
    Submenu* submenu = infrared->submenu;

    submenu_set_header(submenu, infrared->text_store[1]);
    submenu_add_item(
        submenu,
        "Save as New Remote",
        SubmenuIndexSaveAsNewRemote,
        infrared_scene_universal_save_submenu_callback,
        context);
    submenu_add_item(
        submenu,
        "Add to Existing Remote",
        SubmenuIndexAddToExistingRemote,
        infrared_scene_universal_save_submenu_callback,
        context);

    view_dispatcher_switch_to_view(infrared->view_dispatcher, InfraredViewSubmenu);
}

bool infrared_scene_universal_save_on_event(void* context, SceneManagerEvent event) {
    InfraredApp* infrared = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SubmenuIndexSaveAsNewRemote) {
            scene_manager_next_scene(infrared->scene_manager, InfraredSceneUniversalSaveName);
        } else if(event.event == SubmenuIndexAddToExistingRemote) {
            infrared_scene_universal_save_add_to_existing(infrared);
        }
        consumed = true;
    } else if(event.type == SceneManagerEventTypeBack) {
        infrared_scene_universal_common_return(infrared);
        consumed = true;
    }

    return consumed;
}

void infrared_scene_universal_save_on_exit(void* context) {
    InfraredApp* infrared = context;
    submenu_reset(infrared->submenu);
}
