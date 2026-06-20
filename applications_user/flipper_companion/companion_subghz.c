// Sub-GHz transmit-from-file, adapted from Quac! (actions/action_subghz.c)
// by Roberto De Feo, GPL. Trimmed to a single self-contained TX call.

#include "companion_subghz.h"
#include "helpers/subghz_txrx.h"

#include <flipper_format/flipper_format.h>
#include <flipper_format/flipper_format_i.h>
#include <lib/subghz/types.h>
#include <lib/subghz/protocols/raw.h>

#define TAG "Companion-SubGhz"

#define SET_ERR(...)                            \
    do {                                        \
        if(error) furi_string_printf(error, __VA_ARGS__); \
    } while(0)

static void companion_subghz_raw_end_callback(void* context) {
    furi_assert(context);
    FuriThread* thread = context;
    furi_thread_flags_set(furi_thread_get_id(thread), 0);
}

bool companion_subghz_tx_file(
    Storage* storage,
    const char* file_name,
    uint32_t duration_ms,
    FuriString* error) {
    bool ok = false;
    bool is_raw = false;

    FlipperFormat* fff_data_file = flipper_format_file_alloc(storage);
    SubGhzTxRx* txrx = subghz_txrx_alloc();

    Stream* fff_data_stream =
        flipper_format_get_raw_stream(subghz_txrx_get_fff_data(txrx));
    stream_clean(fff_data_stream);

    FuriString* preset_name = furi_string_alloc();
    FuriString* protocol_name = furi_string_alloc();
    FuriString* temp_str = furi_string_alloc();
    uint32_t temp_data32 = 0;
    uint32_t frequency = 0;

    do {
        if(!flipper_format_file_open_existing(fff_data_file, file_name)) {
            SET_ERR("Open failed");
            break;
        }
        if(!flipper_format_read_header(fff_data_file, temp_str, &temp_data32)) {
            SET_ERR("Bad header");
            break;
        }
        if(!(((!strcmp(furi_string_get_cstr(temp_str), SUBGHZ_KEY_FILE_TYPE)) ||
              (!strcmp(furi_string_get_cstr(temp_str), SUBGHZ_RAW_FILE_TYPE))) &&
             temp_data32 == SUBGHZ_KEY_FILE_VERSION)) {
            SET_ERR("Type/version");
            break;
        }

        SubGhzSetting* setting = subghz_txrx_get_setting(txrx);
        if(!flipper_format_read_uint32(fff_data_file, "Frequency", &frequency, 1)) {
            frequency = subghz_setting_get_default_frequency(setting);
        } else if(!subghz_txrx_radio_device_is_frequecy_valid(txrx, frequency)) {
            SET_ERR("Freq unsupported");
            break;
        }

        if(!flipper_format_read_string(fff_data_file, "Preset", temp_str)) {
            SET_ERR("No preset");
            break;
        }
        furi_string_set_str(
            temp_str, subghz_txrx_get_preset_name(txrx, furi_string_get_cstr(temp_str)));
        if(!strcmp(furi_string_get_cstr(temp_str), "")) {
            SET_ERR("Unknown preset");
            break;
        }
        if(!strcmp(furi_string_get_cstr(temp_str), "CUSTOM")) {
            subghz_setting_delete_custom_preset(setting, furi_string_get_cstr(temp_str));
            if(!subghz_setting_load_custom_preset(
                   setting, furi_string_get_cstr(temp_str), fff_data_file)) {
                SET_ERR("No custom preset");
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

        if(!flipper_format_read_string(fff_data_file, "Protocol", protocol_name)) {
            SET_ERR("No protocol");
            break;
        }

        FlipperFormat* fff_data = subghz_txrx_get_fff_data(txrx);
        if(!strcmp(furi_string_get_cstr(protocol_name), "RAW")) {
            subghz_protocol_raw_gen_fff_data(
                fff_data, file_name, subghz_txrx_radio_device_get_name(txrx));
            is_raw = true;
        } else {
            stream_copy_full(
                flipper_format_get_raw_stream(fff_data_file),
                flipper_format_get_raw_stream(fff_data));
        }

        if(subghz_txrx_load_decoder_by_name_protocol(
               txrx, furi_string_get_cstr(protocol_name))) {
            SubGhzProtocolStatus status = subghz_protocol_decoder_base_deserialize(
                subghz_txrx_get_decoder(txrx), fff_data);
            if(status != SubGhzProtocolStatusOk) {
                SET_ERR("Deserialize");
                break;
            }
        } else {
            SET_ERR("Protocol n/a");
            break;
        }
        ok = true;
    } while(false);

    flipper_format_file_close(fff_data_file);
    flipper_format_free(fff_data_file);

    if(ok) {
        if(subghz_txrx_tx_start(txrx, subghz_txrx_get_fff_data(txrx)) !=
           SubGhzTxRxStartTxStateOk) {
            SET_ERR("TX start failed");
            ok = false;
        } else if(is_raw) {
            subghz_txrx_set_raw_file_encoder_worker_callback_end(
                txrx, companion_subghz_raw_end_callback, furi_thread_get_current());
            furi_thread_flags_wait(0, FuriFlagWaitAll, FuriWaitForever);
        } else {
            furi_delay_ms(duration_ms);
        }
        subghz_txrx_stop(txrx);
    }

    subghz_txrx_free(txrx);
    furi_string_free(preset_name);
    furi_string_free(protocol_name);
    furi_string_free(temp_str);
    return ok;
}
