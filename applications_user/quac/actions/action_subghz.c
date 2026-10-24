// Methods for Sub-GHz transmission

#include <flipper_format/flipper_format_i.h>

#include "helpers/subghz_txrx.h"
#include <lib/subghz/blocks/generic.h>
#include <signal_file/signal_file.h>

#include "action_i.h"
#include "quac.h"

typedef struct SubGhzNeedSaveContext {
    App* app;
    SubGhzTxRx* txrx;
    const FuriString* file_path;
    FuriString* error;
} SubGhzNeedSaveContext;

void action_subghz_need_save_callback(void* context) {
    FURI_LOG_I(TAG, "Saving updated subghz signal");
    SubGhzNeedSaveContext* savectx = (SubGhzNeedSaveContext*)context;
    FlipperFormat* ff = subghz_txrx_get_fff_data(savectx->txrx);

    Stream* ff_stream = flipper_format_get_raw_stream(ff);
    if(!signal_file_update(
           savectx->app->storage, ff_stream, furi_string_get_cstr(savectx->file_path))) {
        FURI_LOG_E(TAG, "Signal save failed; keep .tumo-bak if present");
        furi_string_set(savectx->error, "Sub-GHz: Save failed. Keep .tumo-bak if present.");
    }
}

static bool
    quac_action_wait_tx(QuacActionWait* wait, SubGhzTxRx* txrx, bool is_raw, uint32_t duration_ms) {
    const uint32_t start = furi_get_tick();
    while(!subghz_txrx_tx_is_complete(txrx)) {
        if(!is_raw && subghz_block_generic_global.endless_tx &&
           (uint32_t)(furi_get_tick() - start) >= furi_ms_to_ticks(duration_ms)) {
            subghz_block_generic_global.endless_tx = false;
        }
        if(!quac_action_wait_run(wait, 5)) return false;
    }
    return true;
}

void action_subghz_tx(void* context, const FuriString* action_path, FuriString* error) {
    App* app = context;
    const char* file_name = furi_string_get_cstr(action_path);

    FlipperFormat* fff_data_file = flipper_format_file_alloc(app->storage);

    SubGhzTxRx* txrx = subghz_txrx_alloc();

    SubGhzNeedSaveContext save_context = {app, txrx, action_path, error};
    subghz_txrx_set_need_save_callback(txrx, action_subghz_need_save_callback, &save_context);

    Stream* fff_data_stream = flipper_format_get_raw_stream(subghz_txrx_get_fff_data(txrx));
    stream_clean(fff_data_stream);

    FuriString* preset_name = furi_string_alloc();
    FuriString* protocol_name = furi_string_alloc();
    bool is_raw = false;
    bool loaded = false;

    FuriString* temp_str;
    temp_str = furi_string_alloc();
    uint32_t temp_data32;

    uint32_t frequency = 0;

    FURI_LOG_I(TAG, "SUBGHZ: Action starting...");

    do {
        if(!flipper_format_file_open_existing(fff_data_file, file_name)) {
            FURI_LOG_E(TAG, "Error opening %s", file_name);
            ACTION_SET_ERROR("SUBGHZ: Error opening %s", file_name);
            break;
        }

        if(!flipper_format_read_header(fff_data_file, temp_str, &temp_data32)) {
            FURI_LOG_E(TAG, "Missing or incorrect header");
            ACTION_SET_ERROR("SUBGHZ: Missing or incorrect header");
            break;
        }

        if(((!strcmp(furi_string_get_cstr(temp_str), SUBGHZ_KEY_FILE_TYPE)) ||
            (!strcmp(furi_string_get_cstr(temp_str), SUBGHZ_RAW_FILE_TYPE))) &&
           temp_data32 == SUBGHZ_KEY_FILE_VERSION) {
        } else {
            FURI_LOG_E(TAG, "Type or version mismatch");
            ACTION_SET_ERROR("SUBGHZ: Type or version mismatch");
            break;
        }

        SubGhzSetting* setting = subghz_txrx_get_setting(txrx);
        if(!flipper_format_read_uint32(fff_data_file, "Frequency", &frequency, 1)) {
            FURI_LOG_W(TAG, "Missing Frequency. Setting default frequency");
            frequency = subghz_setting_get_default_frequency(setting);
        } else if(!subghz_txrx_radio_device_is_frequecy_valid(txrx, frequency)) {
            FURI_LOG_E(TAG, "Frequency not supported on the chosen radio module");
            ACTION_SET_ERROR("SUBGHZ: Frequency not supported");
            break;
        }

        if(!flipper_format_read_string(fff_data_file, "Preset", temp_str)) {
            FURI_LOG_E(TAG, "Missing Preset");
            ACTION_SET_ERROR("SUBGHZ: Missing preset");
            break;
        }

        furi_string_set_str(
            temp_str, subghz_txrx_get_preset_name(txrx, furi_string_get_cstr(temp_str)));
        if(!strcmp(furi_string_get_cstr(temp_str), "")) {
            FURI_LOG_E(TAG, "Unknown preset");
            ACTION_SET_ERROR("SUBGHZ: Unknown preset");
            break;
        }

        if(!strcmp(furi_string_get_cstr(temp_str), "CUSTOM")) {
            subghz_setting_delete_custom_preset(setting, furi_string_get_cstr(temp_str));
            if(!subghz_setting_load_custom_preset(
                   setting, furi_string_get_cstr(temp_str), fff_data_file)) {
                FURI_LOG_E(TAG, "Missing Custom preset");
                ACTION_SET_ERROR("SUBGHZ: Missing Custom preset");
                break;
            }
        }
        furi_string_set(preset_name, temp_str);
        size_t preset_index =
            subghz_setting_get_inx_preset_by_name(setting, furi_string_get_cstr(preset_name));
        subghz_txrx_set_preset(
            txrx,
            furi_string_get_cstr(preset_name),
            frequency,
            subghz_setting_get_preset_data(setting, preset_index),
            subghz_setting_get_preset_data_size(setting, preset_index));

        // Load Protocol
        if(!flipper_format_read_string(fff_data_file, "Protocol", protocol_name)) {
            FURI_LOG_E(TAG, "Missing protocol");
            ACTION_SET_ERROR("SUBGHZ: Missing protocol");
            break;
        }

        FlipperFormat* fff_data = subghz_txrx_get_fff_data(txrx);
        if(!strcmp(furi_string_get_cstr(protocol_name), "RAW")) {
            subghz_protocol_raw_gen_fff_data(
                fff_data, file_name, subghz_txrx_radio_device_get_name(txrx));
            is_raw = true;
        } else {
            Stream* source = flipper_format_get_raw_stream(fff_data_file);
            if(stream_copy_full(source, flipper_format_get_raw_stream(fff_data)) !=
               stream_size(source)) {
                ACTION_SET_ERROR("SUBGHZ: Failed to read signal");
                break;
            }
        }

        if(subghz_txrx_load_decoder_by_name_protocol(txrx, furi_string_get_cstr(protocol_name))) {
            SubGhzProtocolStatus status =
                subghz_protocol_decoder_base_deserialize(subghz_txrx_get_decoder(txrx), fff_data);
            if(status != SubGhzProtocolStatusOk) {
                ACTION_SET_ERROR("SUBGHZ: Invalid signal data");
                break;
            }
        } else {
            FURI_LOG_E(TAG, "Protocol not found: %s", furi_string_get_cstr(protocol_name));
            ACTION_SET_ERROR("SUBGHZ: Protocol not found");
            break;
        }
        loaded = true;
    } while(false);

    flipper_format_file_close(fff_data_file);
    flipper_format_free(fff_data_file);

    if(loaded && !is_raw && !quac_duration_valid(app->settings.subghz_duration)) {
        ACTION_SET_ERROR("Sub-GHz: Duration must be 100..60000 ms");
        loaded = false;
    }
    QuacActionWait* wait = NULL;
    if(loaded) {
        wait = quac_action_wait_alloc(app);
        subghz_block_generic_global.endless_tx = !is_raw;
        if(subghz_txrx_tx_start(txrx, subghz_txrx_get_fff_data(txrx)) ==
           SubGhzTxRxStartTxStateOk) {
            if(!quac_action_wait_tx(wait, txrx, is_raw, app->settings.subghz_duration) &&
               !app->action_cancelled)
                ACTION_SET_ERROR("Sub-GHz: Wait failed");
        } else {
            FURI_LOG_E(TAG, "Failed to start TX");
            ACTION_SET_ERROR("SUBGHZ: Failed to start TX");
        }
    }

    FURI_LOG_I(TAG, "SUBGHZ: Action complete.");

    // This will call need_save_callback, if necessary
    subghz_block_generic_global.endless_tx = false;
    subghz_txrx_stop(txrx);
    subghz_txrx_reset_dynamic_and_custom_btns(txrx);
    if(wait) quac_action_wait_free(wait);

    subghz_txrx_free(txrx);
    furi_string_free(preset_name);
    furi_string_free(protocol_name);
    furi_string_free(temp_str);
}
