#include "../subghz_i.h"
#include "subghz_scene_start.h"
#include <dolphin/dolphin.h>

#include <lib/subghz/protocols/raw.h>

#ifdef ARF_EXTERNAL_FULL
#include <loader/loader.h>

#define ARF_KEELOQ_PATH    EXT_PATH("apps/ARF Tools/arf_keeloq.fap")
#define ARF_COUNTER_PATH   EXT_PATH("apps/ARF Tools/arf_counter_bf.fap")
#define ARF_CAR_PATH       EXT_PATH("apps/ARF Tools/arf_car_emulate.fap")
#define ARF_PSA_PATH       EXT_PATH("apps/ARF Tools/arf_psa_decrypt.fap")
#define ARF_ANALYZER_PATH  EXT_PATH("apps/ARF Tools/arf_frequency_analyzer.fap")
#define PROTO_PIRATE_PATH  EXT_PATH("apps/ARF Tools/proto_pirate.fap")
#define ROLLJAM_PATH       EXT_PATH("apps/ARF Tools/rolljam.fap")
#define SUBBRUTE_PATH      EXT_PATH("apps/ARF Tools/subghz_bruteforcer.fap")
#define ARF_STATUS_PATH    EXT_PATH("apps/ARF Tools/arf_status.fap")

static void subghz_scene_start_launch_tool(
    SubGhz* subghz,
    uint32_t menu_index,
    const char* path) {
    Loader* loader = furi_record_open(RECORD_LOADER);
    FuriString* self_path = furi_string_alloc();

    scene_manager_set_scene_state(subghz->scene_manager, SubGhzSceneStart, menu_index);
    loader_enqueue_launch(loader, path, NULL, LoaderDeferredLaunchFlagGui);
    if(loader_get_application_launch_path(loader, self_path)) {
        loader_enqueue_launch(
            loader, furi_string_get_cstr(self_path), NULL, LoaderDeferredLaunchFlagGui);
    }

    furi_string_free(self_path);
    furi_record_close(RECORD_LOADER);
    scene_manager_stop(subghz->scene_manager);
    view_dispatcher_stop(subghz->view_dispatcher);
}
#endif

void subghz_scene_start_submenu_callback(void* context, uint32_t index) {
    SubGhz* subghz = context;
    view_dispatcher_send_custom_event(subghz->view_dispatcher, index);
}

void subghz_scene_start_on_enter(void* context) {
    SubGhz* subghz = context;
    if(subghz->state_notifications == SubGhzNotificationStateStarting) {
        subghz->state_notifications = SubGhzNotificationStateIDLE;
    }

    submenu_add_item(
        subghz->submenu, "Read", SubmenuIndexRead, subghz_scene_start_submenu_callback, subghz);
    submenu_add_item(
        subghz->submenu,
        "Read RAW",
        SubmenuIndexReadRAW,
        subghz_scene_start_submenu_callback,
        subghz);
    submenu_add_item(
        subghz->submenu, "Saved", SubmenuIndexSaved, subghz_scene_start_submenu_callback, subghz);
    submenu_add_item(
        subghz->submenu,
        "Add Manually",
        SubmenuIndexAddManually,
        subghz_scene_start_submenu_callback,
        subghz);
    submenu_add_item(
        subghz->submenu,
        "Add Manually [Advanced]",
        SubmenuIndexAddManuallyAdvanced,
        subghz_scene_start_submenu_callback,
        subghz);
    submenu_add_item(
        subghz->submenu,
        "Frequency Analyzer",
        SubmenuIndexFrequencyAnalyzer,
        subghz_scene_start_submenu_callback,
        subghz);
    submenu_add_item(
        subghz->submenu,
        "Radio Settings",
        SubmenuIndexExtSettings,
        subghz_scene_start_submenu_callback,
        subghz);
#ifdef ARF_EXTERNAL_FULL
    submenu_add_item(
        subghz->submenu,
        "ARF KeeLoq",
        SubmenuIndexArfKeeloq,
        subghz_scene_start_submenu_callback,
        subghz);
    submenu_add_item(
        subghz->submenu,
        "ARF Counter BF",
        SubmenuIndexArfCounter,
        subghz_scene_start_submenu_callback,
        subghz);
    submenu_add_item(
        subghz->submenu,
        "ARF Car Emulate",
        SubmenuIndexArfCar,
        subghz_scene_start_submenu_callback,
        subghz);
    submenu_add_item(
        subghz->submenu,
        "ARF PSA Decrypt",
        SubmenuIndexArfPsa,
        subghz_scene_start_submenu_callback,
        subghz);
    submenu_add_item(
        subghz->submenu,
        "ARF Analyzer",
        SubmenuIndexArfAnalyzer,
        subghz_scene_start_submenu_callback,
        subghz);
    submenu_add_item(
        subghz->submenu,
        "ProtoPirate",
        SubmenuIndexProtoPirate,
        subghz_scene_start_submenu_callback,
        subghz);
    submenu_add_item(
        subghz->submenu,
        "RollJam",
        SubmenuIndexRollJam,
        subghz_scene_start_submenu_callback,
        subghz);
    submenu_add_item(
        subghz->submenu,
        "Sub-GHz Bruteforcer",
        SubmenuIndexSubBrute,
        subghz_scene_start_submenu_callback,
        subghz);
    submenu_add_item(
        subghz->submenu,
        "ARF Status",
        SubmenuIndexArfStatus,
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
#ifdef ARF_EXTERNAL_FULL
        // Only one top-level Sub-GHz tool owns RAM at a time in the external build.
        subghz_external_release_tool_views(subghz);
        if(event.event == SubmenuIndexArfKeeloq) {
            subghz_scene_start_launch_tool(subghz, event.event, ARF_KEELOQ_PATH);
            return true;
        } else if(event.event == SubmenuIndexArfCounter) {
            subghz_scene_start_launch_tool(subghz, event.event, ARF_COUNTER_PATH);
            return true;
        } else if(event.event == SubmenuIndexArfCar) {
            subghz_scene_start_launch_tool(subghz, event.event, ARF_CAR_PATH);
            return true;
        } else if(event.event == SubmenuIndexArfPsa) {
            subghz_scene_start_launch_tool(subghz, event.event, ARF_PSA_PATH);
            return true;
        } else if(event.event == SubmenuIndexArfAnalyzer) {
            subghz_scene_start_launch_tool(subghz, event.event, ARF_ANALYZER_PATH);
            return true;
        } else if(event.event == SubmenuIndexProtoPirate) {
            subghz_scene_start_launch_tool(subghz, event.event, PROTO_PIRATE_PATH);
            return true;
        } else if(event.event == SubmenuIndexRollJam) {
            subghz_scene_start_launch_tool(subghz, event.event, ROLLJAM_PATH);
            return true;
        } else if(event.event == SubmenuIndexSubBrute) {
            subghz_scene_start_launch_tool(subghz, event.event, SUBBRUTE_PATH);
            return true;
        } else if(event.event == SubmenuIndexArfStatus) {
            subghz_scene_start_launch_tool(subghz, event.event, ARF_STATUS_PATH);
            return true;
        }
#endif
        if(event.event == SubmenuIndexReadRAW) {
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneStart, SubmenuIndexReadRAW);
            subghz_rx_key_state_set(subghz, SubGhzRxKeyStateIDLE);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneReadRAW);
            return true;
        } else if(event.event == SubmenuIndexRead) {
            subghz_rx_key_state_set(subghz, SubGhzRxKeyStateIDLE);
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneStart, SubmenuIndexRead);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneReceiver);
            return true;
        } else if(event.event == SubmenuIndexSaved) {
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneStart, SubmenuIndexSaved);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneSaved);
            return true;
        } else if(event.event == SubmenuIndexAddManually) {
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneStart, SubmenuIndexAddManually);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneSetType);
            return true;
        } else if(event.event == SubmenuIndexAddManuallyAdvanced) {
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneStart, SubmenuIndexAddManuallyAdvanced);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneSetType);
            return true;
        } else if(event.event == SubmenuIndexFrequencyAnalyzer) {
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneStart, SubmenuIndexFrequencyAnalyzer);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneFrequencyAnalyzer);
            dolphin_deed(DolphinDeedSubGhzFrequencyAnalyzer);
            return true;
        } else if(event.event == SubmenuIndexExtSettings) {
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneStart, SubmenuIndexExtSettings);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneExtModuleSettings);
            return true;
        }
    }
    return false;
}

void subghz_scene_start_on_exit(void* context) {
    SubGhz* subghz = context;
    submenu_reset(subghz->submenu);
}
