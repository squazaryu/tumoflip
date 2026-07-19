#include "../subghz_i.h"
#include "subghz/types.h"
#include "../helpers/subghz_button_labels.h"
#include "../helpers/subghz_custom_event.h"
#include <lib/toolbox/value_index.h>
#include <machine/endian.h>
#include <toolbox/strint.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <string.h>
#include <stdio.h>

#define TAG "SubGhzSceneSignalSettings"

static uint32_t counter_mode = 0xff;
static uint32_t counter32 = 0x0;
static uint16_t counter16 = 0x0;
static uint8_t cnt_byte_count = 0;
static uint8_t* cnt_byte_ptr = NULL;
static uint32_t counter_value = 0;
static uint32_t counter_mask = 0xffffffffUL;

static FuriString* byte_input_text;

static uint8_t button = 0x0;
static uint8_t btn_byte_count = 1;
static uint8_t* btn_byte_ptr = NULL;

static uint8_t submenu_called = 0;
static bool button_uses_custom_btn = false;
static uint8_t button_custom_id = SUBGHZ_CUSTOM_BTN_OK;

static bool subghz_scene_signal_settings_rebuild_save_reload(
    SubGhz* subghz,
    bool use_custom_btn,
    uint8_t custom_btn_id);

enum {
    SignalSettingsIndexCounterMode,
    SignalSettingsIndexCounter,
    SignalSettingsIndexButton,
};

enum {
    SignalSettingsCounterStepDown,
    SignalSettingsCounterStepValue,
    SignalSettingsCounterStepUp,
    SignalSettingsCounterStepCount,
};

#define COUNTER_MODE_COUNT 8
static const char* const counter_mode_text[COUNTER_MODE_COUNT] = {
    "System",
    "Mode 1",
    "Mode 2",
    "Mode 3",
    "Mode 4",
    "Mode 5",
    "Mode 6",
    "Mode 7",
};

static const int32_t counter_mode_value[COUNTER_MODE_COUNT] = {
    0,
    1,
    2,
    3,
    4,
    5,
    6,
    7,
};

static const uint8_t button_value[] = {
    SUBGHZ_CUSTOM_BTN_OK,
    SUBGHZ_CUSTOM_BTN_UP,
    SUBGHZ_CUSTOM_BTN_DOWN,
    SUBGHZ_CUSTOM_BTN_LEFT,
    SUBGHZ_CUSTOM_BTN_RIGHT,
    5,
    6,
    7,
};

#define BUTTON_VALUE_COUNT SUBGHZ_BUTTON_LABEL_COUNT

static const char* button_labels[BUTTON_VALUE_COUNT] = {
    "Original",
    "Up",
    "Down",
    "Left",
    "Right",
    "Button 5",
    "Button 6",
    "Button 7",
};

typedef struct {
    char* name;
    uint8_t mode_count;
} Protocols;

// List of protocols names and appropriate CounterMode counts
static Protocols protocols[] = {
    {"Nice FloR-S", 3},
    {"CAME Atomo", 4},
    {"Alutech AT-4N", 3},
    {"KeeLoq", 8},
    {"Phoenix_V2", 3},
};

#define PROTOCOLS_COUNT (sizeof(protocols) / sizeof(Protocols))

static void subghz_scene_signal_settings_reset_button_labels(void) {
    subghz_button_labels_reset(button_labels);
}

static void subghz_scene_signal_settings_apply_button_labels(const char* protocol) {
    subghz_button_labels_apply_protocol(protocol, button_labels);
}

static const char* subghz_scene_signal_settings_get_button_label(uint8_t custom_btn_id) {
    return subghz_button_labels_get(
        button_labels, custom_btn_id, subghz_custom_btn_get_original());
}

static bool subghz_scene_signal_settings_update_uint32_field(
    FlipperFormat* fff,
    const char* key,
    uint32_t value) {
    flipper_format_rewind(fff);
    return flipper_format_insert_or_update_uint32(fff, key, &value, 1);
}

static uint32_t subghz_scene_signal_settings_counter_get_mask(void) {
    uint8_t bits = subghz_block_generic_global.cnt_length_bit;
    if((bits == 0) || (bits >= 32)) {
        return 0xffffffffUL;
    }
    return (1UL << bits) - 1UL;
}

static void subghz_scene_signal_settings_counter_sync_byte_input(void) {
    if(cnt_byte_count == 4) {
        counter32 = __bswap32(counter_value);
        cnt_byte_ptr = (uint8_t*)&counter32;
    } else if(cnt_byte_count == 2) {
        counter16 = __bswap16((uint16_t)counter_value);
        cnt_byte_ptr = (uint8_t*)&counter16;
    }
}

static void subghz_scene_signal_settings_counter_set_value(uint32_t value) {
    counter_value = value & counter_mask;
    subghz_scene_signal_settings_counter_sync_byte_input();
}

static void subghz_scene_signal_settings_counter_format(FuriString* text) {
    if(cnt_byte_count == 4) {
        furi_string_printf(text, "%lX", (unsigned long)counter_value);
    } else {
        furi_string_printf(text, "%X", (unsigned int)(counter_value & 0xffffU));
    }
}

static void subghz_scene_signal_settings_counter_update_item(VariableItem* item) {
    char text[9] = {0};
    variable_item_set_current_value_index(item, SignalSettingsCounterStepValue);
    if(cnt_byte_count == 4) {
        snprintf(text, sizeof(text), "%lX", (unsigned long)counter_value);
    } else {
        snprintf(text, sizeof(text), "%X", (unsigned int)(counter_value & 0xffffU));
    }
    variable_item_set_current_value_text(item, text);
}

static bool subghz_scene_signal_settings_counter_save(SubGhz* subghz) {
    FlipperFormat* fff = subghz_txrx_get_fff_data(subghz->txrx);
    if(!subghz_scene_signal_settings_update_uint32_field(fff, "Cnt", counter_value)) {
        FURI_LOG_E(TAG, "Error update/insert Cnt value");
        dialog_message_show_storage_error(subghz->dialogs, "Cannot save\ncounter");
        return false;
    }

    subghz_block_generic_global_counter_override_set(counter_value);
    return subghz_scene_signal_settings_rebuild_save_reload(subghz, false, SUBGHZ_CUSTOM_BTN_OK);
}

static bool subghz_scene_signal_settings_hex_digit(char c, uint8_t* value) {
    if(c >= '0' && c <= '9') {
        *value = c - '0';
        return true;
    } else if(c >= 'a' && c <= 'f') {
        *value = c - 'a' + 10;
        return true;
    } else if(c >= 'A' && c <= 'F') {
        *value = c - 'A' + 10;
        return true;
    }
    return false;
}

static bool subghz_scene_signal_settings_parse_hex_field(
    const char* text,
    const char* marker,
    uint32_t* value,
    uint8_t* length_bit) {
    const char* field = strstr(text, marker);
    if(!field) return false;

    field += strlen(marker);
    while(*field == ' ' || *field == '\t') {
        field++;
    }

    uint32_t parsed = 0;
    uint8_t digits = 0;
    uint8_t digit_value = 0;
    while(digits < 8 && subghz_scene_signal_settings_hex_digit(*field, &digit_value)) {
        parsed = (parsed << 4) | digit_value;
        digits++;
        field++;
    }

    if(digits == 0) return false;

    *value = parsed;
    *length_bit = digits * 4;
    return true;
}

static void subghz_scene_signal_settings_apply_text_fallback(FuriString* decoded_text) {
    uint32_t value = 0;
    uint8_t length_bit = 0;
    const char* text = furi_string_get_cstr(decoded_text);

    if(!subghz_block_generic_global.cnt_is_available &&
       subghz_scene_signal_settings_parse_hex_field(text, "Cnt:", &value, &length_bit)) {
        subghz_block_generic_global.cnt_is_available = true;
        subghz_block_generic_global.current_cnt = value;
        subghz_block_generic_global.cnt_length_bit = length_bit;
    }

    if(!subghz_block_generic_global.btn_is_available &&
       subghz_scene_signal_settings_parse_hex_field(text, "Btn:", &value, &length_bit)) {
        subghz_block_generic_global.btn_is_available = true;
        subghz_block_generic_global.current_btn = value & 0xFF;
        subghz_block_generic_global.btn_length_bit = length_bit > 8 ? 8 : length_bit;
    }
}

static void subghz_scene_signal_settings_apply_file_fallback(FlipperFormat* fff) {
    uint32_t value = 0;

    if(!subghz_block_generic_global.cnt_is_available) {
        flipper_format_rewind(fff);
        if(flipper_format_read_uint32(fff, "Cnt", &value, 1)) {
            subghz_block_generic_global.cnt_is_available = true;
            subghz_block_generic_global.current_cnt = value;
            subghz_block_generic_global.cnt_length_bit = 32;
        }
    }

    if(!subghz_block_generic_global.btn_is_available) {
        flipper_format_rewind(fff);
        if(flipper_format_read_uint32(fff, "Btn", &value, 1)) {
            subghz_block_generic_global.btn_is_available = true;
            subghz_block_generic_global.current_btn = value & 0xFF;
            subghz_block_generic_global.btn_length_bit = 8;
        }
    }
}

static bool subghz_scene_signal_settings_rebuild_save_reload(
    SubGhz* subghz,
    bool use_custom_btn,
    uint8_t custom_btn_id) {
    const char* file_path = furi_string_get_cstr(subghz->file_path);
    FlipperFormat* fff = subghz_txrx_get_fff_data(subghz->txrx);

    bool updated = false;
    int32_t counter_mult = furi_hal_subghz_get_rolling_counter_mult();
    furi_hal_subghz_set_rolling_counter_mult(0);

    if(use_custom_btn) {
        subghz_custom_btn_set(custom_btn_id);
    }

    do {
        if(!subghz_txrx_rebuild_from_fff(subghz->txrx, fff)) {
            FURI_LOG_E(TAG, "Error rebuilding protocol data");
            break;
        }
        if(!subghz_save_protocol_to_file(subghz, fff, file_path)) {
            FURI_LOG_E(TAG, "Error saving edited signal");
            break;
        }
        if(!subghz_key_load(subghz, file_path, false)) {
            FURI_LOG_E(TAG, "Error reloading edited signal");
            break;
        }
        updated = true;
    } while(false);

    furi_hal_subghz_set_rolling_counter_mult(counter_mult);

    if(!updated) {
        dialog_message_show_storage_error(subghz->dialogs, "Cannot save\nsignal");
    }
    return updated;
}

void subghz_scene_signal_settings_counter_mode_changed(VariableItem* item) {
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, counter_mode_text[index]);
    counter_mode = counter_mode_value[index];

    SubGhz* subghz = variable_item_get_context(item);
    const char* file_path = furi_string_get_cstr(subghz->file_path);

    furi_assert(subghz);
    furi_assert(file_path);

    // update file every time when we change mode
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* fff_data_file = flipper_format_file_alloc(storage);

    // check is the file available for update/insert CounterMode value
    if(flipper_format_file_open_existing(fff_data_file, file_path)) {
        if(flipper_format_insert_or_update_uint32(fff_data_file, "CounterMode", &counter_mode, 1)) {
            FURI_LOG_D(TAG, "Successfully updated/inserted CounterMode value %li", counter_mode);
        } else {
            FURI_LOG_E(TAG, "Error update/insert CounterMode value");
        }
    } else {
        FURI_LOG_E(TAG, "Error open file %s for writing", file_path);
    }

    flipper_format_file_close(fff_data_file);
    flipper_format_free(fff_data_file);
    furi_record_close(RECORD_STORAGE);

    // we need to reload file after editing it
    if(subghz_key_load(subghz, file_path, false)) {
        FURI_LOG_D(TAG, "Subghz file was successfully reloaded");
    } else {
        FURI_LOG_E(TAG, "Error reloading subghz file");
    }
}

void subghz_scene_signal_settings_counter_changed(VariableItem* item) {
    if(!cnt_byte_ptr || cnt_byte_count == 0) return;

    uint8_t index = variable_item_get_current_value_index(item);
    if(index == SignalSettingsCounterStepValue) {
        subghz_scene_signal_settings_counter_update_item(item);
        return;
    }

    if(index == SignalSettingsCounterStepUp) {
        subghz_scene_signal_settings_counter_set_value(counter_value + 1);
    } else {
        subghz_scene_signal_settings_counter_set_value(counter_value - 1);
    }

    subghz_scene_signal_settings_counter_update_item(item);

    SubGhz* subghz = variable_item_get_context(item);
    furi_assert(subghz);
    subghz_scene_signal_settings_counter_save(subghz);
}

void subghz_scene_signal_settings_button_changed(VariableItem* item) {
    uint8_t index = variable_item_get_current_value_index(item);
    if(index >= BUTTON_VALUE_COUNT) index = 0;

    button_custom_id = button_value[index];
    variable_item_set_current_value_text(
        item, subghz_scene_signal_settings_get_button_label(button_custom_id));

    if(!button_uses_custom_btn) return;

    SubGhz* subghz = variable_item_get_context(item);
    furi_assert(subghz);

    subghz_scene_signal_settings_rebuild_save_reload(subghz, true, button_custom_id);
}

void subghz_scene_signal_settings_byte_input_callback(void* context) {
    SubGhz* subghz = context;
    view_dispatcher_send_custom_event(subghz->view_dispatcher, SubGhzCustomEventByteInputDone);
}

void subghz_scene_signal_settings_variable_item_list_enter_callback(void* context, uint32_t index) {
    SubGhz* subghz = context;

    // when we click OK on "Edit counter" item
    if(index == SignalSettingsIndexCounter) {
        if(!cnt_byte_ptr || cnt_byte_count == 0) return;
        submenu_called = 1;
        furi_string_set_str(byte_input_text, "Enter ");
        furi_string_cat_printf(byte_input_text, "%i", subghz_block_generic_global.cnt_length_bit);
        furi_string_cat_str(byte_input_text, "-bits counter in HEX");

        // Setup byte_input view
        ByteInput* byte_input = subghz->byte_input;
        byte_input_set_header_text(byte_input, furi_string_get_cstr(byte_input_text));

        byte_input_set_result_callback(
            byte_input,
            subghz_scene_signal_settings_byte_input_callback,
            NULL,
            subghz,
            cnt_byte_ptr,
            cnt_byte_count);
        view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdByteInput);
    }
    // when we click OK on "Edit button" item
    if(index == SignalSettingsIndexButton && !button_uses_custom_btn) {
        if(!btn_byte_ptr || btn_byte_count == 0) return;
        submenu_called = 2;
        furi_string_set_str(byte_input_text, "Enter ");
        furi_string_cat_printf(byte_input_text, "%i", subghz_block_generic_global.btn_length_bit);
        furi_string_cat_str(byte_input_text, "-bits button in HEX");

        // Setup byte_input view
        ByteInput* byte_input = subghz->byte_input;
        byte_input_set_header_text(byte_input, furi_string_get_cstr(byte_input_text));

        byte_input_set_result_callback(
            byte_input,
            subghz_scene_signal_settings_byte_input_callback,
            NULL,
            subghz,
            btn_byte_ptr,
            btn_byte_count);
        view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdByteInput);
    }
}

void subghz_scene_signal_settings_on_enter(void* context) {
    SubGhz* subghz = context;

    counter32 = 0;
    counter16 = 0;
    cnt_byte_count = 0;
    cnt_byte_ptr = NULL;
    counter_value = 0;
    counter_mask = 0xffffffffUL;
    button = 0;
    btn_byte_count = 1;
    btn_byte_ptr = NULL;
    submenu_called = 0;
    button_uses_custom_btn = false;
    button_custom_id = SUBGHZ_CUSTOM_BTN_OK;
    subghz_block_generic_global_reset(NULL);
    subghz_custom_btns_reset();
    subghz_scene_signal_settings_reset_button_labels();

    // ### Counter mode section ###

    // When we open saved file we do some check and fill up subghz->file_path.
    // So now we use it to check is there CounterMode in file or not
    const char* file_path = furi_string_get_cstr(subghz->file_path);

    furi_assert(subghz);
    furi_assert(file_path);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* fff_data_file = flipper_format_file_alloc(storage);
    FuriString* tmp_text = furi_string_alloc_set_str("");
    FuriString* protocol_name = furi_string_alloc();

    uint32_t tmp_counter_mode = 0;
    counter_mode = 0xff;
    uint8_t mode_count = 1;

    // Open file and check is it contains allowed protocols and CounterMode variable - if not then CounterMode will stay 0xff
    // if file contain allowed protocol but not contain CounterMode value then setup default CounterMode value = 0 and available CounterMode count for this protocol
    // if file contain CounterMode value then load it
    if(!flipper_format_file_open_existing(fff_data_file, file_path)) {
        FURI_LOG_E(TAG, "Error open file %s", file_path);
    } else {
        flipper_format_read_string(fff_data_file, "Protocol", tmp_text);
        furi_string_set(protocol_name, tmp_text);
        // compare available protocols names, load CounterMode value from file and setup variable_item_list values_count
        for(uint8_t i = 0; i < PROTOCOLS_COUNT; i++) {
            if(!strcmp(furi_string_get_cstr(tmp_text), protocols[i].name)) {
                mode_count = protocols[i].mode_count;
                if(flipper_format_read_uint32(fff_data_file, "CounterMode", &tmp_counter_mode, 1)) {
                    counter_mode = (uint8_t)tmp_counter_mode;
                } else {
                    counter_mode = 0;
                }
            }
        }
    }
    FURI_LOG_D(TAG, "Loaded CounterMode value %li", counter_mode);

    flipper_format_file_close(fff_data_file);
    flipper_format_free(fff_data_file);
    furi_record_close(RECORD_STORAGE);

    byte_input_text = furi_string_alloc_set_str("Enter ");
    bool counter_not_available = true;
    bool button_not_available = true;

    //Create and Enable/Disable variable_item_list depending on current values
    VariableItemList* variable_item_list = subghz->variable_item_list;
    int32_t value_index;
    VariableItem* item;

    variable_item_list_set_enter_callback(
        variable_item_list,
        subghz_scene_signal_settings_variable_item_list_enter_callback,
        subghz);

    item = variable_item_list_add(
        variable_item_list,
        "Counter Mode",
        mode_count,
        subghz_scene_signal_settings_counter_mode_changed,
        subghz);
    value_index = value_index_int32(counter_mode, counter_mode_value, mode_count);
    variable_item_set_current_value_index(item, value_index);
    variable_item_set_current_value_text(item, counter_mode_text[value_index]);
    variable_item_set_locked(item, (counter_mode == 0xff), "Not available\nfor this\nprotocol !");
    //

    SubGhzProtocolDecoderBase* decoder = subghz_txrx_get_decoder(subghz->txrx);

    // deserialaze and decode loaded sugbhz file and push data to subghz_block_generic_global variable
    if(subghz_protocol_decoder_base_deserialize(decoder, subghz_txrx_get_fff_data(subghz->txrx)) ==
       SubGhzProtocolStatusOk) {
        subghz_scene_signal_settings_apply_button_labels(furi_string_get_cstr(protocol_name));
        subghz_protocol_decoder_base_get_string(decoder, tmp_text);
        subghz_scene_signal_settings_apply_text_fallback(tmp_text);
        subghz_scene_signal_settings_apply_file_fallback(subghz_txrx_get_fff_data(subghz->txrx));
    } else {
        FURI_LOG_E(TAG, "Cant deserialize this subghz file");
    }

    // ### Counter edit section ###

    if(!subghz_block_generic_global.cnt_is_available) {
        counter_mode = 0xff;
        furi_string_set_str(tmp_text, "-");
        FURI_LOG_D(TAG, "Counter mode and edit not available for this protocol");
    } else {
        counter_not_available = false;
        counter_mask = subghz_scene_signal_settings_counter_get_mask();

        // ByteInput stores the visible hex value as big-endian bytes.
        if(subghz_block_generic_global.cnt_length_bit > 16) {
            cnt_byte_count = 4;
        } else {
            cnt_byte_count = 2;
        }
        subghz_scene_signal_settings_counter_set_value(subghz_block_generic_global.current_cnt);
        subghz_scene_signal_settings_counter_format(tmp_text);
    }

    item = variable_item_list_add(
        variable_item_list,
        "Edit Counter",
        counter_not_available ? 1 : SignalSettingsCounterStepCount,
        counter_not_available ? NULL : subghz_scene_signal_settings_counter_changed,
        subghz);
    variable_item_set_current_value_index(
        item, counter_not_available ? 0 : SignalSettingsCounterStepValue);
    variable_item_set_current_value_text(item, furi_string_get_cstr(tmp_text));
    variable_item_set_locked(item, (counter_not_available), "Not available\nfor this\nprotocol !");
    //

    // ### Button edit section ###

    if(subghz_custom_btn_is_allowed()) {
        uint8_t max_custom_btn = subghz_custom_btn_get_max();
        uint8_t custom_button_count = max_custom_btn + 1;
        if(custom_button_count > BUTTON_VALUE_COUNT) custom_button_count = BUTTON_VALUE_COUNT;
        button_uses_custom_btn = custom_button_count > 1;
    }

    if(button_uses_custom_btn) {
        button_not_available = false;
        furi_string_set_str(
            tmp_text, subghz_scene_signal_settings_get_button_label(SUBGHZ_CUSTOM_BTN_OK));
    } else if(!subghz_block_generic_global.btn_is_available) {
        furi_string_set_str(tmp_text, "-");
        FURI_LOG_D(TAG, "Button edit not available for this protocol");
    } else {
        button_not_available = false;
        button = subghz_block_generic_global.current_btn;
        btn_byte_ptr = (uint8_t*)&button;
        furi_string_printf(tmp_text, "%X", button);
    }

    uint8_t button_count = 1;
    if(button_uses_custom_btn) {
        button_count = subghz_custom_btn_get_max() + 1;
        if(button_count > BUTTON_VALUE_COUNT) button_count = BUTTON_VALUE_COUNT;
    }
    item = variable_item_list_add(
        variable_item_list,
        button_uses_custom_btn ? "Button" : "Edit Button",
        button_count,
        button_uses_custom_btn ? subghz_scene_signal_settings_button_changed : NULL,
        subghz);
    variable_item_set_current_value_index(item, 0);
    variable_item_set_current_value_text(item, furi_string_get_cstr(tmp_text));
    variable_item_set_locked(item, (button_not_available), "Not available\nfor this\nprotocol !");
    //

    furi_string_free(tmp_text);
    furi_string_free(protocol_name);

    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdVariableItemList);
}

bool subghz_scene_signal_settings_on_event(void* context, SceneManagerEvent event) {
    SubGhz* subghz = context;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SubGhzCustomEventByteInputDone) {
            FlipperFormat* fff = subghz_txrx_get_fff_data(subghz->txrx);

            switch(submenu_called) {
            // edit counter
            case 1:
                switch(cnt_byte_count) {
                case 2:
                    counter16 = __bswap16(counter16);
                    subghz_scene_signal_settings_counter_set_value(counter16);
                    subghz_scene_signal_settings_counter_save(subghz);
                    break;
                case 4:
                    counter32 = __bswap32(counter32);
                    subghz_scene_signal_settings_counter_set_value(counter32);
                    subghz_scene_signal_settings_counter_save(subghz);
                    break;
                default:
                    break;
                }
                break;
            // edit button
            case 2:
                subghz_scene_signal_settings_update_uint32_field(fff, "Btn", button);
                subghz_block_generic_global_button_override_set(button);
                subghz_scene_signal_settings_rebuild_save_reload(
                    subghz, false, SUBGHZ_CUSTOM_BTN_OK);
                break;

            default:
                break;
            }

            scene_manager_previous_scene(subghz->scene_manager);
            return true;
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        scene_manager_previous_scene(subghz->scene_manager);
        return true;
    }
    return false;
}

void subghz_scene_signal_settings_on_exit(void* context) {
    SubGhz* subghz = context;

    furi_assert(subghz);

    // Clear views
    variable_item_list_set_selected_item(subghz->variable_item_list, 0);
    variable_item_list_reset(subghz->variable_item_list);
    byte_input_set_result_callback(subghz->byte_input, NULL, NULL, NULL, NULL, 0);
    byte_input_set_header_text(subghz->byte_input, "");
    furi_string_free(byte_input_text);
    subghz_custom_btns_reset();
}
