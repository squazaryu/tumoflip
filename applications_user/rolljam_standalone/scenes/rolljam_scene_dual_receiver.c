// scenes/rolljam_scene_dual_receiver.c
#include "../rolljam_app_i.h"

#ifdef ENABLE_DUAL_RX_SCENE

#include "../helpers/rolljam_storage.h"
#include <notification/notification_messages.h>
#include <stdio.h>

#define TAG "RollJamSceneDualRx"

#define DUAL_SCENE_STATE_NONE        0u
#define DUAL_SCENE_STATE_TO_SUBSCENE 1u

typedef struct {
    RollJamApp* app;
    RollJamRxChain* chain;
} DualDecodeBinding;

static DualDecodeBinding s_bind_a;
static DualDecodeBinding s_bind_b;
static bool s_dual_devices_inited = false;

void rolljam_scene_dual_receiver_view_callback(RollJamCustomEvent event, void* context);

static void rolljam_scene_dual_receiver_fmt_freq(uint32_t freq, char* out, size_t out_size) {
    snprintf(
        out,
        out_size,
        "%03lu.%02lu",
        (unsigned long)((freq / 1000000UL) % 1000UL),
        (unsigned long)((freq / 10000UL) % 100UL));
}

static void rolljam_scene_dual_receiver_decode_cb(
    SubGhzReceiver* receiver,
    SubGhzProtocolDecoderBase* decoder_base,
    void* context) {
    UNUSED(receiver);
    furi_check(decoder_base);
    furi_check(context);
    DualDecodeBinding* bind = context;
    RollJamApp* app = bind->app;

    if(!app->dual_history || !app->dual_history_mutex) {
        return;
    }

    furi_mutex_acquire(app->dual_history_mutex, FuriWaitForever);
    RollJamHistorySource source = bind->chain->is_external ?
                                         RollJamHistorySourceExternal :
                                         RollJamHistorySourceInternal;
    bool added = rolljam_history_add_to_history(
        app->dual_history, decoder_base, &bind->chain->preset, source);
    furi_mutex_release(app->dual_history_mutex);

    if(added) {
        notification_message(app->notifications, &sequence_semi_success);
        view_dispatcher_send_custom_event(
            app->view_dispatcher, RollJamCustomEventDualReceiverUpdate);
    }
}

static void rolljam_scene_dual_receiver_update_status(RollJamApp* app) {
    furi_check(app);
    if(!app->dual_receiver) {
        return;
    }

    char freq_str[16];
    if(app->dual_chain_a) {
        rolljam_scene_dual_receiver_fmt_freq(
            app->dual_chain_a->frequency, freq_str, sizeof(freq_str));
        rolljam_view_dual_receiver_set_chain_status(
            app->dual_receiver,
            0,
            "A",
            freq_str,
            furi_string_get_cstr(app->dual_chain_a->preset.name),
            app->dual_chain_a->is_external);
    }
    if(app->dual_chain_b) {
        rolljam_scene_dual_receiver_fmt_freq(
            app->dual_chain_b->frequency, freq_str, sizeof(freq_str));
        rolljam_view_dual_receiver_set_chain_status(
            app->dual_receiver,
            1,
            "B",
            freq_str,
            furi_string_get_cstr(app->dual_chain_b->preset.name),
            app->dual_chain_b->is_external);
    }

    char hist_str[16] = {0};
    if(app->dual_history) {
        rolljam_history_format_status_text(app->dual_history, hist_str, sizeof(hist_str));
    } else {
        snprintf(hist_str, sizeof(hist_str), "0/%u", ROLLJAM_HISTORY_MAX);
    }
    rolljam_view_dual_receiver_set_history_stat(app->dual_receiver, hist_str);
}

static void rolljam_scene_dual_receiver_teardown(RollJamApp* app) {
    furi_check(app);

    if(app->dual_chain_a) {
        rolljam_rx_chain_free(app->dual_chain_a);
        app->dual_chain_a = NULL;
    }
    if(app->dual_chain_b) {
        rolljam_rx_chain_free(app->dual_chain_b);
        app->dual_chain_b = NULL;
    }

    s_bind_a.chain = NULL;
    s_bind_b.chain = NULL;

    if(s_dual_devices_inited) {
        subghz_devices_deinit();
        s_dual_devices_inited = false;
    }
}

static bool rolljam_scene_dual_receiver_build(RollJamApp* app) {
    furi_check(app);

    rolljam_radio_deinit(app);

    if(!app->dual_history) {
        app->dual_history = rolljam_history_alloc();
        if(!app->dual_history) {
            FURI_LOG_E(TAG, "Failed to allocate dual history");
            return false;
        }
    }
    if(!app->dual_history_mutex) {
        app->dual_history_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
        if(!app->dual_history_mutex) {
            FURI_LOG_E(TAG, "Failed to allocate history mutex");
            return false;
        }
    }

    subghz_devices_init();
    s_dual_devices_inited = true;

    app->dual_chain_a = rolljam_rx_chain_alloc('A');
    app->dual_chain_b = rolljam_rx_chain_alloc('B');
    if(!app->dual_chain_a || !app->dual_chain_b) {
        FURI_LOG_E(TAG, "Failed to allocate chains");
        rolljam_scene_dual_receiver_teardown(app);
        return false;
    }

    if(!rolljam_rx_chain_acquire_device(
           app->dual_chain_a, SubGhzRadioDeviceTypeExternalCC1101)) {
        FURI_LOG_E(TAG, "External CC1101 unavailable - dual RX requires it");
        rolljam_scene_dual_receiver_teardown(app);
        return false;
    }
    if(!rolljam_rx_chain_acquire_device(app->dual_chain_b, SubGhzRadioDeviceTypeInternal)) {
        FURI_LOG_E(TAG, "Internal CC1101 unavailable");
        rolljam_scene_dual_receiver_teardown(app);
        return false;
    }

    size_t preset_count = subghz_setting_get_preset_count(app->setting);
    if(app->dual_preset_a >= preset_count || app->dual_preset_b >= preset_count) {
        FURI_LOG_E(TAG, "Dual RX requires valid presets");
        rolljam_scene_dual_receiver_teardown(app);
        return false;
    }

    if(!rolljam_rx_chain_set_preset_data(
           app->dual_chain_a,
           subghz_setting_get_preset_name(app->setting, app->dual_preset_a),
           subghz_setting_get_preset_data(app->setting, app->dual_preset_a),
           subghz_setting_get_preset_data_size(app->setting, app->dual_preset_a),
           app->dual_freq_a) ||
       !rolljam_rx_chain_set_preset_data(
           app->dual_chain_b,
           subghz_setting_get_preset_name(app->setting, app->dual_preset_b),
           subghz_setting_get_preset_data(app->setting, app->dual_preset_b),
           subghz_setting_get_preset_data_size(app->setting, app->dual_preset_b),
           app->dual_freq_b)) {
        FURI_LOG_E(TAG, "Failed to set chain presets");
        rolljam_scene_dual_receiver_teardown(app);
        return false;
    }

    if(!rolljam_rx_chain_init_receiver(app->dual_chain_a) ||
       !rolljam_rx_chain_init_receiver(app->dual_chain_b)) {
        FURI_LOG_E(TAG, "Failed to init chain receivers");
        rolljam_scene_dual_receiver_teardown(app);
        return false;
    }

    s_bind_a.app = app;
    s_bind_a.chain = app->dual_chain_a;
    s_bind_b.app = app;
    s_bind_b.chain = app->dual_chain_b;

    rolljam_rx_chain_set_decode_callback(
        app->dual_chain_a, rolljam_scene_dual_receiver_decode_cb, &s_bind_a);
    rolljam_rx_chain_set_decode_callback(
        app->dual_chain_b, rolljam_scene_dual_receiver_decode_cb, &s_bind_b);

    return true;
}

void rolljam_scene_dual_receiver_on_enter(void* context) {
    furi_check(context);
    RollJamApp* app = context;

    if(!rolljam_ensure_dual_receiver_view(app)) {
        notification_message(app->notifications, &sequence_error);
        scene_manager_search_and_switch_to_previous_scene(app->scene_manager, RollJamSceneStart);
        return;
    }

    if(!rolljam_scene_dual_receiver_build(app)) {
        notification_message(app->notifications, &sequence_error);
        scene_manager_search_and_switch_to_previous_scene(app->scene_manager, RollJamSceneStart);
        return;
    }

    rolljam_view_dual_receiver_set_callback(
        app->dual_receiver, rolljam_scene_dual_receiver_view_callback, app);
    rolljam_view_dual_receiver_set_history_mutex(app->dual_receiver, app->dual_history_mutex);
    rolljam_view_dual_receiver_set_history(app->dual_receiver, app->dual_history);
    rolljam_view_dual_receiver_sync_menu_from_history(app->dual_receiver, app->dual_history);
    if(app->selected_capture.owner == RollJamCaptureOwnerDualReceiver &&
       app->selected_capture.history == app->dual_history &&
       rolljam_selected_capture_is_valid(app)) {
        rolljam_view_dual_receiver_set_idx_menu(
            app->dual_receiver, app->selected_capture.index);
    }
    rolljam_scene_dual_receiver_update_status(app);

    scene_manager_set_scene_state(
        app->scene_manager, RollJamSceneDualReceiver, DUAL_SCENE_STATE_NONE);

    view_dispatcher_switch_to_view(app->view_dispatcher, RollJamViewDualReceiver);
    view_dispatcher_send_custom_event(
        app->view_dispatcher, RollJamCustomEventDualReceiverDeferredRxStart);
}

bool rolljam_scene_dual_receiver_on_event(void* context, SceneManagerEvent event) {
    furi_check(context);
    RollJamApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        switch(event.event) {
        case RollJamCustomEventDualReceiverDeferredRxStart:
            if(!rolljam_rx_chain_start(app->dual_chain_a) ||
               !rolljam_rx_chain_start(app->dual_chain_b)) {
                FURI_LOG_E(TAG, "Failed to start one or both chains");
                rolljam_scene_dual_receiver_teardown(app);
                notification_message(app->notifications, &sequence_error);
                scene_manager_set_scene_state(
                    app->scene_manager, RollJamSceneDualReceiver, DUAL_SCENE_STATE_NONE);
                scene_manager_search_and_switch_to_previous_scene(
                    app->scene_manager, RollJamSceneStart);
                consumed = true;
                break;
            }
            rolljam_scene_dual_receiver_update_status(app);
            consumed = true;
            break;

        case RollJamCustomEventDualReceiverUpdate:
            rolljam_view_dual_receiver_sync_menu_from_history(
                app->dual_receiver, app->dual_history);
            rolljam_scene_dual_receiver_update_status(app);
            consumed = true;
            break;

        case RollJamCustomEventViewDualReceiverConfig:
            scene_manager_set_scene_state(
                app->scene_manager, RollJamSceneDualReceiver, DUAL_SCENE_STATE_TO_SUBSCENE);
            scene_manager_next_scene(app->scene_manager, RollJamSceneDualReceiverConfig);
            consumed = true;
            break;

        case RollJamCustomEventViewDualReceiverDeleteItem: {
            uint16_t idx = rolljam_view_dual_receiver_get_idx_menu(app->dual_receiver);
            furi_mutex_acquire(app->dual_history_mutex, FuriWaitForever);
            bool valid = idx < rolljam_history_get_item(app->dual_history);
            if(valid) {
                rolljam_history_delete_item(app->dual_history, idx);
            }
            furi_mutex_release(app->dual_history_mutex);
            if(valid) {
                rolljam_view_dual_receiver_delete_item(app->dual_receiver, idx);
                rolljam_scene_dual_receiver_update_status(app);
            }
            consumed = true;
            break;
        }

        case RollJamCustomEventViewDualReceiverOK:
            {
                uint16_t idx =
                    rolljam_view_dual_receiver_get_idx_menu(app->dual_receiver);
                furi_mutex_acquire(app->dual_history_mutex, FuriWaitForever);
                bool valid = idx < rolljam_history_get_item(app->dual_history);
                furi_mutex_release(app->dual_history_mutex);
                if(valid) {
                    rolljam_selected_capture_set(
                        app,
                        app->dual_history,
                        app->dual_history_mutex,
                        idx,
                        RollJamCaptureOwnerDualReceiver);
                    scene_manager_set_scene_state(
                        app->scene_manager,
                        RollJamSceneDualReceiver,
                        DUAL_SCENE_STATE_TO_SUBSCENE);
                    scene_manager_next_scene(
                        app->scene_manager, RollJamSceneReceiverInfo);
                }
            }
            consumed = true;
            break;

        case RollJamCustomEventViewDualReceiverBack:
            furi_mutex_acquire(app->dual_history_mutex, FuriWaitForever);
            bool has_history = rolljam_history_get_item(app->dual_history) > 0;
            furi_mutex_release(app->dual_history_mutex);
            if(has_history) {
                app->unsaved_history_owner = RollJamCaptureOwnerDualReceiver;
                scene_manager_set_scene_state(
                    app->scene_manager,
                    RollJamSceneDualReceiver,
                    DUAL_SCENE_STATE_TO_SUBSCENE);
                scene_manager_next_scene(app->scene_manager, RollJamSceneNeedSaving);
            } else {
                app->unsaved_history_owner = RollJamCaptureOwnerNone;
                scene_manager_search_and_switch_to_previous_scene(
                    app->scene_manager, RollJamSceneStart);
            }
            consumed = true;
            break;

        default:
            break;
        }
    } else if(event.type == SceneManagerEventTypeTick) {
        if(app->dual_chain_a) {
            rolljam_view_dual_receiver_set_rssi(
                app->dual_receiver, 0, rolljam_rx_chain_get_rssi(app->dual_chain_a));
        }
        if(app->dual_chain_b) {
            rolljam_view_dual_receiver_set_rssi(
                app->dual_receiver, 1, rolljam_rx_chain_get_rssi(app->dual_chain_b));
        }
        consumed = true;
    }

    return consumed;
}

void rolljam_scene_dual_receiver_on_exit(void* context) {
    furi_check(context);
    RollJamApp* app = context;

    const bool leaving_for_subscene =
        (scene_manager_get_scene_state(app->scene_manager, RollJamSceneDualReceiver) ==
         DUAL_SCENE_STATE_TO_SUBSCENE);

    rolljam_scene_dual_receiver_teardown(app);

    if(leaving_for_subscene) {
        return;
    }

    if(app->dual_receiver) {
        rolljam_view_dual_receiver_reset_menu(app->dual_receiver);
    }
    if(app->selected_capture.owner == RollJamCaptureOwnerDualReceiver) {
        rolljam_selected_capture_clear(app);
    }
}

void rolljam_scene_dual_receiver_view_callback(RollJamCustomEvent event, void* context) {
    furi_check(context);
    RollJamApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, event);
}

#else

#endif // ENABLE_DUAL_RX_SCENE
