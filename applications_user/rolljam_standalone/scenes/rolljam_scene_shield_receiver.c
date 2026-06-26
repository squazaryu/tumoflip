// scenes/rolljam_scene_shield_receiver.c
#include "../rolljam_app_i.h"

#ifdef ENABLE_SHIELD_RX_SCENE

#include "../helpers/rolljam_storage.h"
#include "views/rolljam_receiver.h"
#include <notification/notification_messages.h>
#include <stdio.h>

#define TAG "RollJamSceneShieldRx"

#define SHIELD_SCENE_STATE_NONE        0u
#define SHIELD_SCENE_STATE_TO_SUBSCENE 1u

#define SHIELD_ROLLJAM_START_DELAY_MS 300U

#define SHIELD_TX_OFFSET_COUNT 12U
static const int32_t shield_tx_offset_hz[SHIELD_TX_OFFSET_COUNT] = {
    75000L,
    100000L,
    150000L,
    200000L,
    250000L,
    300000L,
    -75000L,
    -100000L,
    -150000L,
    -200000L,
    -250000L,
    -300000L,
};

static bool s_shield_devices_inited = false;
static FuriThread* s_shield_rolljam_tx_thread = NULL;
static bool s_shield_rolljam_tx_thread_done = false;
static uint16_t s_shield_rolljam_last_trigger_count = 0;

typedef struct {
    RollJamApp* app;
    uint32_t tx_hold_ms;
} ShieldRollJamTxRequest;

void rolljam_scene_shield_receiver_view_callback(RollJamCustomEvent event, void* context);

void rolljam_scene_shield_receiver_cleanup_rolljam_tx(void) {
    if(s_shield_rolljam_tx_thread) {
        furi_thread_join(s_shield_rolljam_tx_thread);
        furi_thread_free(s_shield_rolljam_tx_thread);
        s_shield_rolljam_tx_thread = NULL;
    }
    s_shield_rolljam_tx_thread_done = false;
}

static uint32_t rolljam_scene_shield_receiver_min_tx_time(const char* protocol) {
    if(!protocol) return 666U;
    if(strcmp(protocol, "Kia V3/V4") == 0 || strcmp(protocol, "Kia V3") == 0 ||
       strcmp(protocol, "Kia V4") == 0 || strcmp(protocol, "KIA/HYU V3") == 0 ||
       strcmp(protocol, "KIA/HYU V4") == 0) {
        return 1600U;
    }
    return 666U;
}

static bool rolljam_scene_shield_receiver_capture_fields(
    RollJamHistory* history,
    uint16_t idx,
    FuriString* protocol_out,
    uint32_t* serial_out,
    uint32_t* cnt_out) {
    furi_check(history);
    furi_check(protocol_out);

    FlipperFormat* ff = rolljam_history_get_raw_data(history, idx);
    if(!ff) {
        return false;
    }

    bool ok = false;
    uint32_t serial = 0;
    uint32_t cnt = 0;
    FuriString* protocol = furi_string_alloc();
    if(!protocol) {
        return false;
    }

    flipper_format_rewind(ff);
    if(!flipper_format_read_string(ff, FF_PROTOCOL, protocol)) {
        goto done;
    }

    flipper_format_rewind(ff);
    if(!flipper_format_read_uint32(ff, FF_SERIAL, &serial, 1)) {
        goto done;
    }

    flipper_format_rewind(ff);
    if(!flipper_format_read_uint32(ff, FF_CNT, &cnt, 1)) {
        goto done;
    }

    furi_string_set(protocol_out, protocol);
    if(serial_out) *serial_out = serial;
    if(cnt_out) *cnt_out = cnt;
    ok = true;

done:
    furi_string_free(protocol);
    return ok;
}

static bool rolljam_scene_shield_receiver_pair_matches(
    RollJamHistory* history,
    uint16_t first_idx,
    uint16_t second_idx,
    FuriString* protocol_out,
    uint32_t* tx_hold_ms) {
    furi_check(history);
    furi_check(protocol_out);

    FuriString* proto_first = furi_string_alloc();
    FuriString* proto_second = furi_string_alloc();
    if(!proto_first || !proto_second) {
        if(proto_first) furi_string_free(proto_first);
        if(proto_second) furi_string_free(proto_second);
        return false;
    }

    uint32_t serial_first = 0;
    uint32_t serial_second = 0;
    uint32_t cnt_first = 0;
    uint32_t cnt_second = 0;
    bool ok_first = rolljam_scene_shield_receiver_capture_fields(
        history, first_idx, proto_first, &serial_first, &cnt_first);
    bool ok_second = rolljam_scene_shield_receiver_capture_fields(
        history, second_idx, proto_second, &serial_second, &cnt_second);
    if(!ok_first || !ok_second) {
        furi_string_free(proto_first);
        furi_string_free(proto_second);
        return false;
    }

    bool match = (serial_first == serial_second) && (cnt_first != cnt_second) &&
                 furi_string_equal(proto_first, proto_second);
    if(match) {
        furi_string_set(protocol_out, proto_first);
        if(tx_hold_ms) {
            *tx_hold_ms = rolljam_scene_shield_receiver_min_tx_time(
                furi_string_get_cstr(proto_first));
        }
    }

    furi_string_free(proto_first);
    furi_string_free(proto_second);
    return match;
}

static int32_t rolljam_scene_shield_receiver_rolljam_thread_entry(void* context) {
    ShieldRollJamTxRequest* request = context;
    if(!request || !request->app) {
        if(request) free(request);
        return 0;
    }

    RollJamApp* app = request->app;

    furi_delay_ms(SHIELD_ROLLJAM_START_DELAY_MS);
    if(!app->loaded_file_path || furi_string_empty(app->loaded_file_path)) {
        goto done;
    }

    view_dispatcher_send_custom_event(
        app->view_dispatcher, RollJamCustomEventEmulateTransmit);
    furi_delay_ms(request->tx_hold_ms);
    view_dispatcher_send_custom_event(app->view_dispatcher, RollJamCustomEventEmulateStop);
    furi_delay_ms(120);
    view_dispatcher_send_custom_event(app->view_dispatcher, RollJamCustomEventEmulateExit);

done:
    s_shield_rolljam_tx_thread_done = true;
    free(request);
    return 0;
}

static bool rolljam_scene_shield_receiver_maybe_trigger_rolljam(RollJamApp* app) {
    furi_check(app);

    if(s_shield_rolljam_tx_thread) {
        return false;
    }
    if(!app->shield_history || !app->shield_history_mutex) {
        return false;
    }

    uint16_t item_count = 0;
    FuriString* first_path = NULL;
    FuriString* protocol = NULL;
    uint32_t tx_hold_ms = 0;
    bool should_trigger = false;

    protocol = furi_string_alloc();
    first_path = furi_string_alloc();
    if(!protocol || !first_path) {
        if(protocol) furi_string_free(protocol);
        if(first_path) furi_string_free(first_path);
        return false;
    }

    furi_mutex_acquire(app->shield_history_mutex, FuriWaitForever);
    item_count = rolljam_history_get_item(app->shield_history);
    if(item_count < s_shield_rolljam_last_trigger_count) {
        s_shield_rolljam_last_trigger_count = 0;
    }
    if(item_count >= 2U && item_count != s_shield_rolljam_last_trigger_count) {
        uint16_t first_idx = (uint16_t)(item_count - 2U);
        uint16_t second_idx = (uint16_t)(item_count - 1U);
        should_trigger = rolljam_scene_shield_receiver_pair_matches(
            app->shield_history, first_idx, second_idx, protocol, &tx_hold_ms);
        if(should_trigger) {
            should_trigger = rolljam_history_get_capture_path(
                app->shield_history, first_idx, first_path);
            if(should_trigger) {
                s_shield_rolljam_last_trigger_count = item_count;
            }
        }
    }
    furi_mutex_release(app->shield_history_mutex);

    if(!should_trigger) {
        furi_string_free(protocol);
        furi_string_free(first_path);
        return false;
    }

    ShieldRollJamTxRequest* request = malloc(sizeof(ShieldRollJamTxRequest));
    if(!request) {
        furi_string_free(protocol);
        furi_string_free(first_path);
        return false;
    }
    request->app = app;
    request->tx_hold_ms = tx_hold_ms ? tx_hold_ms : 666U;

    s_shield_rolljam_tx_thread_done = false;
    s_shield_rolljam_tx_thread =
        furi_thread_alloc_ex("RollJamTX", 1024, rolljam_scene_shield_receiver_rolljam_thread_entry, request);
    if(!s_shield_rolljam_tx_thread) {
        free(request);
        furi_string_free(protocol);
        furi_string_free(first_path);
        return false;
    }

    if(app->loaded_file_path) {
        furi_string_free(app->loaded_file_path);
        app->loaded_file_path = NULL;
    }
    app->loaded_file_path = furi_string_alloc_set(first_path);
    if(!app->loaded_file_path) {
        free(request);
        if(s_shield_rolljam_tx_thread) {
            furi_thread_free(s_shield_rolljam_tx_thread);
            s_shield_rolljam_tx_thread = NULL;
        }
        s_shield_rolljam_tx_thread_done = false;
        furi_string_free(protocol);
        furi_string_free(first_path);
        return false;
    }

    scene_manager_set_scene_state(
        app->scene_manager, RollJamSceneShieldReceiver, SHIELD_SCENE_STATE_TO_SUBSCENE);
    scene_manager_next_scene(app->scene_manager, RollJamSceneEmulate);
    furi_thread_start(s_shield_rolljam_tx_thread);

    furi_string_free(protocol);
    furi_string_free(first_path);
    return true;
}

static bool rolljam_scene_shield_receiver_auto_save_locked(RollJamApp* app) {
    uint16_t item_count = rolljam_history_get_item(app->shield_history);
    if(item_count == 0) {
        return false;
    }

    FlipperFormat* ff = rolljam_history_get_raw_data(app->shield_history, item_count - 1U);
    if(!ff) {
        return false;
    }

    FuriString* protocol = furi_string_alloc();
    FuriString* saved_path = furi_string_alloc();
    if(!protocol || !saved_path) {
        if(protocol) {
            furi_string_free(protocol);
        }
        if(saved_path) {
            furi_string_free(saved_path);
        }
        return false;
    }

    flipper_format_rewind(ff);
    if(!flipper_format_read_string(ff, FF_PROTOCOL, protocol)) {
        furi_string_set_str(protocol, "Unknown");
    }
    furi_string_replace_all(protocol, "/", "_");
    furi_string_replace_all(protocol, " ", "_");

    bool saved = rolljam_storage_save_capture(ff, furi_string_get_cstr(protocol), saved_path);
    furi_string_free(protocol);
    furi_string_free(saved_path);
    return saved;
}

static int32_t rolljam_scene_shield_receiver_tx_offset_hz(const RollJamApp* app) {
    furi_check(app);
    uint8_t index = app->shield_tx_offset_index;
    if(index >= SHIELD_TX_OFFSET_COUNT) {
        index = 3;
    }
    return shield_tx_offset_hz[index];
}

static int32_t rolljam_scene_shield_receiver_resolve_tx_offset(RollJamApp* app) {

    return rolljam_scene_shield_receiver_tx_offset_hz(app);
}

static void rolljam_scene_shield_receiver_decode_cb(
    SubGhzReceiver* receiver,
    SubGhzProtocolDecoderBase* decoder_base,
    void* context) {
    UNUSED(receiver);
    furi_check(decoder_base);
    furi_check(context);
    RollJamApp* app = context;

    if(!app->shield_history || !app->shield_history_mutex || !app->shield_rx_chain) {
        return;
    }

    furi_mutex_acquire(app->shield_history_mutex, FuriWaitForever);
    bool added = rolljam_history_add_to_history(
        app->shield_history,
        decoder_base,
        &app->shield_rx_chain->preset,
        RollJamHistorySourceExternal);
    bool auto_save = app->auto_save;
    bool auto_saved = added && auto_save &&
                      rolljam_scene_shield_receiver_auto_save_locked(app);
    if(added && auto_save && !auto_saved) {
        app->shield_auto_save_failed = true;
    }
    furi_mutex_release(app->shield_history_mutex);

    if(added) {
        notification_message(app->notifications, &sequence_semi_success);
        if(auto_saved) {
            notification_message(app->notifications, &sequence_double_vibro);
        } else if(auto_save) {
            notification_message(app->notifications, &sequence_error);
        }
        view_dispatcher_send_custom_event(
            app->view_dispatcher, RollJamCustomEventShieldReceiverUpdate);
    }
}

static void rolljam_scene_shield_receiver_update_statusbar(RollJamApp* app) {
    furi_check(app);

    char frequency_str[16] = {0};
    char modulation_str[24] = {0};
    char history_stat_str[16] = {0};

    if(app->shield_rx_chain) {
        snprintf(
            frequency_str,
            sizeof(frequency_str),
            "%03lu.%02lu",
            (unsigned long)((app->shield_rx_chain->frequency / 1000000UL) % 1000UL),
            (unsigned long)((app->shield_rx_chain->frequency / 10000UL) % 100UL));
        snprintf(
            modulation_str,
            sizeof(modulation_str),
            "%s %luk/%+ldk",
            furi_string_get_cstr(app->shield_rx_chain->preset.name),
            (unsigned long)((app->shield_rx_chain->rx_bandwidth_hz + 500UL) / 1000UL),
            (long)(rolljam_scene_shield_receiver_tx_offset_hz(app) / 1000L));
    }

    if(app->shield_history) {
        furi_mutex_acquire(app->shield_history_mutex, FuriWaitForever);
        rolljam_history_format_status_text(
            app->shield_history, history_stat_str, sizeof(history_stat_str));
        furi_mutex_release(app->shield_history_mutex);
    } else {
        snprintf(history_stat_str, sizeof(history_stat_str), "0/%u", ROLLJAM_HISTORY_MAX);
    }

    rolljam_view_receiver_add_data_statusbar(
        app->rolljam_receiver, frequency_str, modulation_str, history_stat_str, true);
}

static void rolljam_scene_shield_receiver_teardown(RollJamApp* app) {
    furi_check(app);

    if(app->shield_rx_chain) {
        rolljam_rx_chain_free(app->shield_rx_chain);
        app->shield_rx_chain = NULL;
    }
    if(app->shield_tx_chain) {
        rolljam_tx_chain_free(app->shield_tx_chain);
        app->shield_tx_chain = NULL;
    }

    if(s_shield_devices_inited) {
        subghz_devices_deinit();
        s_shield_devices_inited = false;
    }

    if(s_shield_rolljam_tx_thread) {
        rolljam_scene_shield_receiver_cleanup_rolljam_tx();
    }
}

static bool rolljam_scene_shield_receiver_build(RollJamApp* app) {
    furi_check(app);

    rolljam_radio_deinit(app);

    if(!app->shield_history) {
        app->shield_history = rolljam_history_alloc();
        if(!app->shield_history) {
            FURI_LOG_E(TAG, "Failed to allocate shield history");
            return false;
        }
    }
    if(!app->shield_history_mutex) {
        app->shield_history_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
        if(!app->shield_history_mutex) {
            FURI_LOG_E(TAG, "Failed to allocate shield history mutex");
            return false;
        }
    }
    furi_mutex_acquire(app->shield_history_mutex, FuriWaitForever);
    if(rolljam_history_get_item(app->shield_history) == 0) {
        app->shield_auto_save_failed = false;
    }
    furi_mutex_release(app->shield_history_mutex);

    subghz_devices_init();
    s_shield_devices_inited = true;

    app->shield_rx_chain = rolljam_rx_chain_alloc('E');
    app->shield_tx_chain = rolljam_tx_chain_alloc();
    if(!app->shield_rx_chain || !app->shield_tx_chain) {
        FURI_LOG_E(TAG, "Failed to allocate shield chains");
        rolljam_scene_shield_receiver_teardown(app);
        return false;
    }

    if(!rolljam_rx_chain_acquire_device(
           app->shield_rx_chain, SubGhzRadioDeviceTypeExternalCC1101)) {
        FURI_LOG_E(TAG, "External CC1101 unavailable - Shield RX requires it");
        rolljam_scene_shield_receiver_teardown(app);
        return false;
    }
    if(!rolljam_tx_chain_acquire_device(app->shield_tx_chain)) {
        FURI_LOG_E(TAG, "Internal CC1101 unavailable");
        rolljam_scene_shield_receiver_teardown(app);
        return false;
    }

    size_t preset_count = subghz_setting_get_preset_count(app->setting);
    if(app->shield_preset_index >= preset_count) {
        FURI_LOG_E(TAG, "Invalid shield preset index");
        rolljam_scene_shield_receiver_teardown(app);
        return false;
    }

    const char* preset_name = subghz_setting_get_preset_name(app->setting, app->shield_preset_index);
    if(!rolljam_rx_chain_set_preset(
           app->shield_rx_chain, app->setting, preset_name, app->shield_freq) ||
       !rolljam_rx_chain_apply_shield_profile(app->shield_rx_chain)) {
        FURI_LOG_E(TAG, "Failed to configure Shield RX profile");
        rolljam_scene_shield_receiver_teardown(app);
        return false;
    }

    if(!rolljam_tx_chain_configure(
           app->shield_tx_chain,
           app->setting,
           app->shield_freq,
           rolljam_scene_shield_receiver_resolve_tx_offset(app),
           app->shield_tx_power)) {
        FURI_LOG_E(TAG, "Failed to configure Shield TX");
        rolljam_scene_shield_receiver_teardown(app);
        return false;
    }

    if(!rolljam_rx_chain_init_receiver(app->shield_rx_chain)) {
        FURI_LOG_E(TAG, "Failed to init external RX chain");
        rolljam_scene_shield_receiver_teardown(app);
        return false;
    }

    rolljam_rx_chain_set_decode_callback(
        app->shield_rx_chain, rolljam_scene_shield_receiver_decode_cb, app);

    return true;
}

void rolljam_scene_shield_receiver_on_enter(void* context) {
    furi_check(context);
    RollJamApp* app = context;

    if(s_shield_rolljam_tx_thread) {
        rolljam_scene_shield_receiver_cleanup_rolljam_tx();
    }

    if(!rolljam_ensure_receiver_view(app)) {
        notification_message(app->notifications, &sequence_error);
        scene_manager_search_and_switch_to_previous_scene(
            app->scene_manager, RollJamSceneStart);
        return;
    }

    if(!rolljam_scene_shield_receiver_build(app)) {
        notification_message(app->notifications, &sequence_error);
        scene_manager_search_and_switch_to_previous_scene(
            app->scene_manager, RollJamSceneStart);
        return;
    }

    rolljam_view_receiver_set_history_mutex(
        app->rolljam_receiver, app->shield_history_mutex);
    rolljam_view_receiver_sync_menu_from_history(
        app->rolljam_receiver, app->shield_history);
    rolljam_view_receiver_set_callback(
        app->rolljam_receiver, rolljam_scene_shield_receiver_view_callback, app);
    rolljam_view_receiver_set_lock(app->rolljam_receiver, app->lock);
    rolljam_view_receiver_set_autosave(app->rolljam_receiver, app->auto_save);
    rolljam_view_receiver_set_sub_decode_mode(app->rolljam_receiver, false);

    if(app->selected_capture.owner == RollJamCaptureOwnerShieldReceiver &&
       app->selected_capture.history == app->shield_history &&
       rolljam_selected_capture_is_valid(app)) {
        rolljam_view_receiver_set_idx_menu(
            app->rolljam_receiver, app->selected_capture.index);
    }

    rolljam_scene_shield_receiver_update_statusbar(app);
    scene_manager_set_scene_state(
        app->scene_manager, RollJamSceneShieldReceiver, SHIELD_SCENE_STATE_NONE);

    view_dispatcher_switch_to_view(app->view_dispatcher, RollJamViewReceiver);
    view_dispatcher_send_custom_event(
        app->view_dispatcher, RollJamCustomEventShieldReceiverDeferredStart);
}

static void rolljam_scene_shield_receiver_handle_back(RollJamApp* app) {
    bool has_history = false;
    bool auto_save_failed = false;
    if(app->shield_history && app->shield_history_mutex) {
        furi_mutex_acquire(app->shield_history_mutex, FuriWaitForever);
        has_history = rolljam_history_get_item(app->shield_history) > 0;
        auto_save_failed = app->shield_auto_save_failed;
        furi_mutex_release(app->shield_history_mutex);
    }

    if(has_history && (!app->auto_save || auto_save_failed)) {
        app->unsaved_history_owner = RollJamCaptureOwnerShieldReceiver;
        scene_manager_set_scene_state(
            app->scene_manager, RollJamSceneShieldReceiver, SHIELD_SCENE_STATE_TO_SUBSCENE);
        scene_manager_next_scene(app->scene_manager, RollJamSceneNeedSaving);
    } else {
        app->unsaved_history_owner = RollJamCaptureOwnerNone;
        scene_manager_search_and_switch_to_previous_scene(
            app->scene_manager, RollJamSceneStart);
    }
}

bool rolljam_scene_shield_receiver_on_event(void* context, SceneManagerEvent event) {
    furi_check(context);
    RollJamApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        switch(event.event) {
        case RollJamCustomEventShieldReceiverDeferredStart:
            if(!rolljam_rx_chain_start(app->shield_rx_chain) ||
               !rolljam_tx_chain_start_carrier(app->shield_tx_chain)) {
                FURI_LOG_E(TAG, "Failed to start shield TX/RX");
                rolljam_scene_shield_receiver_teardown(app);
                notification_message(app->notifications, &sequence_error);
                scene_manager_search_and_switch_to_previous_scene(
                    app->scene_manager, RollJamSceneStart);
            } else {
                notification_message(app->notifications, &sequence_tx);
                rolljam_scene_shield_receiver_update_statusbar(app);
            }
            consumed = true;
            break;

        case RollJamCustomEventShieldReceiverUpdate:
            rolljam_view_receiver_sync_menu_from_history(
                app->rolljam_receiver, app->shield_history);
            rolljam_scene_shield_receiver_update_statusbar(app);
            rolljam_scene_shield_receiver_maybe_trigger_rolljam(app);
            consumed = true;
            break;

        case RollJamCustomEventViewReceiverOK: {
            uint16_t idx = rolljam_view_receiver_get_idx_menu(app->rolljam_receiver);
            furi_mutex_acquire(app->shield_history_mutex, FuriWaitForever);
            bool valid = idx < rolljam_history_get_item(app->shield_history);
            furi_mutex_release(app->shield_history_mutex);
            if(valid) {
                rolljam_selected_capture_set(
                    app,
                    RollJamCaptureOwnerShieldReceiver,
                    idx);
                scene_manager_set_scene_state(
                    app->scene_manager,
                    RollJamSceneShieldReceiver,
                    SHIELD_SCENE_STATE_TO_SUBSCENE);
                                scene_manager_next_scene(app->scene_manager, RollJamSceneReceiverInfo);
            }
            consumed = true;
            break;
        }

        case RollJamCustomEventViewReceiverDeleteItem: {
            uint16_t idx = rolljam_view_receiver_get_idx_menu(app->rolljam_receiver);
            furi_mutex_acquire(app->shield_history_mutex, FuriWaitForever);
            bool valid = idx < rolljam_history_get_item(app->shield_history);
            if(valid) {
                rolljam_history_delete_item(app->shield_history, idx);
            }
            uint16_t count_after = rolljam_history_get_item(app->shield_history);
            if(count_after == 0) {
                app->shield_auto_save_failed = false;
            }
            furi_mutex_release(app->shield_history_mutex);

            if(valid) {
                rolljam_view_receiver_delete_item(app->rolljam_receiver, idx);

                if(count_after == 0) {
                    rolljam_view_receiver_sync_menu_from_history(
                        app->rolljam_receiver, app->shield_history);
                    rolljam_view_receiver_set_idx_menu(app->rolljam_receiver, 0);
                }
                rolljam_scene_shield_receiver_update_statusbar(app);
            }
            consumed = true;
            break;
        }

        case RollJamCustomEventViewReceiverConfig:
            scene_manager_set_scene_state(
                app->scene_manager,
                RollJamSceneShieldReceiver,
                SHIELD_SCENE_STATE_TO_SUBSCENE);
            scene_manager_next_scene(app->scene_manager, RollJamSceneShieldReceiverConfig);
            consumed = true;
            break;

        case RollJamCustomEventViewReceiverBack:
            rolljam_scene_shield_receiver_handle_back(app);
            consumed = true;
            break;

        case RollJamCustomEventViewReceiverUnlock:
            app->lock = RollJamLockOff;
            rolljam_view_receiver_set_lock(app->rolljam_receiver, app->lock);
            consumed = true;
            break;

        default:
            break;
        }
    } else if(event.type == SceneManagerEventTypeTick) {
        if(s_shield_rolljam_tx_thread && s_shield_rolljam_tx_thread_done) {
            rolljam_scene_shield_receiver_cleanup_rolljam_tx();
        }
        if(app->shield_rx_chain) {
            rolljam_view_receiver_set_rssi(
                app->rolljam_receiver, rolljam_rx_chain_get_rssi(app->shield_rx_chain));
            notification_message(app->notifications, &sequence_blink_cyan_10);
        }
        consumed = true;
    }

    return consumed;
}

void rolljam_scene_shield_receiver_on_exit(void* context) {
    furi_check(context);
    RollJamApp* app = context;

    const bool leaving_for_subscene =
        (scene_manager_get_scene_state(app->scene_manager, RollJamSceneShieldReceiver) ==
         SHIELD_SCENE_STATE_TO_SUBSCENE);

    rolljam_scene_shield_receiver_teardown(app);

    if(leaving_for_subscene) {
        return;
    }

    rolljam_view_receiver_reset_menu(app->rolljam_receiver);
    if(app->selected_capture.owner == RollJamCaptureOwnerShieldReceiver) {
        rolljam_selected_capture_clear(app);
    }
}

void rolljam_scene_shield_receiver_view_callback(RollJamCustomEvent event, void* context) {
    furi_check(context);
    RollJamApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, event);
}

#endif // ENABLE_SHIELD_RX_SCENE
