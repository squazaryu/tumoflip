#include "../subghz_i.h"
#include "../helpers/subghz_txrx_i.h"
#include <lib/toolbox/value_index.h>
#include <lib/subghz/subghz_rx_profiles.h>

#define TAG "SubGhzSceneReceiverConfig"

enum SubGhzSettingIndex {
    SubGhzSettingIndexFrequency,
    SubGhzSettingIndexModulation,
    SubGhzSettingIndexHoppingMode,
    SubGhzSettingIndexHoppingRSSI,
    SubGhzSettingIndexProtocolPack,
    SubGhzSettingIndexProtocolPackInfo,
    SubGhzSettingIndexProtocolFilter,
    SubGhzSettingIndexBinRAW,
    SubGhzSettingIndexIgnoreReversRB2,
    SubGhzSettingIndexIgnoreAlarms,
    SubGhzSettingIndexIgnoreSensors,
    SubGhzSettingIndexIgnorePrinceton,
    SubGhzSettingIndexIgnoreNiceFlorS,
    SubGhzSettingIndexDeleteOldSignals,
    SubGhzSettingIndexSound,
    SubGhzSettingIndexResetToDefault,
    SubGhzSettingIndexLock,
    SubGhzSettingIndexRAWThresholdRSSI,
};

#define RAW_THRESHOLD_RSSI_COUNT 11
const char* const raw_threshold_rssi_text[RAW_THRESHOLD_RSSI_COUNT] = {
    "-----",
    "-85.0",
    "-80.0",
    "-75.0",
    "-70.0",
    "-65.0",
    "-60.0",
    "-55.0",
    "-50.0",
    "-45.0",
    "-40.0",

};
const float raw_threshold_rssi_value[RAW_THRESHOLD_RSSI_COUNT] = {
    -90.0f,
    -85.0f,
    -80.0f,
    -75.0f,
    -70.0f,
    -65.0f,
    -60.0f,
    -55.0f,
    -50.0f,
    -45.0f,
    -40.0f,
};

static const char* const hopping_mode_text[SubGhzHoppingModeCount] = {
    "OFF",
    "Frequency",
    "Preset",
    "Combined",
};

#define COMBO_BOX_COUNT 2

const uint32_t hopping_value[COMBO_BOX_COUNT] = {
    SubGhzHopperStateOFF,
    SubGhzHopperStateRunning,
};

const uint32_t speaker_value[COMBO_BOX_COUNT] = {
    SubGhzSpeakerStateShutdown,
    SubGhzSpeakerStateEnable,
};

const uint32_t bin_raw_value[COMBO_BOX_COUNT] = {
    SubGhzProtocolFlag_Decodable,
    SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_BinRAW,
};

const char* const combobox_text[COMBO_BOX_COUNT] = {
    "OFF",
    "ON",
};

static void
    subghz_scene_receiver_config_set_ignore_filter(VariableItem* item, SubGhzProtocolFlag filter) {
    SubGhz* subghz = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);

    variable_item_set_current_value_text(item, combobox_text[index]);

    if(index == 0) {
        CLEAR_BIT(subghz->ignore_filter, filter);
    } else {
        SET_BIT(subghz->ignore_filter, filter);
    }

    subghz->last_settings->ignore_filter = subghz->ignore_filter;
}

uint8_t subghz_scene_receiver_config_next_frequency(const uint32_t value, void* context) {
    furi_assert(context);
    SubGhz* subghz = context;
    SubGhzSetting* setting = subghz_txrx_get_setting(subghz->txrx);

    uint8_t index = 0;
    for(size_t i = 0; i < subghz_setting_get_frequency_count(setting); i++) {
        if(value == subghz_setting_get_frequency(setting, i)) {
            index = i;
            break;
        } else {
            index = subghz_setting_get_frequency_default_index(setting);
        }
    }
    return index;
}

// TumoHonda is owned by the Honda RX profile, not by the generic Core
// modulation picker. Keep its real setting index internal and expose only
// visible modulation indices to the UI.
static bool subghz_scene_receiver_config_is_profile_preset(const char* name) {
    return strcmp(name, SUBGHZ_RX_CUSTOM_NAME) == 0;
}

static size_t subghz_scene_receiver_config_modulation_count(SubGhzSetting* setting) {
    size_t count = 0;
    for(size_t i = 0; i < subghz_setting_get_preset_count(setting); i++) {
        if(!subghz_scene_receiver_config_is_profile_preset(
               subghz_setting_get_preset_name(setting, i))) {
            count++;
        }
    }
    return count;
}

static int
    subghz_scene_receiver_config_modulation_index(SubGhzSetting* setting, size_t visible_index) {
    size_t visible = 0;
    for(size_t actual = 0; actual < subghz_setting_get_preset_count(setting); actual++) {
        if(subghz_scene_receiver_config_is_profile_preset(
               subghz_setting_get_preset_name(setting, actual))) {
            continue;
        }
        if(visible++ == visible_index) return actual;
    }
    return -1;
}

static uint8_t
    subghz_scene_receiver_config_visible_preset_index(SubGhzSetting* setting, size_t actual_index) {
    size_t visible = 0;
    for(size_t actual = 0; actual < subghz_setting_get_preset_count(setting); actual++) {
        if(subghz_scene_receiver_config_is_profile_preset(
               subghz_setting_get_preset_name(setting, actual))) {
            continue;
        }
        if(actual == actual_index) return visible <= UINT8_MAX ? (uint8_t)visible : 0;
        visible++;
    }
    return 0;
}

static const char*
    subghz_scene_receiver_config_preset_label(SubGhzSetting* setting, size_t actual_index) {
    const char* name = subghz_setting_get_preset_name(setting, actual_index);
    return subghz_scene_receiver_config_is_profile_preset(name) ? "Honda RX Custom" : name;
}

uint8_t subghz_scene_receiver_config_next_preset(const char* preset_name, void* context) {
    furi_assert(context);
    SubGhz* subghz = context;
    uint8_t index = 0;
    SubGhzSetting* setting = subghz_txrx_get_setting(subghz->txrx);

    for(size_t actual = 0; actual < subghz_setting_get_preset_count(setting); actual++) {
        if(!strcmp(subghz_setting_get_preset_name(setting, actual), preset_name)) {
            index = subghz_scene_receiver_config_visible_preset_index(setting, actual);
            break;
        } else {
            //  index = subghz_setting_get_frequency_default_index(setting);
        }
    }
    return index;
}

static SubGhzHoppingMode subghz_scene_receiver_config_get_hopping_mode(void* context) {
    furi_assert(context);
    SubGhz* subghz = context;
    return subghz->last_settings->hopping_mode;
}

static bool subghz_scene_receiver_config_is_raw(const SubGhz* subghz) {
    return scene_manager_get_scene_state(subghz->scene_manager, SubGhzSceneReadRAW) ==
           SubGhzCustomEventManagerSet;
}

static void subghz_scene_receiver_config_set_frequency(VariableItem* item) {
    SubGhz* subghz = variable_item_get_context(item);
    if(subghz->rx_profile_item) {
        variable_item_set_current_value_index(subghz->rx_profile_item, 0);
        variable_item_set_current_value_text(subghz->rx_profile_item, "Manual");
    }
    uint8_t index = variable_item_get_current_value_index(item);
    SubGhzSetting* setting = subghz_txrx_get_setting(subghz->txrx);
    const bool is_raw = subghz_scene_receiver_config_is_raw(subghz);

    if(is_raw || (subghz->last_settings->hopping_mode != SubGhzHoppingModeFrequency &&
                  subghz->last_settings->hopping_mode != SubGhzHoppingModeCombined)) {
        char text_buf[10] = {0};
        uint32_t frequency = subghz_setting_get_frequency(setting, index);
        SubGhzRadioPreset preset = subghz_txrx_get_preset(subghz->txrx);

        snprintf(
            text_buf,
            sizeof(text_buf),
            "%lu.%02lu",
            frequency / 1000000,
            (frequency % 1000000) / 10000);
        variable_item_set_current_value_text(item, text_buf);

        //Set TX Power
        subghz_txrx_set_tx_power(preset.data, preset.data_size, subghz->tx_power);

        //Set the preset now.
        subghz_txrx_set_preset(
            subghz->txrx,
            furi_string_get_cstr(preset.name),
            frequency,
            preset.data,
            preset.data_size);

        preset = subghz_txrx_get_preset(subghz->txrx);

        if(is_raw) {
            subghz->last_settings->raw_frequency = preset.frequency;
        } else {
            subghz->last_settings->frequency = preset.frequency;
        }
        subghz_setting_set_default_frequency(setting, preset.frequency);
    } else {
        variable_item_set_current_value_index(
            item, subghz_setting_get_frequency_default_index(setting));
    }
}

static void subghz_scene_receiver_config_set_preset(VariableItem* item) {
    SubGhz* subghz = variable_item_get_context(item);
    if(subghz->rx_profile_item) {
        variable_item_set_current_value_index(subghz->rx_profile_item, 0);
        variable_item_set_current_value_text(subghz->rx_profile_item, "Manual");
    }
    uint8_t visible_index = variable_item_get_current_value_index(item);
    SubGhzSetting* setting = subghz_txrx_get_setting(subghz->txrx);
    const int actual_index = subghz_scene_receiver_config_modulation_index(setting, visible_index);
    if(actual_index < 0) return;
    const bool is_raw = subghz_scene_receiver_config_is_raw(subghz);

    if(is_raw || (subghz->last_settings->hopping_mode != SubGhzHoppingModePreset &&
                  subghz->last_settings->hopping_mode != SubGhzHoppingModeCombined)) {
        const char* preset_name = subghz_setting_get_preset_name(setting, actual_index);
        variable_item_set_current_value_text(item, preset_name);
        SubGhzRadioPreset preset = subghz_txrx_get_preset(subghz->txrx);
        uint8_t* preset_data = subghz_setting_get_preset_data(setting, actual_index);
        size_t preset_data_size = subghz_setting_get_preset_data_size(setting, actual_index);

        subghz_txrx_set_tx_power(preset_data, preset_data_size, subghz->tx_power);

        subghz_txrx_set_preset(
            subghz->txrx, preset_name, preset.frequency, preset_data, preset_data_size);
        if(is_raw) {
            subghz->last_settings->raw_preset_index = actual_index;
        } else {
            subghz->last_settings->preset_index = actual_index;
        }
    } else {
        variable_item_set_current_value_index(item, subghz->last_settings->preset_index);
    }
}

static void subghz_scene_receiver_config_set_hopping_mode(VariableItem* item) {
    SubGhz* subghz = variable_item_get_context(item);
    SubGhzHoppingMode mode = variable_item_get_current_value_index(item);
    VariableItem* frequency_item =
        variable_item_list_get(subghz->variable_item_list, SubGhzSettingIndexFrequency);
    VariableItem* preset_item =
        variable_item_list_get(subghz->variable_item_list, SubGhzSettingIndexModulation);

    variable_item_set_current_value_text(item, hopping_mode_text[mode]);
    subghz->last_settings->hopping_mode = mode;

    if(mode == SubGhzHoppingModeFrequency || mode == SubGhzHoppingModeCombined) {
        variable_item_set_current_value_text(frequency_item, " -----");
    } else {
        subghz_scene_receiver_config_set_frequency(frequency_item);
    }
    if(mode == SubGhzHoppingModePreset || mode == SubGhzHoppingModeCombined) {
        variable_item_set_current_value_text(preset_item, " -----");
    } else {
        subghz_scene_receiver_config_set_preset(preset_item);
    }

    if(mode != SubGhzHoppingModeOff) {
        subghz_txrx_hopper_set_state(subghz->txrx, SubGhzHopperStateRunning);
    } else {
        subghz_txrx_hopper_set_state(subghz->txrx, SubGhzHopperStateOFF);
    }
}

static void subghz_scene_receiver_config_set_hopping_rssi(VariableItem* item) {
    SubGhz* subghz = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    float threshold = raw_threshold_rssi_value[index];
    variable_item_set_current_value_text(item, raw_threshold_rssi_text[index]);
    subghz->last_settings->hopping_threshold = threshold;
}

static void
    subghz_scene_receiver_config_update_protocol_pack_status(VariableItem* item, SubGhz* subghz) {
    const SubGhzProtocolPackReport* report = subghz_txrx_get_protocol_pack_report(subghz->txrx);
    char status_text[32];
    if(report->expected_plugin_count == 0) {
        snprintf(status_text, sizeof(status_text), "Core only");
    } else {
        snprintf(
            status_text,
            sizeof(status_text),
            "%zu/%zu %s",
            report->loaded_plugin_count,
            report->expected_plugin_count,
            report->loaded_plugin_count == report->expected_plugin_count ? "OK" : "ERR");
    }
    variable_item_set_current_value_text(item, status_text);
}

static void subghz_scene_receiver_config_set_protocol_pack(VariableItem* item) {
    SubGhz* subghz = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    uint8_t previous = subghz->last_settings->protocol_pack_group;

    if(subghz_txrx_reload_protocol_pack(subghz->txrx, index)) {
        subghz->last_settings->protocol_pack_group = index;
    } else {
        index = previous;
        variable_item_set_current_value_index(item, previous);
    }
    variable_item_set_current_value_text(item, subghz_protocol_pack_group_get_name(index));

    subghz_scene_receiver_config_update_protocol_pack_status(
        variable_item_list_get(subghz->variable_item_list, SubGhzSettingIndexProtocolPackInfo),
        subghz);
}

static void subghz_scene_receiver_config_set_speaker(VariableItem* item) {
    SubGhz* subghz = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);

    variable_item_set_current_value_text(item, combobox_text[index]);
    subghz_txrx_speaker_set_state(subghz->txrx, speaker_value[index]);
}

static void subghz_scene_receiver_config_set_bin_raw(VariableItem* item) {
    SubGhz* subghz = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);

    variable_item_set_current_value_text(item, combobox_text[index]);
    subghz->filter = bin_raw_value[index];
    subghz_txrx_receiver_set_filter(subghz->txrx, subghz->filter);

    // We can set here, but during subghz_last_settings_save filter was changed to ignore BinRAW
    subghz->last_settings->filter = subghz->filter;
}

static void subghz_scene_receiver_config_set_raw_threshold_rssi(VariableItem* item) {
    SubGhz* subghz = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);

    variable_item_set_current_value_text(item, raw_threshold_rssi_text[index]);
    subghz_threshold_rssi_set(subghz->threshold_rssi, raw_threshold_rssi_value[index]);

    subghz->last_settings->rssi = raw_threshold_rssi_value[index];
}

static inline bool subghz_scene_receiver_config_ignore_filter_get_index(
    SubGhzProtocolFlag filter,
    SubGhzProtocolFlag flag) {
    return READ_BIT(filter, flag) > 0;
}

static void subghz_scene_receiver_config_set_reversrb2(VariableItem* item) {
    subghz_scene_receiver_config_set_ignore_filter(item, SubGhzProtocolFlag_ReversRB2);
}

static void subghz_scene_receiver_config_set_alarms(VariableItem* item) {
    subghz_scene_receiver_config_set_ignore_filter(item, SubGhzProtocolFlag_Alarms);
}

static void subghz_scene_receiver_config_set_sensors(VariableItem* item) {
    subghz_scene_receiver_config_set_ignore_filter(item, SubGhzProtocolFlag_Sensors);
}

static void subghz_scene_receiver_config_set_princeton(VariableItem* item) {
    subghz_scene_receiver_config_set_ignore_filter(item, SubGhzProtocolFlag_Princeton);
}

static void subghz_scene_receiver_config_set_niceflors(VariableItem* item) {
    subghz_scene_receiver_config_set_ignore_filter(item, SubGhzProtocolFlag_NiceFlorS);
}

static void subghz_scene_receiver_config_set_delete_old_signals(VariableItem* item) {
    SubGhz* subghz = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);

    variable_item_set_current_value_text(item, combobox_text[index]);

    subghz->last_settings->delete_old_signals = index == 1;
}

static void subghz_scene_receiver_config_var_list_enter_callback(void* context, uint32_t index) {
    furi_assert(context);
    SubGhz* subghz = context;
    if(index == SubGhzSettingIndexProtocolPackInfo) {
        scene_manager_next_scene(subghz->scene_manager, SubGhzSceneProtocolPackInfo);
    } else if(index == SubGhzSettingIndexProtocolFilter) {
        scene_manager_next_scene(subghz->scene_manager, SubGhzSceneProtocolList);
    } else if(index == SubGhzSettingIndexLock) {
        view_dispatcher_send_custom_event(
            subghz->view_dispatcher, SubGhzCustomEventSceneSettingLock);
    } else if(index == SubGhzSettingIndexResetToDefault) {
        // Reset all values to default state!
        subghz_txrx_set_preset_internal(
            subghz->txrx,
            SUBGHZ_LAST_SETTING_DEFAULT_FREQUENCY,
            SUBGHZ_LAST_SETTING_DEFAULT_PRESET,
            subghz->tx_power);

        SubGhzSetting* setting = subghz_txrx_get_setting(subghz->txrx);
        SubGhzRadioPreset preset = subghz_txrx_get_preset(subghz->txrx);
        const char* preset_name = furi_string_get_cstr(preset.name);
        int preset_index = subghz_setting_get_inx_preset_by_name(setting, preset_name);
        const int default_index = 0;

        subghz->last_settings->frequency = preset.frequency;
        subghz->last_settings->preset_index = preset_index;
        subghz->last_settings->raw_frequency = SUBGHZ_LAST_SETTING_DEFAULT_FREQUENCY;
        subghz->last_settings->raw_preset_index = SUBGHZ_LAST_SETTING_DEFAULT_PRESET;

        subghz_threshold_rssi_set(subghz->threshold_rssi, raw_threshold_rssi_value[default_index]);
        subghz->filter = bin_raw_value[0];
        subghz->ignore_filter = 0x00;
        subghz_txrx_receiver_set_filter(subghz->txrx, subghz->filter);
        subghz->last_settings->ignore_filter = subghz->ignore_filter;
        subghz->last_settings->filter = subghz->filter;
        subghz->last_settings->delete_old_signals = false;
        subghz->last_settings->protocol_filter[0] = '\0';
        subghz->last_settings->tx_power = subghz->tx_power = 0;
        subghz_txrx_speaker_set_state(subghz->txrx, speaker_value[default_index]);

        subghz_txrx_hopper_set_state(subghz->txrx, hopping_value[default_index]);
        subghz->last_settings->hopping_mode = SubGhzHoppingModeOff;
        if(subghz_txrx_reload_protocol_pack(subghz->txrx, SubGhzProtocolPackGroupCore)) {
            subghz->last_settings->protocol_pack_group = SubGhzProtocolPackGroupCore;
        }

        variable_item_list_set_selected_item(subghz->variable_item_list, default_index);
        variable_item_list_reset(subghz->variable_item_list);

        subghz_last_settings_save(subghz->last_settings);

        view_dispatcher_send_custom_event(
            subghz->view_dispatcher, SubGhzCustomEventSceneSettingResetToDefault);
    }
}

static void subghz_scene_receiver_config_set_rx_profile(VariableItem* item) {
    SubGhz* subghz = variable_item_get_context(item);
    const unsigned index = variable_item_get_current_value_index(item);
    const SubGhzRxProfile* profile = subghz_rx_profile_get(index);
    const bool is_raw = subghz_scene_receiver_config_is_raw(subghz);
    if(!profile) {
        variable_item_set_current_value_text(item, "Manual");
        return;
    }
    if(!is_raw && subghz->last_settings->hopping_mode != SubGhzHoppingModeOff) {
        variable_item_set_current_value_index(item, 0);
        variable_item_set_current_value_text(item, "Stop hopping");
        return;
    }
    SubGhzSetting* setting = subghz_txrx_get_setting(subghz->txrx);
    const int preset_index = subghz_rx_profile_preset_index(setting, index);
    if(preset_index < 0 || preset_index > UINT8_MAX ||
       !furi_hal_subghz_is_frequency_valid(profile->frequency)) {
        variable_item_set_current_value_index(item, 0);
        variable_item_set_current_value_text(item, "Unavailable");
        return;
    }
    subghz_txrx_set_preset_internal(
        subghz->txrx, profile->frequency, preset_index, subghz->tx_power);
    if(is_raw) {
        subghz->last_settings->raw_frequency = profile->frequency;
        subghz->last_settings->raw_preset_index = preset_index;
    } else {
        subghz->last_settings->frequency = profile->frequency;
        subghz->last_settings->preset_index = preset_index;
    }
    VariableItem* frequency =
        variable_item_list_get(subghz->variable_item_list, SubGhzSettingIndexFrequency);
    char text[12];
    snprintf(
        text,
        sizeof(text),
        "%lu.%02lu",
        (unsigned long)(profile->frequency / 1000000),
        (unsigned long)((profile->frequency % 1000000) / 10000));
    variable_item_set_current_value_text(frequency, text);
    variable_item_set_current_value_index(
        frequency, subghz_scene_receiver_config_next_frequency(profile->frequency, subghz));
    VariableItem* modulation =
        variable_item_list_get(subghz->variable_item_list, SubGhzSettingIndexModulation);
    variable_item_set_current_value_index(
        modulation, subghz_scene_receiver_config_visible_preset_index(setting, preset_index));
    variable_item_set_current_value_text(
        modulation, subghz_scene_receiver_config_preset_label(setting, preset_index));
    variable_item_set_current_value_text(item, profile->label);
}

void subghz_scene_receiver_config_on_enter(void* context) {
    SubGhz* subghz = context;
    subghz->rx_profile_item = NULL;
    VariableItem* item;
    uint8_t value_index;
    SubGhzSetting* setting = subghz_txrx_get_setting(subghz->txrx);
    SubGhzRadioPreset preset = subghz_txrx_get_preset(subghz->txrx);

    item = variable_item_list_add(
        subghz->variable_item_list,
        "Frequency",
        subghz_setting_get_frequency_count(setting),
        subghz_scene_receiver_config_set_frequency,
        subghz);
    value_index = subghz_scene_receiver_config_next_frequency(preset.frequency, subghz);
    scene_manager_set_scene_state(
        subghz->scene_manager, SubGhzSceneReceiverConfig, (uint32_t)item);
    variable_item_set_current_value_index(item, value_index);
    char text_buf[10] = {0};
    uint32_t frequency = subghz_setting_get_frequency(setting, value_index);
    snprintf(
        text_buf,
        sizeof(text_buf),
        "%lu.%02lu",
        frequency / 1000000,
        (frequency % 1000000) / 10000);
    variable_item_set_current_value_text(item, text_buf);

    item = variable_item_list_add(
        subghz->variable_item_list,
        "Modulation",
        subghz_scene_receiver_config_modulation_count(setting),
        subghz_scene_receiver_config_set_preset,
        subghz);
    const int actual_preset_index =
        subghz_setting_get_inx_preset_by_name(setting, furi_string_get_cstr(preset.name));
    const size_t safe_preset_index = actual_preset_index >= 0 ? (size_t)actual_preset_index : 0;
    variable_item_set_current_value_index(
        item, subghz_scene_receiver_config_visible_preset_index(setting, safe_preset_index));
    variable_item_set_current_value_text(
        item, subghz_scene_receiver_config_preset_label(setting, safe_preset_index));

    if(scene_manager_get_scene_state(subghz->scene_manager, SubGhzSceneReadRAW) !=
       SubGhzCustomEventManagerSet) {
        value_index = subghz_scene_receiver_config_get_hopping_mode(subghz);
        item = variable_item_list_add(
            subghz->variable_item_list,
            "Hopping Mode",
            SubGhzHoppingModeCount,
            subghz_scene_receiver_config_set_hopping_mode,
            subghz);
        variable_item_set_current_value_index(item, value_index);
        variable_item_set_current_value_text(item, hopping_mode_text[value_index]);

        item = variable_item_list_add(
            subghz->variable_item_list,
            "Hopping RSSI",
            RAW_THRESHOLD_RSSI_COUNT,
            subghz_scene_receiver_config_set_hopping_rssi,
            subghz);
        value_index = value_index_float(
            subghz->last_settings->hopping_threshold,
            raw_threshold_rssi_value,
            RAW_THRESHOLD_RSSI_COUNT);
        variable_item_set_current_value_index(item, value_index);
        variable_item_set_current_value_text(item, raw_threshold_rssi_text[value_index]);

        item = variable_item_list_add(
            subghz->variable_item_list,
            "Protocol Pack",
            SubGhzProtocolPackGroupCount,
            subghz_scene_receiver_config_set_protocol_pack,
            subghz);
        value_index = subghz->last_settings->protocol_pack_group;
        variable_item_set_current_value_index(item, value_index);
        variable_item_set_current_value_text(
            item, subghz_protocol_pack_group_get_name(value_index));

        item = variable_item_list_add(subghz->variable_item_list, "Pack Status", 1, NULL, NULL);
        subghz_scene_receiver_config_update_protocol_pack_status(item, subghz);

        item = variable_item_list_add(subghz->variable_item_list, "Protocols", 1, NULL, NULL);
        const uint8_t protocol_filter_count =
            subghz_last_settings_protocol_filter_count(subghz->last_settings);
        if(protocol_filter_count == 0) {
            variable_item_set_current_value_text(item, "All ON");
        } else {
            static char filter_count_text[8];
            snprintf(
                filter_count_text, sizeof(filter_count_text), "%u OFF", protocol_filter_count);
            variable_item_set_current_value_text(item, filter_count_text);
        }

        SubGhzHoppingMode hopping_mode = subghz_scene_receiver_config_get_hopping_mode(subghz);
        if(hopping_mode == SubGhzHoppingModeFrequency ||
           hopping_mode == SubGhzHoppingModeCombined) {
            variable_item_set_current_value_text(
                variable_item_list_get(subghz->variable_item_list, SubGhzSettingIndexFrequency),
                " -----");
        }
        if(hopping_mode == SubGhzHoppingModePreset || hopping_mode == SubGhzHoppingModeCombined) {
            variable_item_set_current_value_text(
                variable_item_list_get(subghz->variable_item_list, SubGhzSettingIndexModulation),
                " -----");
        }
    }

    if(scene_manager_get_scene_state(subghz->scene_manager, SubGhzSceneReadRAW) !=
       SubGhzCustomEventManagerSet) {
        item = variable_item_list_add(
            subghz->variable_item_list,
            "Bin RAW",
            COMBO_BOX_COUNT,
            subghz_scene_receiver_config_set_bin_raw,
            subghz);

        value_index = value_index_uint32(subghz->filter, bin_raw_value, COMBO_BOX_COUNT);
        variable_item_set_current_value_index(item, value_index);
        variable_item_set_current_value_text(item, combobox_text[value_index]);
    }

    if(scene_manager_get_scene_state(subghz->scene_manager, SubGhzSceneReadRAW) !=
       SubGhzCustomEventManagerSet) {
        item = variable_item_list_add(
            subghz->variable_item_list,
            "Ignore ReversRB2",
            COMBO_BOX_COUNT,
            subghz_scene_receiver_config_set_reversrb2,
            subghz);

        value_index = subghz_scene_receiver_config_ignore_filter_get_index(
            subghz->ignore_filter, SubGhzProtocolFlag_ReversRB2);
        variable_item_set_current_value_index(item, value_index);
        variable_item_set_current_value_text(item, combobox_text[value_index]);

        item = variable_item_list_add(
            subghz->variable_item_list,
            "Ignore Alarms",
            COMBO_BOX_COUNT,
            subghz_scene_receiver_config_set_alarms,
            subghz);

        value_index = subghz_scene_receiver_config_ignore_filter_get_index(
            subghz->ignore_filter, SubGhzProtocolFlag_Alarms);
        variable_item_set_current_value_index(item, value_index);
        variable_item_set_current_value_text(item, combobox_text[value_index]);

        item = variable_item_list_add(
            subghz->variable_item_list,
            "Ignore Sensors",
            COMBO_BOX_COUNT,
            subghz_scene_receiver_config_set_sensors,
            subghz);

        value_index = subghz_scene_receiver_config_ignore_filter_get_index(
            subghz->ignore_filter, SubGhzProtocolFlag_Sensors);
        variable_item_set_current_value_index(item, value_index);
        variable_item_set_current_value_text(item, combobox_text[value_index]);

        item = variable_item_list_add(
            subghz->variable_item_list,
            "Ignore Princeton",
            COMBO_BOX_COUNT,
            subghz_scene_receiver_config_set_princeton,
            subghz);

        value_index = subghz_scene_receiver_config_ignore_filter_get_index(
            subghz->ignore_filter, SubGhzProtocolFlag_Princeton);
        variable_item_set_current_value_index(item, value_index);
        variable_item_set_current_value_text(item, combobox_text[value_index]);

        item = variable_item_list_add(
            subghz->variable_item_list,
            "Ignore Nice Flor-S / Nice One",
            COMBO_BOX_COUNT,
            subghz_scene_receiver_config_set_niceflors,
            subghz);

        value_index = subghz_scene_receiver_config_ignore_filter_get_index(
            subghz->ignore_filter, SubGhzProtocolFlag_NiceFlorS);
        variable_item_set_current_value_index(item, value_index);
        variable_item_set_current_value_text(item, combobox_text[value_index]);

        item = variable_item_list_add(
            subghz->variable_item_list,
            "Delete old signals when memory is full",
            COMBO_BOX_COUNT,
            subghz_scene_receiver_config_set_delete_old_signals,
            subghz);

        value_index = subghz->last_settings->delete_old_signals;
        variable_item_set_current_value_index(item, value_index);
        variable_item_set_current_value_text(item, combobox_text[value_index]);
    }

    // Enable speaker, will send all incoming noises and signals to speaker so you can listen how your remote sounds like :)
    item = variable_item_list_add(
        subghz->variable_item_list,
        "Sound",
        COMBO_BOX_COUNT,
        subghz_scene_receiver_config_set_speaker,
        subghz);
    value_index = value_index_uint32(
        subghz_txrx_speaker_get_state(subghz->txrx), speaker_value, COMBO_BOX_COUNT);
    variable_item_set_current_value_index(item, value_index);
    variable_item_set_current_value_text(item, combobox_text[value_index]);

    if(scene_manager_get_scene_state(subghz->scene_manager, SubGhzSceneReadRAW) !=
       SubGhzCustomEventManagerSet) {
        // Reset to default
        variable_item_list_add(subghz->variable_item_list, "Reset to default", 1, NULL, NULL);

        variable_item_list_set_enter_callback(
            subghz->variable_item_list,
            subghz_scene_receiver_config_var_list_enter_callback,
            subghz);
    }
    if(scene_manager_get_scene_state(subghz->scene_manager, SubGhzSceneReadRAW) !=
       SubGhzCustomEventManagerSet) {
        // Lock keyboard
        variable_item_list_add(subghz->variable_item_list, "Lock Keyboard", 1, NULL, NULL);
        variable_item_list_set_enter_callback(
            subghz->variable_item_list,
            subghz_scene_receiver_config_var_list_enter_callback,
            subghz);
    }

    if(scene_manager_get_scene_state(subghz->scene_manager, SubGhzSceneReadRAW) ==
       SubGhzCustomEventManagerSet) {
        item = variable_item_list_add(
            subghz->variable_item_list,
            "RSSI Threshold:",
            RAW_THRESHOLD_RSSI_COUNT,
            subghz_scene_receiver_config_set_raw_threshold_rssi,
            subghz);
        value_index = value_index_float(
            subghz_threshold_rssi_get(subghz->threshold_rssi),
            raw_threshold_rssi_value,
            RAW_THRESHOLD_RSSI_COUNT);
        variable_item_set_current_value_index(item, value_index);
        variable_item_set_current_value_text(item, raw_threshold_rssi_text[value_index]);
    }
    subghz->rx_profile_item = variable_item_list_add(
        subghz->variable_item_list,
        "RX profile",
        SUBGHZ_RX_PROFILE_COUNT,
        subghz_scene_receiver_config_set_rx_profile,
        subghz);
    variable_item_set_current_value_text(subghz->rx_profile_item, "Manual");
    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdVariableItemList);
}

bool subghz_scene_receiver_config_on_event(void* context, SceneManagerEvent event) {
    SubGhz* subghz = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SubGhzCustomEventSceneSettingLock) {
            subghz_lock(subghz);
            scene_manager_previous_scene(subghz->scene_manager);
            consumed = true;
        } else if(event.event == SubGhzCustomEventSceneSettingResetToDefault) {
            scene_manager_previous_scene(subghz->scene_manager);
            consumed = true;
        }
    }
    return consumed;
}

void subghz_scene_receiver_config_on_exit(void* context) {
    SubGhz* subghz = context;
    subghz->rx_profile_item = NULL;
    variable_item_list_set_selected_item(subghz->variable_item_list, 0);
    variable_item_list_reset(subghz->variable_item_list);

    subghz_last_settings_save(subghz->last_settings);
    scene_manager_set_scene_state(
        subghz->scene_manager, SubGhzSceneReadRAW, SubGhzCustomEventManagerNoSet);
}
