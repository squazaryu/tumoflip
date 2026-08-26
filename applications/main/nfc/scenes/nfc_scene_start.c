#include "../nfc_app_i.h"
#include <dolphin/dolphin.h>

enum SubmenuIndex {
    SubmenuIndexRead,
    SubmenuIndexDetectReader,
    SubmenuIndexSaved,
    SubmenuIndexRecoverMfClassic,
    SubmenuIndexRecoverMfPlus,
    SubmenuIndexRecoverMfUltralight,
    SubmenuIndexRecoverType4,
    SubmenuIndexExtraAction,
    SubmenuIndexAddManually,
    SubmenuIndexDebug,
};

void nfc_scene_start_submenu_callback(void* context, uint32_t index) {
    NfcApp* nfc = context;

    view_dispatcher_send_custom_event(nfc->view_dispatcher, index);
}

void nfc_scene_start_on_enter(void* context) {
    NfcApp* nfc = context;
    Submenu* submenu = nfc->submenu;

    // Clear file name and device contents
    furi_string_reset(nfc->file_name);
    nfc_device_clear(nfc->nfc_device);
    nfc->checkpoint_recovered = false;
    nfc->checkpoint_protocol = NfcProtocolInvalid;
    iso14443_3a_reset(nfc->iso14443_3a_edit_data);
    // Reset detected protocols list
    nfc_detected_protocols_reset(nfc->detected_protocols);

    submenu_add_item(submenu, "Read", SubmenuIndexRead, nfc_scene_start_submenu_callback, nfc);
    submenu_add_item(
        submenu,
        "Extract MFC Keys",
        SubmenuIndexDetectReader,
        nfc_scene_start_submenu_callback,
        nfc);
    submenu_add_item(submenu, "Saved", SubmenuIndexSaved, nfc_scene_start_submenu_callback, nfc);
    if(nfc_checkpoint_exists(nfc, NfcProtocolMfClassic)) {
        submenu_add_item(
            submenu,
            "Recover MIFARE Classic read",
            SubmenuIndexRecoverMfClassic,
            nfc_scene_start_submenu_callback,
            nfc);
    }
    if(nfc_checkpoint_exists(nfc, NfcProtocolMfPlus)) {
        submenu_add_item(
            submenu,
            "Recover MIFARE Plus read",
            SubmenuIndexRecoverMfPlus,
            nfc_scene_start_submenu_callback,
            nfc);
    }
    if(nfc_checkpoint_exists(nfc, NfcProtocolMfUltralight)) {
        submenu_add_item(
            submenu,
            "Recover Ultralight read",
            SubmenuIndexRecoverMfUltralight,
            nfc_scene_start_submenu_callback,
            nfc);
    }
    if(nfc_checkpoint_exists(nfc, NfcProtocolType4Tag)) {
        submenu_add_item(
            submenu,
            "Recover Type 4 read",
            SubmenuIndexRecoverType4,
            nfc_scene_start_submenu_callback,
            nfc);
    }
    submenu_add_item(
        submenu, "Extra Actions", SubmenuIndexExtraAction, nfc_scene_start_submenu_callback, nfc);
    submenu_add_item(
        submenu, "Add Manually", SubmenuIndexAddManually, nfc_scene_start_submenu_callback, nfc);

    if(furi_hal_rtc_is_flag_set(FuriHalRtcFlagDebug)) {
        submenu_add_item(
            submenu, "Debug", SubmenuIndexDebug, nfc_scene_start_submenu_callback, nfc);
    }

    submenu_set_selected_item(
        submenu, scene_manager_get_scene_state(nfc->scene_manager, NfcSceneStart));

    view_dispatcher_switch_to_view(nfc->view_dispatcher, NfcViewMenu);
}

static bool nfc_scene_start_recover_checkpoint(NfcApp* nfc, NfcProtocol protocol) {
    const char* path = nfc_checkpoint_path_for_protocol(protocol);
    if(path == NULL) return false;

    furi_string_set(nfc->file_path, path);
    if(!nfc_load_file(nfc, nfc->file_path, true) ||
       nfc_device_get_protocol(nfc->nfc_device) != protocol) {
        furi_string_set(nfc->file_path, NFC_APP_FOLDER);
        furi_string_reset(nfc->file_name);
        return false;
    }

    /* Save Name must create a regular dump in /ext/nfc, never overwrite the hidden checkpoint. */
    furi_string_reset(nfc->file_name);
    nfc->checkpoint_recovered = true;
    nfc->checkpoint_protocol = protocol;
    scene_manager_next_scene(nfc->scene_manager, NfcSceneSavedMenu);
    return true;
}

bool nfc_scene_start_on_event(void* context, SceneManagerEvent event) {
    NfcApp* nfc = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        consumed = true;
        if(event.event == SubmenuIndexRead) {
            scene_manager_next_scene(nfc->scene_manager, NfcSceneDetect);
            dolphin_deed(DolphinDeedNfcRead);
        } else if(event.event == SubmenuIndexDetectReader) {
            scene_manager_next_scene(nfc->scene_manager, NfcSceneMfClassicDetectReader);
        } else if(event.event == SubmenuIndexSaved) {
            scene_manager_next_scene(nfc->scene_manager, NfcSceneFileSelect);
        } else if(event.event == SubmenuIndexRecoverMfClassic) {
            consumed = nfc_scene_start_recover_checkpoint(nfc, NfcProtocolMfClassic);
        } else if(event.event == SubmenuIndexRecoverMfPlus) {
            consumed = nfc_scene_start_recover_checkpoint(nfc, NfcProtocolMfPlus);
        } else if(event.event == SubmenuIndexRecoverMfUltralight) {
            consumed = nfc_scene_start_recover_checkpoint(nfc, NfcProtocolMfUltralight);
        } else if(event.event == SubmenuIndexRecoverType4) {
            consumed = nfc_scene_start_recover_checkpoint(nfc, NfcProtocolType4Tag);
        } else if(event.event == SubmenuIndexExtraAction) {
            scene_manager_next_scene(nfc->scene_manager, NfcSceneExtraActions);
        } else if(event.event == SubmenuIndexAddManually) {
            scene_manager_next_scene(nfc->scene_manager, NfcSceneSetType);
        } else if(event.event == SubmenuIndexDebug) {
            scene_manager_next_scene(nfc->scene_manager, NfcSceneDebug);
        } else {
            consumed = false;
        }
        if(consumed) {
            scene_manager_set_scene_state(nfc->scene_manager, NfcSceneStart, event.event);
        }
    }
    return consumed;
}

void nfc_scene_start_on_exit(void* context) {
    NfcApp* nfc = context;

    submenu_reset(nfc->submenu);
}
