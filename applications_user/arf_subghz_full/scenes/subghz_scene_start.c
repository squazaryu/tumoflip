#include "../subghz_i.h"
#include "subghz_scene_start.h"
#include <dolphin/dolphin.h>
#include <dialogs/dialogs.h>

#include <lib/subghz/protocols/raw.h>

void subghz_scene_start_submenu_callback(void* context, uint32_t index) {
    SubGhz* subghz = context;
    view_dispatcher_send_custom_event(subghz->view_dispatcher, index);
}

static bool __attribute__((unused)) subghz_scene_start_load_signal(SubGhz* subghz) {
    DialogsFileBrowserOptions browser_options;
    dialog_file_browser_set_basic_options(
        &browser_options, SUBGHZ_APP_FILENAME_EXTENSION, &I_sub1_10px);
    browser_options.base_path = SUBGHZ_APP_FOLDER;

    FuriString* selected = furi_string_alloc();
    furi_string_set(selected, SUBGHZ_APP_FOLDER);

    bool loaded = dialog_file_browser_show(subghz->dialogs, selected, selected, &browser_options);
    if(loaded) {
        loaded = subghz_key_load(subghz, furi_string_get_cstr(selected), true);
        if(loaded) {
            furi_string_set(subghz->file_path, selected);
        }
    }

    furi_string_free(selected);
    return loaded;
}

void subghz_scene_start_on_enter(void* context) {
    SubGhz* subghz = context;
    if(subghz->state_notifications == SubGhzNotificationStateStarting) {
        subghz->state_notifications = SubGhzNotificationStateIDLE;
    }

#if defined(ARF_PROFILE_KEELOQ)
    submenu_add_item(
        subghz->submenu,
        "KeeLoq Keys",
        SubmenuIndexKeeloqKeys,
        subghz_scene_start_submenu_callback,
        subghz);
    submenu_add_item(
        subghz->submenu,
        "KeeLoq BF (2 Signals)",
        SubmenuIndexKeeloqBf2,
        subghz_scene_start_submenu_callback,
        subghz);
#elif defined(ARF_PROFILE_COUNTER)
    submenu_add_item(
        subghz->submenu,
        "Load Signal",
        SubmenuIndexCounterBf,
        subghz_scene_start_submenu_callback,
        subghz);
#elif defined(ARF_PROFILE_CAR)
    submenu_add_item(
        subghz->submenu,
        "Load Signal",
        SubmenuIndexCarEmulate,
        subghz_scene_start_submenu_callback,
        subghz);
#elif defined(ARF_PROFILE_FA)
    submenu_add_item(
        subghz->submenu,
        "Frequency Analyzer",
        SubmenuIndexFrequencyAnalyzer,
        subghz_scene_start_submenu_callback,
        subghz);
#elif defined(ARF_PROFILE_PSA)
    submenu_add_item(
        subghz->submenu,
        "Load PSA Signal",
        SubmenuIndexPsaDecrypt,
        subghz_scene_start_submenu_callback,
        subghz);
#else
    submenu_add_item(
        subghz->submenu, "Read", SubmenuIndexRead, subghz_scene_start_submenu_callback, subghz);
    submenu_add_item(
        subghz->submenu,
        "Read RAW",
        SubmenuIndexReadRAW,
        subghz_scene_start_submenu_callback,
        subghz);
    submenu_add_item(
        subghz->submenu,
        "Protocol List",
        SubmenuIndexProtocolList,
        subghz_scene_start_submenu_callback,
        subghz);
    submenu_add_item(
        subghz->submenu,
        "Radio Settings",
        SubmenuIndexExtSettings,
        subghz_scene_start_submenu_callback,
        subghz);
#endif
    submenu_set_selected_item(
        subghz->submenu, scene_manager_get_scene_state(subghz->scene_manager, SubGhzSceneStart));

    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdMenu);
}

bool subghz_scene_start_on_event(void* context, SceneManagerEvent event) {
    SubGhz* subghz = context;
    if(event.type == SceneManagerEventTypeBack) {
        //exit app
        scene_manager_stop(subghz->scene_manager);
        view_dispatcher_stop(subghz->view_dispatcher);
        return true;
    } else if(event.type == SceneManagerEventTypeCustom) {
#if defined(ARF_PROFILE_KEELOQ)
        if(event.event == SubmenuIndexKeeloqKeys) {
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneStart, SubmenuIndexKeeloqKeys);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneKeeloqKeys);
            return true;
        } else if(event.event == SubmenuIndexKeeloqBf2) {
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneStart, SubmenuIndexKeeloqBf2);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneKeeloqBf2);
            return true;
        }
#elif defined(ARF_PROFILE_COUNTER)
        if(event.event == SubmenuIndexCounterBf) {
            if(subghz_scene_start_load_signal(subghz)) {
                scene_manager_next_scene(subghz->scene_manager, SubGhzSceneCounterBf);
            }
            return true;
        }
#elif defined(ARF_PROFILE_CAR)
        if(event.event == SubmenuIndexCarEmulate) {
            if(subghz_scene_start_load_signal(subghz)) {
                scene_manager_next_scene(subghz->scene_manager, SubGhzSceneCarEmulate);
            }
            return true;
        }
#elif defined(ARF_PROFILE_FA)
        if(event.event == SubmenuIndexFrequencyAnalyzer) {
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneStart, SubmenuIndexFrequencyAnalyzer);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneFrequencyAnalyzer);
            dolphin_deed(DolphinDeedSubGhzFrequencyAnalyzer);
            return true;
        }
#elif defined(ARF_PROFILE_PSA)
        if(event.event == SubmenuIndexPsaDecrypt) {
            if(subghz_scene_start_load_signal(subghz)) {
                scene_manager_next_scene(subghz->scene_manager, SubGhzScenePsaDecrypt);
            }
            return true;
        }
#else
        if(event.event == SubmenuIndexReadRAW) {
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneStart, SubmenuIndexReadRAW);
            subghz_rx_key_state_set(subghz, SubGhzRxKeyStateIDLE);
            subghz_ensure_read_raw_view(subghz, false);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneReadRAW);
            return true;
        } else if(event.event == SubmenuIndexRead) {
            subghz_rx_key_state_set(subghz, SubGhzRxKeyStateIDLE);
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneStart, SubmenuIndexRead);
            subghz_ensure_receiver_view(subghz);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneReceiver);
            return true;
        } else if(event.event == SubmenuIndexProtocolList) {
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneStart, SubmenuIndexProtocolList);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneProtocolList);
            return true;
        } else if(event.event == SubmenuIndexExtSettings) {
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneStart, SubmenuIndexExtSettings);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneExtModuleSettings);
            return true;
        }
#endif
    }
    return false;
}

void subghz_scene_start_on_exit(void* context) {
    SubGhz* subghz = context;
    submenu_reset(subghz->submenu);
}
