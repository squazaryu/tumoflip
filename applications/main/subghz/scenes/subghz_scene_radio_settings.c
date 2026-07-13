#include "../subghz_i.h"
#include "../helpers/subghz_custom_event.h"
#include <lib/toolbox/value_index.h>
#include <applications/drivers/subghz/cc1101_ext/cc1101_ext_interconnect.h>

#define RADIO_DEVICE_COUNT 2
const char* const radio_device_text[RADIO_DEVICE_COUNT] = {
    "Internal",
    "External",
};

const uint32_t radio_device_value[RADIO_DEVICE_COUNT] = {
    SubGhzRadioDeviceTypeInternal,
    SubGhzRadioDeviceTypeExternalCC1101,
};

#define RX_MODE_COUNT 2
const char* const rx_mode_text[RX_MODE_COUNT] = {
    "AUTO",
    "DUAL",
};

typedef enum {
    SubGhzRadioSettingsIndexModule,
    SubGhzRadioSettingsIndexRxMode,
} SubGhzRadioSettingsIndex;

static void subghz_scene_radio_settings_sync_mode(SubGhz* subghz) {
    VariableItem* mode_item =
        variable_item_list_get(subghz->variable_item_list, SubGhzRadioSettingsIndexRxMode);
    const uint8_t mode_index =
        subghz_txrx_radio_device_get(subghz->txrx) == SubGhzRadioDeviceTypeAuto ? 1U : 0U;
    variable_item_set_current_value_index(mode_item, mode_index);
    variable_item_set_current_value_text(mode_item, rx_mode_text[mode_index]);
}

static void subghz_scene_radio_settings_sync_module(SubGhz* subghz) {
    VariableItem* module_item =
        variable_item_list_get(subghz->variable_item_list, SubGhzRadioSettingsIndexModule);
    const uint8_t module_index =
        subghz_txrx_radio_device_get(subghz->txrx) == SubGhzRadioDeviceTypeInternal ? 0U : 1U;
    variable_item_set_current_value_index(module_item, module_index);
    variable_item_set_current_value_text(module_item, radio_device_text[module_index]);
}

#define ON_OFF_COUNT 2
const char* const on_off_text[ON_OFF_COUNT] = {
    "OFF",
    "ON",
};

#define DEBUG_P_COUNT 2
const char* const debug_pin_text[DEBUG_P_COUNT] = {
    "OFF",
    "17(1W)",
};

#define DEBUG_COUNTER_COUNT 17
const char* const debug_counter_text[DEBUG_COUNTER_COUNT] = {
    "+1",
    "+2",
    "+3",
    "+4",
    "+5",
    "+10",
    "+50",
    "OVFL",
    "OFEX",
    "No",
    "-1",
    "-2",
    "-3",
    "-4",
    "-5",
    "-10",
    "-50",
};
const int32_t debug_counter_val[DEBUG_COUNTER_COUNT] = {
    1,
    2,
    3,
    4,
    5,
    10,
    50,
    65535,
    -2147483647,
    0,
    -1,
    -2,
    -3,
    -4,
    -5,
    -10,
    -50,
};

//TX Power
#define TX_POWER_COUNT 9
const char* const tx_power_text[TX_POWER_COUNT] = {
    "Preset",
    "10dBm +",
    "7dBm",
    "5dBm",
    "0dBm",
    "-10dBm",
    "-15dBm",
    "-20dBm",
    "-30dBm",
};

static void subghz_scene_radio_settings_set_device(VariableItem* item) {
    SubGhz* subghz = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);

    if(radio_device_value[index] != SubGhzRadioDeviceTypeInternal &&
       !subghz_txrx_radio_device_is_external_connected(
           subghz->txrx, SUBGHZ_DEVICE_CC1101_EXT_NAME)) {
        index = 0;
    }
    const SubGhzRadioDeviceType actual =
        subghz_txrx_radio_device_set(subghz->txrx, radio_device_value[index]);
    index = value_index_uint32(actual, radio_device_value, RADIO_DEVICE_COUNT);
    variable_item_set_current_value_index(item, index);
    variable_item_set_current_value_text(item, radio_device_text[index]);
    subghz_scene_radio_settings_sync_mode(subghz);
}

static void subghz_scene_radio_settings_set_rx_mode(VariableItem* item) {
    SubGhz* subghz = variable_item_get_context(item);
    const uint8_t requested_index = variable_item_get_current_value_index(item);
    const SubGhzRadioDeviceType current = subghz_txrx_radio_device_get(subghz->txrx);
    const SubGhzRadioDeviceType single_device = current == SubGhzRadioDeviceTypeInternal ?
                                                    SubGhzRadioDeviceTypeInternal :
                                                    SubGhzRadioDeviceTypeExternalCC1101;
    const SubGhzRadioDeviceType actual = subghz_txrx_radio_device_set(
        subghz->txrx, requested_index == 1U ? SubGhzRadioDeviceTypeAuto : single_device);

    const uint8_t actual_index = actual == SubGhzRadioDeviceTypeAuto ? 1U : 0U;
    variable_item_set_current_value_index(item, actual_index);
    variable_item_set_current_value_text(item, rx_mode_text[actual_index]);
    subghz_scene_radio_settings_sync_module(subghz);
}

static void subghz_scene_radio_settings_set_tx_power(VariableItem* item) {
    SubGhz* subghz = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);

    //Update the Menu Item on screen
    variable_item_set_current_value_text(item, tx_power_text[index]);

    //Set TX power and remember setting
    subghz->last_settings->tx_power = subghz->tx_power = index;

    //Save the settings now, this is the convention here!
    subghz_last_settings_save(subghz->last_settings);
}

static void subghz_scene_receiver_config_set_debug_pin(VariableItem* item) {
    SubGhz* subghz = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);

    variable_item_set_current_value_text(item, debug_pin_text[index]);

    subghz_txrx_set_debug_pin_state(subghz->txrx, index == 1);
}

static void subghz_scene_reciever_config_set_ext_amp_leds_control(VariableItem* item) {
    SubGhz* subghz = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, on_off_text[index]);
    subghz->last_settings->leds_and_amp = index == 1;
    // Set globally in furi hal
    furi_hal_subghz_set_ext_leds_and_amp(subghz->last_settings->leds_and_amp);
    subghz_last_settings_save(subghz->last_settings);
    // reinit external device
    const SubGhzRadioDeviceType current = subghz_txrx_radio_device_get(subghz->txrx);
    if(current != SubGhzRadioDeviceTypeInternal) {
        subghz_txrx_radio_device_set(subghz->txrx, SubGhzRadioDeviceTypeInternal);
        subghz_txrx_radio_device_set(subghz->txrx, current);
    }
}

static void subghz_scene_receiver_config_set_debug_counter(VariableItem* item) {
    uint8_t index = variable_item_get_current_value_index(item);

    variable_item_set_current_value_text(item, debug_counter_text[index]);
    furi_hal_subghz_set_rolling_counter_mult(debug_counter_val[index]);
}

static void subghz_scene_receiver_config_set_timestamp_file_names(VariableItem* item) {
    SubGhz* subghz = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);

    variable_item_set_current_value_text(item, on_off_text[index]);

    subghz->last_settings->protocol_file_names = (index == 1);
    subghz_last_settings_save(subghz->last_settings);
}

void subghz_scene_radio_settings_on_enter(void* context) {
    SubGhz* subghz = context;

    VariableItemList* variable_item_list = subghz->variable_item_list;
    int32_t value_index;
    VariableItem* item;

    uint8_t value_count_device = RADIO_DEVICE_COUNT;
    if(subghz_txrx_radio_device_get(subghz->txrx) == SubGhzRadioDeviceTypeInternal &&
       !subghz_txrx_radio_device_is_external_connected(subghz->txrx, SUBGHZ_DEVICE_CC1101_EXT_NAME))
        value_count_device = 1; // Only 1 item if external disconnected
    item = variable_item_list_add(
        subghz->variable_item_list,
        "Module",
        value_count_device,
        subghz_scene_radio_settings_set_device,
        subghz);
    const SubGhzRadioDeviceType current_device = subghz_txrx_radio_device_get(subghz->txrx);
    value_index = current_device == SubGhzRadioDeviceTypeInternal ? 0 : 1;
    variable_item_set_current_value_index(item, value_index);
    variable_item_set_current_value_text(item, radio_device_text[value_index]);

    const bool external_connected = subghz_txrx_radio_device_is_external_connected(
        subghz->txrx, SUBGHZ_DEVICE_CC1101_EXT_NAME);
    item = variable_item_list_add(
        subghz->variable_item_list,
        "RX Mode",
        external_connected ? RX_MODE_COUNT : 1U,
        subghz_scene_radio_settings_set_rx_mode,
        subghz);
    value_index = subghz_txrx_radio_device_get(subghz->txrx) == SubGhzRadioDeviceTypeAuto ? 1 : 0;
    variable_item_set_current_value_index(item, value_index);
    variable_item_set_current_value_text(item, rx_mode_text[value_index]);

    //Add TX Power
    item = variable_item_list_add(
        subghz->variable_item_list,
        "TX Power",
        TX_POWER_COUNT,
        subghz_scene_radio_settings_set_tx_power,
        subghz);

    value_index = subghz->tx_power;
    variable_item_set_current_value_index(item, value_index);
    variable_item_set_current_value_text(item, tx_power_text[value_index]);

    item = variable_item_list_add(
        variable_item_list,
        "Protocol Names",
        ON_OFF_COUNT,
        subghz_scene_receiver_config_set_timestamp_file_names,
        subghz);
    value_index = subghz->last_settings->protocol_file_names;
    variable_item_set_current_value_index(item, value_index);
    variable_item_set_current_value_text(item, on_off_text[value_index]);

    item = variable_item_list_add(
        variable_item_list,
        "Counter Incr.",
        furi_hal_rtc_is_flag_set(FuriHalRtcFlagDebug) ? DEBUG_COUNTER_COUNT : 3,
        subghz_scene_receiver_config_set_debug_counter,
        subghz);
    value_index = value_index_int32(
        furi_hal_subghz_get_rolling_counter_mult(),
        debug_counter_val,
        furi_hal_rtc_is_flag_set(FuriHalRtcFlagDebug) ? DEBUG_COUNTER_COUNT : 3);
    furi_hal_subghz_set_rolling_counter_mult(debug_counter_val[value_index]);

    variable_item_set_current_value_index(item, value_index);
    variable_item_set_current_value_text(item, debug_counter_text[value_index]);

    if(furi_hal_rtc_is_flag_set(FuriHalRtcFlagDebug)) {
        item = variable_item_list_add(
            variable_item_list,
            "Ext Amp & LEDs",
            ON_OFF_COUNT,
            subghz_scene_reciever_config_set_ext_amp_leds_control,
            subghz);
        value_index = subghz->last_settings->leds_and_amp ? 1 : 0;
        variable_item_set_current_value_index(item, value_index);
        variable_item_set_current_value_text(item, on_off_text[value_index]);

        item = variable_item_list_add(
            variable_item_list,
            "Debug Pin",
            DEBUG_P_COUNT,
            subghz_scene_receiver_config_set_debug_pin,
            subghz);
        value_index = subghz_txrx_get_debug_pin_state(subghz->txrx);
        variable_item_set_current_value_index(item, value_index);
        variable_item_set_current_value_text(item, debug_pin_text[value_index]);
    }

    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdVariableItemList);
}

bool subghz_scene_radio_settings_on_event(void* context, SceneManagerEvent event) {
    SubGhz* subghz = context;
    UNUSED(subghz);
    UNUSED(event);

    return false;
}

void subghz_scene_radio_settings_on_exit(void* context) {
    SubGhz* subghz = context;
    variable_item_list_set_selected_item(subghz->variable_item_list, 0);
    variable_item_list_reset(subghz->variable_item_list);
}
