// scenes/rolljam_scene_receiver.c
#include "../rolljam_app_i.h"
#include "../helpers/rolljam_storage.h"
#include "views/rolljam_receiver.h"
#include <notification/notification_messages.h>
#include <stdio.h>
//#include "rolljam_standalone_icons.h"

#define TAG "RollJamSceneRx"

// Forward declaration
void rolljam_scene_receiver_view_callback(RollJamCustomEvent event, void* context);
static void rolljam_scene_receiver_start_rx_stack(RollJamApp* app);

static void rolljam_scene_receiver_update_statusbar(void* context) {
    furi_check(context);
    RollJamApp* app = context;

    char frequency_str[16] = {0};
    char modulation_str[8] = {0};
    char history_stat_str[16] = {0};

    rolljam_get_frequency_modulation_str(
        app, frequency_str, sizeof(frequency_str), modulation_str, sizeof(modulation_str));

    bool is_external = false;
    if(app->radio_initialized && app->txrx->radio_device) {
        is_external = radio_device_loader_is_external(app->txrx->radio_device);
    }

    if(app->txrx->history) {
        rolljam_history_format_status_text(
            app->txrx->history, history_stat_str, sizeof(history_stat_str));
    } else {
        snprintf(history_stat_str, sizeof(history_stat_str), "0/%u", ROLLJAM_HISTORY_MAX);
    }

    rolljam_view_receiver_add_data_statusbar(
        app->rolljam_receiver, frequency_str, modulation_str, history_stat_str, is_external);
}

static void rolljam_scene_receiver_callback(
    SubGhzReceiver* receiver,
    SubGhzProtocolDecoderBase* decoder_base,
    void* context) {
    UNUSED(receiver);
    furi_check(decoder_base);
    furi_check(context);
    RollJamApp* app = context;

    FURI_LOG_I(TAG, "=== SIGNAL DECODED (%s) ===", decoder_base->protocol->name);

    uint16_t count_before = rolljam_history_get_item(app->txrx->history);
    bool added =
        rolljam_history_add_to_history(
            app->txrx->history,
            decoder_base,
            app->txrx->preset,
            RollJamHistorySourceUnknown);

    if(added) {
        notification_message(app->notifications, &sequence_semi_success);

        FURI_LOG_I(
            TAG,
            "Added to history, total items: %u",
            rolljam_history_get_item(app->txrx->history));

        uint16_t count_after = rolljam_history_get_item(app->txrx->history);

        if(count_after > count_before) {
            rolljam_view_receiver_append_menu_row_from_history(
                app->rolljam_receiver, app->txrx->history, count_after - 1);
        }

        uint16_t last_index = rolljam_history_get_item(app->txrx->history) - 1;
        rolljam_view_receiver_set_idx_menu(app->rolljam_receiver, last_index);

        if(app->auto_save) {
            FlipperFormat* ff = rolljam_history_get_raw_data(
                app->txrx->history, rolljam_history_get_item(app->txrx->history) - 1);

            if(ff) {
                FuriString* protocol = furi_string_alloc();
                if(!protocol) {
                    FURI_LOG_E(TAG, "protocol allocation failed");
                    return;
                }

                flipper_format_rewind(ff);
                if(!flipper_format_read_string(ff, FF_PROTOCOL, protocol)) {
                    furi_string_set_str(protocol, "Unknown");
                }

                furi_string_replace_all(protocol, "/", "_");
                furi_string_replace_all(protocol, " ", "_");

                FuriString* saved_path = furi_string_alloc();
                if(!saved_path) {
                    FURI_LOG_E(TAG, "saved_path allocation failed");
                    furi_string_free(protocol);
                    return;
                }

                if(rolljam_storage_save_capture(
                       ff, furi_string_get_cstr(protocol), saved_path)) {
                    FURI_LOG_I(TAG, "Auto-saved: %s", furi_string_get_cstr(saved_path));
                    notification_message(app->notifications, &sequence_double_vibro);
                } else {
                    FURI_LOG_E(TAG, "Auto-save failed");
                }

                furi_string_free(protocol);
                furi_string_free(saved_path);
            }
        }

        view_dispatcher_send_custom_event(
            app->view_dispatcher, RollJamCustomEventSceneReceiverUpdate);
    } else {
        FURI_LOG_D(TAG, "Capture not admitted (full or duplicate)");
    }

    if(app->txrx->hopper_state == RollJamHopperStateRunning) {
        app->txrx->hopper_state = RollJamHopperStatePause;
        app->txrx->hopper_timeout = 10;
    }
}

static void rolljam_scene_receiver_start_rx_stack(RollJamApp* app) {
    furi_check(app);
    if(!app->radio_initialized) {
        return;
    }

    rolljam_rx_stack_resume_after_tx(app);
    if(!app->txrx->receiver) {
        FURI_LOG_E(TAG, "SubGhz receiver unavailable — staying on receiver in degraded mode");
        notification_message(app->notifications, &sequence_error);
        return;
    }

    if(!app->txrx->worker) {
        app->txrx->worker = subghz_worker_alloc();
        if(!app->txrx->worker) {
            FURI_LOG_E(TAG, "Failed to allocate worker — staying on receiver in degraded mode");
            notification_message(app->notifications, &sequence_error);
            return;
        }
        subghz_worker_set_overrun_callback(
            app->txrx->worker, (SubGhzWorkerOverrunCallback)subghz_receiver_reset);
        subghz_worker_set_pair_callback(
            app->txrx->worker, (SubGhzWorkerPairCallback)subghz_receiver_decode);
    }

    subghz_receiver_reset(app->txrx->receiver);

    subghz_worker_set_context(app->txrx->worker, app->txrx->receiver);
    subghz_receiver_set_rx_callback(app->txrx->receiver, rolljam_scene_receiver_callback, app);

    if(app->txrx->hopper_state != RollJamHopperStateOFF) {
        app->txrx->hopper_state = RollJamHopperStateRunning;
    }

    const char* preset_name = furi_string_get_cstr(app->txrx->preset->name);
    uint8_t* preset_data = subghz_setting_get_preset_data_by_name(app->setting, preset_name);

    if(preset_data == NULL) {
        FURI_LOG_E(TAG, "Failed to get preset data for %s, using AM650", preset_name);
        preset_data = subghz_setting_get_preset_data_by_name(app->setting, "AM650");
    }

    rolljam_begin(app, preset_data);

    uint32_t frequency = app->txrx->preset->frequency;
    if(app->txrx->hopper_state == RollJamHopperStateRunning) {
        frequency = subghz_setting_get_hopper_frequency(app->setting, 0);
        app->txrx->hopper_idx_frequency = 0;
    }

    FURI_LOG_I(TAG, "Starting RX on %lu Hz", frequency);
    rolljam_rx(app, frequency);

    FURI_LOG_I(TAG, "RX started, state: %d", app->txrx->txrx_state);
}

void rolljam_scene_receiver_on_enter(void* context) {
    furi_check(context);
    RollJamApp* app = context;

    if(!rolljam_ensure_receiver_view(app)) {
        notification_message(app->notifications, &sequence_error);
        scene_manager_previous_scene(app->scene_manager);
        return;
    }

    if(app->txrx->history) {
        rolljam_history_release_scratch(app->txrx->history);
    }

    if(!app->radio_initialized && !rolljam_radio_init(app)) {
        FURI_LOG_E(TAG, "Failed to initialize radio for receiver scene");
        notification_message(app->notifications, &sequence_error);
        scene_manager_previous_scene(app->scene_manager);
        return;
    }

    if(!app->txrx->history) {
        app->txrx->history = rolljam_history_alloc();
        if(!app->txrx->history) {
            FURI_LOG_E(TAG, "Failed to allocate history!");
            return;
        }
    }

    rolljam_view_receiver_set_history_mutex(app->rolljam_receiver, NULL);
    rolljam_view_receiver_sync_menu_from_history(
        app->rolljam_receiver, app->txrx->history);

    rolljam_view_receiver_set_callback(
        app->rolljam_receiver, rolljam_scene_receiver_view_callback, app);

    rolljam_view_receiver_set_lock(app->rolljam_receiver, app->lock);
    rolljam_view_receiver_set_autosave(app->rolljam_receiver, app->auto_save);
    rolljam_view_receiver_set_sub_decode_mode(app->rolljam_receiver, false);

    rolljam_scene_receiver_update_statusbar(app);

#ifndef REMOVE_LOGS
    bool is_external =
        app->txrx->radio_device ? radio_device_loader_is_external(app->txrx->radio_device) : false;
    const char* device_name =
        app->txrx->radio_device ? subghz_devices_get_name(app->txrx->radio_device) : NULL;
    FURI_LOG_I(TAG, "=== ENTERING RECEIVER SCENE ===");
    FURI_LOG_I(TAG, "Radio device: %s", device_name ? device_name : "NULL");
    FURI_LOG_I(TAG, "Is External: %s", is_external ? "YES" : "NO");
    FURI_LOG_I(TAG, "Frequency: %lu Hz", app->txrx->preset->frequency);
    FURI_LOG_I(TAG, "Modulation: %s", furi_string_get_cstr(app->txrx->preset->name));
    FURI_LOG_I(TAG, "Auto-save: %s", app->auto_save ? "ON" : "OFF");
#endif

    view_dispatcher_switch_to_view(app->view_dispatcher, RollJamViewReceiver);
    view_dispatcher_send_custom_event(
        app->view_dispatcher, RollJamCustomEventReceiverDeferredRxStart);
}

static void rolljam_scene_receiver_handle_back(RollJamApp* app) {
    if(app->txrx->history && rolljam_history_get_item(app->txrx->history) > 0 &&
       !app->auto_save) {
        app->unsaved_history_owner = RollJamCaptureOwnerReceiver;
        scene_manager_set_scene_state(app->scene_manager, RollJamSceneReceiver, 1);
        scene_manager_next_scene(app->scene_manager, RollJamSceneNeedSaving);
    } else {
        app->unsaved_history_owner = RollJamCaptureOwnerNone;
        scene_manager_search_and_switch_to_previous_scene(
            app->scene_manager, RollJamSceneStart);
    }
}

bool rolljam_scene_receiver_on_event(void* context, SceneManagerEvent event) {
    furi_check(context);
    RollJamApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        switch(event.event) {
        case RollJamCustomEventReceiverDeferredRxStart:
#ifndef REMOVE_LOGS
            FURI_LOG_I(TAG, "Deferred RX start (post-emulate path)");
#endif
            rolljam_scene_receiver_start_rx_stack(app);
            rolljam_scene_receiver_update_statusbar(app);
            consumed = true;
            break;

        case RollJamCustomEventSceneReceiverUpdate:
            rolljam_scene_receiver_update_statusbar(app);
            consumed = true;
            break;

        case RollJamCustomEventViewReceiverOK: {
            uint16_t idx = rolljam_view_receiver_get_idx_menu(app->rolljam_receiver);
            FURI_LOG_I(TAG, "Selected item %d", idx);
            if(idx < rolljam_history_get_item(app->txrx->history)) {
                app->txrx->idx_menu_chosen = idx;
                rolljam_selected_capture_set(
                    app,
                    app->txrx->history,
                    NULL,
                    idx,
                    RollJamCaptureOwnerReceiver);
                scene_manager_set_scene_state(app->scene_manager, RollJamSceneReceiver, 1);
                scene_manager_next_scene(app->scene_manager, RollJamSceneReceiverInfo);
            }
        }
            consumed = true;
            break;

        case RollJamCustomEventViewReceiverDeleteItem: {
            uint16_t idx = rolljam_view_receiver_get_idx_menu(app->rolljam_receiver);
            if(idx < rolljam_history_get_item(app->txrx->history)) {
                if(app->loaded_file_path &&
                   rolljam_history_capture_path_equals(
                       app->txrx->history, idx, furi_string_get_cstr(app->loaded_file_path))) {
                    furi_string_free(app->loaded_file_path);
                    app->loaded_file_path = NULL;
                }
                rolljam_history_delete_item(app->txrx->history, idx);
                rolljam_view_receiver_delete_item(app->rolljam_receiver, idx);

                uint16_t count_after =
                    app->txrx->history ? rolljam_history_get_item(app->txrx->history) : 0;
                if(count_after == 0) {
                    rolljam_view_receiver_sync_menu_from_history(
                        app->rolljam_receiver, app->txrx->history);
                    rolljam_view_receiver_set_idx_menu(app->rolljam_receiver, 0);
                }
                rolljam_scene_receiver_update_statusbar(app);
                app->txrx->idx_menu_chosen =
                    rolljam_view_receiver_get_idx_menu(app->rolljam_receiver);
            }
            consumed = true;
            break;
        }

        case RollJamCustomEventViewReceiverConfig:
            scene_manager_set_scene_state(app->scene_manager, RollJamSceneReceiver, 1);
            scene_manager_next_scene(app->scene_manager, RollJamSceneReceiverConfig);
            consumed = true;
            break;

        case RollJamCustomEventViewReceiverBack:
            rolljam_scene_receiver_handle_back(app);
            consumed = true;
            break;

        case RollJamCustomEventViewReceiverUnlock:
            app->lock = RollJamLockOff;
            rolljam_view_receiver_set_lock(app->rolljam_receiver, app->lock);
            consumed = true;
            break;
        }
    } else if(event.type == SceneManagerEventTypeTick) {
        if(app->txrx->hopper_state != RollJamHopperStateOFF) {
            rolljam_hopper_update(app);
            static uint8_t hopper_statusbar_tick = 0;
            if(++hopper_statusbar_tick >= 8) {
                hopper_statusbar_tick = 0;
                rolljam_scene_receiver_update_statusbar(app);
            }
        }

        if(app->radio_initialized && app->txrx->txrx_state == RollJamTxRxStateRx &&
           app->txrx->radio_device) {
            float rssi = subghz_devices_get_rssi(app->txrx->radio_device);
            rolljam_view_receiver_set_rssi(app->rolljam_receiver, rssi);

            static uint8_t rssi_log_counter = 0;
            if(++rssi_log_counter >= 50) {
#ifndef REMOVE_LOGS
                bool is_external = app->txrx->radio_device ?
                                       radio_device_loader_is_external(app->txrx->radio_device) :
                                       false;
                FURI_LOG_D(TAG, "RSSI: %.1f dBm (%s)", (double)rssi, is_external ? "EXT" : "INT");
#endif
                rssi_log_counter = 0;
            }

            notification_message(app->notifications, &sequence_blink_cyan_10);
        }

        consumed = true;
    }

    return consumed;
}

void rolljam_scene_receiver_on_exit(void* context) {
    furi_check(context);
    RollJamApp* app = context;

    FURI_LOG_I(TAG, "=== EXITING RECEIVER SCENE ===");

    const bool leaving_for_subscene =
        (scene_manager_get_scene_state(app->scene_manager, RollJamSceneReceiver) == 1);

    if(app->radio_initialized && app->txrx->txrx_state == RollJamTxRxStateRx) {
        rolljam_rx_end(app);
    }

    if(leaving_for_subscene) {
        scene_manager_set_scene_state(app->scene_manager, RollJamSceneReceiver, 0);
        return;
    }

    rolljam_view_receiver_reset_menu(app->rolljam_receiver);
    rolljam_radio_deinit(app);
}

void rolljam_scene_receiver_view_callback(RollJamCustomEvent event, void* context) {
    furi_check(context);
    RollJamApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, event);
}
