#include "../subghz_i.h" // IWYU pragma: keep
#include "../helpers/subghz_frequency_notebook.h"
#include "../views/subghz_frequency_analyzer.h"
#include <loader/loader.h>

#define TAG "SubGhzSceneFrequencyAnalyzer"

#define ARF_SUBGHZ_HUB_PATH EXT_PATH("apps/ARF Tools/arf_subghz_full.fap")

static const NotificationSequence sequence_saved = {
    &message_blink_stop,
    &message_blue_0,
    &message_green_255,
    &message_red_0,
    &message_vibro_on,
    &message_delay_100,
    &message_vibro_off,
    NULL,
};

static void subghz_scene_frequency_analyzer_prepare_receiver(
    SubGhz* subghz,
    uint32_t frequency,
    uint32_t preset_index) {
    subghz->last_settings->frequency = frequency;
    subghz->last_settings->preset_index = preset_index;
    subghz_txrx_set_preset_internal(
        subghz->txrx, frequency, preset_index, subghz->last_settings->tx_power);
#if defined(ARF_PROFILE_FA)
    subghz->last_settings->enable_hopping = false;
#else
    subghz->last_settings->hopping_mode = SubGhzHoppingModeOff;
#endif
    subghz_last_settings_save(subghz->last_settings);
}

static void subghz_scene_frequency_analyzer_open_receiver(SubGhz* subghz) {
    subghz_rx_key_state_set(subghz, SubGhzRxKeyStateIDLE);
#if defined(ARF_PROFILE_FA)
    Loader* loader = furi_record_open(RECORD_LOADER);
    loader_clear_launch_queue(loader);
    loader_enqueue_launch(loader, "Sub-GHz", "receiver", LoaderDeferredLaunchFlagGui);
    loader_enqueue_launch(loader, ARF_SUBGHZ_HUB_PATH, "1", LoaderDeferredLaunchFlagGui);
    furi_record_close(RECORD_LOADER);
    scene_manager_stop(subghz->scene_manager);
    view_dispatcher_stop(subghz->view_dispatcher);
#else
    scene_manager_previous_scene(subghz->scene_manager);
    subghz_ensure_receiver_view(subghz);
    scene_manager_next_scene(subghz->scene_manager, SubGhzSceneReceiver);
#endif
}

void subghz_scene_frequency_analyzer_callback(SubGhzCustomEvent event, void* context) {
    furi_assert(context);
    SubGhz* subghz = context;
    view_dispatcher_send_custom_event(subghz->view_dispatcher, event);
}

void subghz_scene_frequency_analyzer_on_enter(void* context) {
    SubGhz* subghz = context;
#if !defined(ARF_PROFILE_FA)
    subghz_ensure_frequency_analyzer_view(subghz);
#endif
    subghz_frequency_analyzer_set_callback(
        subghz->subghz_frequency_analyzer, subghz_scene_frequency_analyzer_callback, subghz);
    subghz_frequency_analyzer_feedback_level(
        subghz->subghz_frequency_analyzer,
        subghz->last_settings->frequency_analyzer_feedback_level,
        true);
    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdFrequencyAnalyzer);
}

bool subghz_scene_frequency_analyzer_on_event(void* context, SceneManagerEvent event) {
    SubGhz* subghz = context;
    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SubGhzCustomEventSceneAnalyzerLock) {
            notification_message(subghz->notifications, &sequence_set_green_255);
            switch(subghz_frequency_analyzer_feedback_level(
                subghz->subghz_frequency_analyzer,
                SubGHzFrequencyAnalyzerFeedbackLevelAll,
                false)) {
            case SubGHzFrequencyAnalyzerFeedbackLevelAll:
                notification_message(subghz->notifications, &sequence_success);
                break;
            case SubGHzFrequencyAnalyzerFeedbackLevelVibro:
                notification_message(subghz->notifications, &sequence_single_vibro);
                break;
            case SubGHzFrequencyAnalyzerFeedbackLevelMute:
                break;
            }
            notification_message(subghz->notifications, &sequence_display_backlight_on);
            return true;
        } else if(event.event == SubGhzCustomEventSceneAnalyzerUnlock) {
            notification_message(subghz->notifications, &sequence_reset_rgb);
            return true;
        } else if(event.event == SubGhzCustomEventViewFreqAnalOkShort) {
            SubGhzFrequencyAnalyzerObservation observation;
            const bool has_observation = subghz_frequency_analyzer_get_observation(
                subghz->subghz_frequency_analyzer, &observation);
            const bool notebook_saved = has_observation &&
                                        subghz_frequency_notebook_append(&observation);
            notification_message(
                subghz->notifications, notebook_saved ? &sequence_saved : &sequence_error);

            uint32_t frequency = has_observation ? observation.frequency :
                                                   subghz_frequency_analyzer_get_frequency_to_save(
                                                       subghz->subghz_frequency_analyzer);
            if(frequency > 0) {
                subghz->last_settings->frequency = frequency;
                // Disable Hopping before opening the receiver scene!
#if defined(ARF_PROFILE_FA)
                if(subghz->last_settings->enable_hopping) {
                    subghz->last_settings->enable_hopping = false;
                }
#else
                subghz->last_settings->hopping_mode = SubGhzHoppingModeOff;
#endif
                subghz_last_settings_save(subghz->last_settings);
            }

            return true;
        } else if(event.event == SubGhzCustomEventViewFreqAnalOkLong) {
            // Don't need to save, we already saved on short event (and on exit event too)
            subghz_scene_frequency_analyzer_open_receiver(subghz);
            return true;
        } else if(event.event == SubGhzCustomEventViewFreqAnalPresetRx) {
            uint32_t frequency = 0;
            uint32_t preset_index = 0;
            if(!subghz_frequency_analyzer_get_selected_preset(
                   subghz->subghz_frequency_analyzer, &frequency, &preset_index)) {
                notification_message(subghz->notifications, &sequence_error);
                return true;
            }

            SubGhzSetting* setting = subghz_txrx_get_setting(subghz->txrx);
            if(preset_index >= subghz_setting_get_preset_count(setting)) {
                notification_message(subghz->notifications, &sequence_error);
                return true;
            }
            subghz_scene_frequency_analyzer_prepare_receiver(subghz, frequency, preset_index);
            subghz_scene_frequency_analyzer_open_receiver(subghz);
            return true;
        }
    }
    return false;
}

void subghz_scene_frequency_analyzer_on_exit(void* context) {
    SubGhz* subghz = context;
    notification_message(subghz->notifications, &sequence_reset_rgb);

    subghz->last_settings->frequency_analyzer_feedback_level =
        subghz_frequency_analyzer_feedback_level(subghz->subghz_frequency_analyzer, 0, false);
    subghz->last_settings->frequency_analyzer_trigger =
        subghz_frequency_analyzer_get_trigger_level(subghz->subghz_frequency_analyzer);
    subghz_last_settings_save(subghz->last_settings);
}
