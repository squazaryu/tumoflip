#include "subghz_txrx_i.h" // IWYU pragma: keep

#include <lib/subghz/protocols/protocol_items.h>
#include <applications/drivers/subghz/cc1101_ext/cc1101_ext_interconnect.h>
#include <lib/subghz/devices/cc1101_int/cc1101_int_interconnect.h>
#include <lib/subghz/blocks/custom_btn.h>
#include <lib/subghz/subghz_hopper_plan.h>
#include <lib/subghz/subghz_worker_i.h>

#define TAG "SubGhzTxRx"

#define SUBGHZ_HOPPER_DWELL_TICKS        2U
#define SUBGHZ_HOPPER_RELEASE_TICKS      4U
#define SUBGHZ_HOPPER_MAX_HOLD_TICKS     30U
#define SUBGHZ_PROTOCOL_PLUGIN_PATH      EXT_PATH("apps_data/subghz/plugins")
#define SUBGHZ_DIVERSITY_STREAM_CAPACITY 1024U

static void subghz_txrx_radio_device_power_on(SubGhzTxRx* instance) {
    subghz_radio_broker_external_power_on(instance->radio_broker, &instance->radio_lease);
}

static void subghz_txrx_radio_device_power_off(SubGhzTxRx* instance) {
    subghz_radio_broker_external_power_off(instance->radio_broker, &instance->radio_lease);
}

static void subghz_txrx_radio_state(SubGhzTxRx* instance, SubGhzRadioBrokerState state) {
    subghz_radio_broker_set_state(instance->radio_broker, &instance->radio_lease, state);
}

static void subghz_txrx_receiver_callback(
    SubGhzReceiver* receiver,
    SubGhzProtocolDecoderBase* decoder_base,
    void* context) {
    SubGhzTxRxReceiverContext* receiver_context = context;
    SubGhzTxRx* instance = receiver_context->instance;

    furi_check(furi_mutex_acquire(instance->rx_callback_mutex, FuriWaitForever) == FuriStatusOk);
    const SubGhzDevice* source_device = receiver_context->source == SubGhzRadioDeviceTypeInternal ?
                                            instance->diversity_radio_device :
                                            instance->radio_device;
    if(instance->radio_device_type != SubGhzRadioDeviceTypeAuto) {
        source_device = instance->radio_device;
    }
    const float source_rssi = source_device ? subghz_devices_get_rssi(source_device) : -127.0f;
    const uint8_t source_hash = subghz_protocol_decoder_base_get_hash_data(decoder_base);
    const uint32_t now = furi_get_tick();
    const bool same_frame = source_hash == instance->last_rx_hash &&
                            (now - instance->last_rx_tick) < 500U;
    if(!same_frame || source_rssi > instance->last_rx_rssi) {
        instance->last_rx_device_type = receiver_context->source;
        instance->last_rx_rssi = source_rssi;
        instance->last_rx_hash = source_hash;
    }
    instance->last_rx_tick = now;
    if(instance->rx_callback) {
        instance->rx_callback(receiver, decoder_base, instance->rx_context);
    }
    furi_mutex_release(instance->rx_callback_mutex);
}

static void subghz_txrx_configure_receiver(
    SubGhzTxRx* instance,
    SubGhzWorker* worker,
    SubGhzReceiver* receiver,
    SubGhzTxRxReceiverContext* context) {
    subghz_receiver_set_filter(receiver, instance->receiver_filter);
    subghz_receiver_set_rx_callback(receiver, subghz_txrx_receiver_callback, context);
    subghz_worker_set_overrun_callback(worker, (SubGhzWorkerOverrunCallback)subghz_receiver_reset);
    subghz_worker_set_pair_callback(worker, (SubGhzWorkerPairCallback)subghz_receiver_decode);
    subghz_worker_set_context(worker, receiver);
}

static void subghz_txrx_diversity_free(SubGhzTxRx* instance) {
    if(instance->diversity_worker) {
        furi_check(!subghz_worker_is_running(instance->diversity_worker));
        subghz_worker_free(instance->diversity_worker);
        instance->diversity_worker = NULL;
    }
    if(instance->diversity_receiver) {
        subghz_receiver_free(instance->diversity_receiver);
        instance->diversity_receiver = NULL;
    }
    instance->diversity_decoder_result = NULL;
    instance->diversity_radio_device = NULL;
    instance->diversity_rx_active = false;
}

static void subghz_txrx_diversity_alloc(SubGhzTxRx* instance) {
    if(instance->diversity_worker) return;

    instance->diversity_worker =
        subghz_worker_alloc_with_capacity(SUBGHZ_DIVERSITY_STREAM_CAPACITY);
    instance->diversity_receiver = subghz_receiver_alloc_init(instance->environment);
    instance->diversity_radio_device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
    instance->diversity_receiver_context.instance = instance;
    instance->diversity_receiver_context.source = SubGhzRadioDeviceTypeInternal;
    subghz_txrx_configure_receiver(
        instance,
        instance->diversity_worker,
        instance->diversity_receiver,
        &instance->diversity_receiver_context);
}

SubGhzTxRx* subghz_txrx_alloc(SubGhzProtocolPackGroup protocol_pack_group) {
    SubGhzTxRx* instance = calloc(1, sizeof(SubGhzTxRx));
    instance->radio_broker = furi_record_open(RECORD_SUBGHZ_RADIO_BROKER);
    furi_check(subghz_radio_broker_acquire(
        instance->radio_broker, "system_subghz", FuriWaitForever, &instance->radio_lease));
    instance->setting = subghz_setting_alloc();
    subghz_setting_load(instance->setting, EXT_PATH("subghz/assets/setting_user"));

    instance->preset = malloc(sizeof(SubGhzRadioPreset));
    instance->preset->name = furi_string_alloc();
    subghz_txrx_set_default_preset(instance, 0);

    instance->txrx_state = SubGhzTxRxStateSleep;

    subghz_txrx_hopper_set_state(instance, SubGhzHopperStateOFF);
    instance->hopper_idx_frequency = 0;
    instance->preset_hopper_idx = 0;
    subghz_txrx_speaker_set_state(instance, SubGhzSpeakerStateDisable);
    subghz_txrx_set_debug_pin_state(instance, false);

    instance->worker = subghz_worker_alloc();
    instance->rx_callback_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    instance->fff_data = flipper_format_string_alloc();
    instance->rx_callback = NULL;
    instance->rx_context = NULL;
    instance->receiver_filter = SubGhzProtocolFlag_Decodable;

    instance->environment = subghz_environment_alloc();
    instance->is_database_loaded =
        subghz_environment_load_keystore(instance->environment, SUBGHZ_KEYSTORE_DIR_NAME);
    subghz_environment_load_keystore(instance->environment, SUBGHZ_KEYSTORE_DIR_USER_NAME);
    subghz_environment_set_alutech_at_4n_rainbow_table_file_name(
        instance->environment, SUBGHZ_ALUTECH_AT_4N_DIR_NAME);
    subghz_environment_set_nice_flor_s_rainbow_table_file_name(
        instance->environment, SUBGHZ_NICE_FLOR_S_DIR_NAME);
    instance->protocol_pack_registry = subghz_protocol_pack_registry_alloc(
        &subghz_protocol_registry, SUBGHZ_PROTOCOL_PLUGIN_PATH, protocol_pack_group);
    instance->protocol_pack_group = protocol_pack_group;
    subghz_environment_set_protocol_registry(
        instance->environment,
        subghz_protocol_pack_registry_get(instance->protocol_pack_registry));
    instance->receiver = subghz_receiver_alloc_init(instance->environment);
    instance->primary_receiver_context.instance = instance;
    instance->primary_receiver_context.source = SubGhzRadioDeviceTypeInternal;
    instance->last_rx_device_type = SubGhzRadioDeviceTypeInternal;
    instance->last_rx_rssi = -127.0f;
    subghz_txrx_configure_receiver(
        instance, instance->worker, instance->receiver, &instance->primary_receiver_context);

    //set default device External
    subghz_txrx_radio_state(instance, SubGhzRadioBrokerStateProbing);
    subghz_devices_init();
    instance->radio_device_type = SubGhzRadioDeviceTypeInternal;
    instance->radio_device_type =
        subghz_txrx_radio_device_set(instance, SubGhzRadioDeviceTypeExternalCC1101);
    subghz_txrx_radio_state(instance, SubGhzRadioBrokerStateInitialized);

    return instance;
}

bool subghz_txrx_reload_protocol_pack(
    SubGhzTxRx* instance,
    SubGhzProtocolPackGroup protocol_pack_group) {
    furi_assert(instance);

    if(protocol_pack_group >= SubGhzProtocolPackGroupCount) return false;
    if(protocol_pack_group == instance->protocol_pack_group) return true;
    if(instance->txrx_state == SubGhzTxRxStateTx) return false;

    const bool resume_rx = instance->txrx_state == SubGhzTxRxStateRx;
    subghz_txrx_stop(instance);

    const bool diversity_enabled = instance->radio_device_type == SubGhzRadioDeviceTypeAuto;
    subghz_txrx_diversity_free(instance);
    subghz_worker_set_context(instance->worker, NULL);
    subghz_receiver_free(instance->receiver);
    subghz_protocol_pack_registry_free(instance->protocol_pack_registry);

    instance->protocol_pack_registry = subghz_protocol_pack_registry_alloc(
        &subghz_protocol_registry, SUBGHZ_PROTOCOL_PLUGIN_PATH, protocol_pack_group);
    instance->protocol_pack_group = protocol_pack_group;
    subghz_environment_set_protocol_registry(
        instance->environment,
        subghz_protocol_pack_registry_get(instance->protocol_pack_registry));

    instance->receiver = subghz_receiver_alloc_init(instance->environment);
    subghz_txrx_configure_receiver(
        instance, instance->worker, instance->receiver, &instance->primary_receiver_context);
    if(diversity_enabled) subghz_txrx_diversity_alloc(instance);
    instance->decoder_result = subghz_receiver_search_decoder_base_by_name(
        instance->receiver, SUBGHZ_PROTOCOL_BIN_RAW_NAME);
    if(instance->diversity_receiver) {
        instance->diversity_decoder_result = subghz_receiver_search_decoder_base_by_name(
            instance->diversity_receiver, SUBGHZ_PROTOCOL_BIN_RAW_NAME);
    }

    if(resume_rx) subghz_txrx_rx_start(instance);
    return true;
}

const SubGhzProtocolPackReport* subghz_txrx_get_protocol_pack_report(SubGhzTxRx* instance) {
    furi_assert(instance);
    return subghz_protocol_pack_registry_get_report(instance->protocol_pack_registry);
}

void subghz_txrx_free(SubGhzTxRx* instance) {
    furi_assert(instance);

    subghz_txrx_stop(instance);
    subghz_txrx_radio_state(instance, SubGhzRadioBrokerStateCleaningUp);
    if(instance->radio_device_type != SubGhzRadioDeviceTypeInternal) {
        subghz_txrx_radio_device_power_off(instance);
        subghz_devices_end(instance->radio_device);
    }

    subghz_devices_deinit();
    subghz_radio_broker_release(instance->radio_broker, &instance->radio_lease);
    furi_record_close(RECORD_SUBGHZ_RADIO_BROKER);

    subghz_txrx_diversity_free(instance);
    subghz_worker_free(instance->worker);
    subghz_receiver_free(instance->receiver);
    subghz_protocol_pack_registry_free(instance->protocol_pack_registry);
    subghz_environment_free(instance->environment);
    flipper_format_free(instance->fff_data);
    furi_string_free(instance->preset->name);
    subghz_setting_free(instance->setting);
    furi_mutex_free(instance->rx_callback_mutex);

    free(instance->preset);
    free(instance);
}

bool subghz_txrx_is_database_loaded(SubGhzTxRx* instance) {
    furi_assert(instance);
    return instance->is_database_loaded;
}

void subghz_txrx_set_preset(
    SubGhzTxRx* instance,
    const char* preset_name,
    uint32_t frequency,
    uint8_t* preset_data,
    size_t preset_data_size) {
    furi_assert(instance);
    furi_string_set(instance->preset->name, preset_name);

    SubGhzRadioPreset* preset = instance->preset;
    preset->frequency = frequency;
    preset->data = preset_data;
    preset->data_size = preset_data_size;
}

uint8_t*
    subghz_txrx_set_tx_power(uint8_t* preset_data, size_t preset_data_size, uint8_t tx_power) {
#define PRESET_POWER_OFFSET_FM 8
#define PRESET_POWER_OFFSET_AM 7
#define TX_PATABLE_OFFSET_AM   8
#define TX_PATABLE_COUNT       17

    //I had to skip the +10dBM and -6dBm Values, use only ones AM/FM have in common.
    //Highest Value is 12dBm for AM, 10 for FM. So Menu needs to reflect that.
    const uint8_t tx_pa_table[TX_PATABLE_COUNT] = {
        0,
        0xC0, //12dBm
        0xCD, //7dBm
        0x86, //5dBm
        0x50, //0dBm
        0x26, // -10dBm
        0x1D, // -15dBm
        0x17, //-20dBm
        0x03, //-30dBm
        0xC0, // 10dBm
        0xC8, //7dBm
        0x84, //5dBm
        0x60, //0dBm
        0x34, //-10dBm
        0x1D, //-15dBm
        0x0E, // -20dBm
        0x12, //-30dBm
    };

    //Grab the AM and FM byte now, so we can do proper checks.
    uint8_t fm_byte = preset_data[preset_data_size - PRESET_POWER_OFFSET_FM];
    uint8_t am_byte = preset_data[preset_data_size - PRESET_POWER_OFFSET_AM];

    //Set the TX Power Here in the CC1101 register...

    //If we have both bytes 1st bytes set or none, this isnt a preset we can deal with here.
    if(fm_byte && !am_byte) {
        //Use FM Table
        if(tx_power) {
            preset_data[preset_data_size - PRESET_POWER_OFFSET_FM] =
                tx_pa_table[TX_PATABLE_OFFSET_AM + tx_power];
        } else {
            preset_data[preset_data_size - PRESET_POWER_OFFSET_FM] =
                tx_pa_table[1]; //Max Power 0xC0 10dBm
        }
    } else if(am_byte && !fm_byte) {
        //Use AM Table
        if(tx_power) {
            preset_data[preset_data_size - PRESET_POWER_OFFSET_AM] = tx_pa_table[tx_power];
        } else {
            preset_data[preset_data_size - PRESET_POWER_OFFSET_AM] =
                tx_pa_table[1]; //Max Power 0xC0 12dBm
        }
    }

    //Pass back the preset_so we can call one liners.
    return preset_data;
}

const char* subghz_txrx_get_preset_name(SubGhzTxRx* instance, const char* preset) {
    UNUSED(instance);
    const char* preset_name = "";
    if(!strcmp(preset, "FuriHalSubGhzPresetOok270Async")) {
        preset_name = "AM270";
    } else if(!strcmp(preset, "FuriHalSubGhzPresetOok650Async")) {
        preset_name = "AM650";
    } else if(!strcmp(preset, "FuriHalSubGhzPreset2FSKDev238Async")) {
        preset_name = "FM238";
    } else if(!strcmp(preset, "FuriHalSubGhzPreset2FSKDev12KAsync")) {
        preset_name = "FM12K";
    } else if(!strcmp(preset, "FuriHalSubGhzPreset2FSKDev476Async")) {
        preset_name = "FM476";
    } else if(!strcmp(preset, "FuriHalSubGhzPresetCustom")) {
        preset_name = "CUSTOM";
    } else {
        FURI_LOG_E(TAG, "Unknown preset");
    }
    return preset_name;
}

SubGhzRadioPreset subghz_txrx_get_preset(SubGhzTxRx* instance) {
    furi_assert(instance);
    return *instance->preset;
}

void subghz_txrx_get_frequency_and_modulation(
    SubGhzTxRx* instance,
    FuriString* frequency,
    FuriString* modulation,
    bool long_name) {
    furi_assert(instance);
    SubGhzRadioPreset* preset = instance->preset;
    if(frequency != NULL) {
        furi_string_printf(
            frequency,
            "%03ld.%02ld",
            preset->frequency / 1000000 % 1000,
            preset->frequency / 10000 % 100);
    }
    if(modulation != NULL) {
        if(long_name) {
            furi_string_printf(modulation, "%s", furi_string_get_cstr(preset->name));
        } else {
            furi_string_printf(modulation, "%.2s", furi_string_get_cstr(preset->name));
        }
    }
}

static void subghz_txrx_begin(SubGhzTxRx* instance, uint8_t* preset_data) {
    furi_assert(instance);
    subghz_devices_reset(instance->radio_device);
    subghz_devices_idle(instance->radio_device);
    subghz_devices_load_preset(instance->radio_device, FuriHalSubGhzPresetCustom, preset_data);
    if(instance->radio_device_type == SubGhzRadioDeviceTypeAuto &&
       instance->diversity_radio_device) {
        subghz_devices_reset(instance->diversity_radio_device);
        subghz_devices_idle(instance->diversity_radio_device);
        subghz_devices_load_preset(
            instance->diversity_radio_device, FuriHalSubGhzPresetCustom, preset_data);
    }
    instance->txrx_state = SubGhzTxRxStateIDLE;
    subghz_txrx_radio_state(instance, SubGhzRadioBrokerStateInitialized);
}

static uint32_t subghz_txrx_rx(SubGhzTxRx* instance, uint32_t frequency) {
    furi_assert(instance);
    furi_assert(
        instance->txrx_state != SubGhzTxRxStateRx && instance->txrx_state != SubGhzTxRxStateSleep);

    subghz_devices_idle(instance->radio_device);

    uint32_t value = subghz_devices_set_frequency(instance->radio_device, frequency);
    if(value == 0U) {
        if(instance->radio_device_type != SubGhzRadioDeviceTypeInternal) {
            FURI_LOG_W(TAG, "External radio retune failed, falling back to internal");
            subghz_txrx_radio_device_fallback_internal(instance);
            subghz_txrx_begin(instance, instance->preset->data);
            return subghz_txrx_rx(instance, frequency);
        }

        FURI_LOG_E(TAG, "Internal radio retune failed");
        return 0U;
    }

    subghz_devices_flush_rx(instance->radio_device);
    if(instance->radio_device_type == SubGhzRadioDeviceTypeAuto &&
       instance->diversity_radio_device) {
        subghz_devices_idle(instance->diversity_radio_device);
        subghz_devices_set_frequency(instance->diversity_radio_device, frequency);
        subghz_devices_flush_rx(instance->diversity_radio_device);
    }
    subghz_txrx_radio_state(instance, SubGhzRadioBrokerStateRx);
    subghz_txrx_speaker_on(instance);

    subghz_devices_start_async_rx(
        instance->radio_device, subghz_worker_rx_callback, instance->worker);
    if(instance->radio_device_type == SubGhzRadioDeviceTypeAuto &&
       instance->diversity_radio_device && instance->diversity_worker) {
        subghz_devices_start_async_rx(
            instance->diversity_radio_device,
            subghz_worker_rx_callback,
            instance->diversity_worker);
        instance->diversity_rx_active = true;
    }
    subghz_worker_start(instance->worker);
    if(instance->diversity_rx_active) subghz_worker_start(instance->diversity_worker);
    instance->txrx_state = SubGhzTxRxStateRx;
    subghz_txrx_radio_state(instance, SubGhzRadioBrokerStateAsyncRx);
    return value;
}

static void subghz_txrx_idle(SubGhzTxRx* instance) {
    furi_assert(instance);
    if(instance->txrx_state != SubGhzTxRxStateSleep) {
        subghz_devices_idle(instance->radio_device);
        if(instance->radio_device_type == SubGhzRadioDeviceTypeAuto &&
           instance->diversity_radio_device) {
            subghz_devices_idle(instance->diversity_radio_device);
        }
        subghz_txrx_speaker_off(instance);
        instance->txrx_state = SubGhzTxRxStateIDLE;
        subghz_txrx_radio_state(instance, SubGhzRadioBrokerStateInitialized);
    }
}

static void subghz_txrx_rx_end(SubGhzTxRx* instance) {
    furi_assert(instance);
    furi_assert(instance->txrx_state == SubGhzTxRxStateRx);

    subghz_txrx_radio_state(instance, SubGhzRadioBrokerStateCleaningUp);
    if(subghz_worker_is_running(instance->worker)) {
        subghz_worker_stop(instance->worker);
    }
    subghz_devices_stop_async_rx(instance->radio_device);
    if(instance->diversity_rx_active) {
        if(subghz_worker_is_running(instance->diversity_worker)) {
            subghz_worker_stop(instance->diversity_worker);
        }
        subghz_devices_stop_async_rx(instance->diversity_radio_device);
        instance->diversity_rx_active = false;
    }
    subghz_txrx_speaker_off(instance);
    instance->txrx_state = SubGhzTxRxStateIDLE;
    subghz_txrx_radio_state(instance, SubGhzRadioBrokerStateInitialized);
}

void subghz_txrx_sleep(SubGhzTxRx* instance) {
    furi_assert(instance);
    subghz_devices_sleep(instance->radio_device);
    if(instance->radio_device_type == SubGhzRadioDeviceTypeAuto &&
       instance->diversity_radio_device) {
        subghz_devices_sleep(instance->diversity_radio_device);
    }
    instance->txrx_state = SubGhzTxRxStateSleep;
}

static bool subghz_txrx_tx(SubGhzTxRx* instance, uint32_t frequency) {
    furi_assert(instance);
    furi_assert(instance->txrx_state != SubGhzTxRxStateSleep);

    subghz_devices_idle(instance->radio_device);
    if(instance->radio_device_type == SubGhzRadioDeviceTypeAuto &&
       instance->diversity_radio_device) {
        subghz_devices_idle(instance->diversity_radio_device);
    }
    subghz_devices_set_frequency(instance->radio_device, frequency);

    bool ret = subghz_devices_set_tx(instance->radio_device);
    if(ret) {
        subghz_txrx_speaker_on(instance);
        instance->txrx_state = SubGhzTxRxStateTx;
        subghz_txrx_radio_state(instance, SubGhzRadioBrokerStateTx);
    }

    return ret;
}

SubGhzTxRxStartTxState subghz_txrx_tx_start(SubGhzTxRx* instance, FlipperFormat* flipper_format) {
    furi_assert(instance);
    furi_assert(flipper_format);

    subghz_txrx_stop(instance);

    SubGhzTxRxStartTxState ret = SubGhzTxRxStartTxStateErrorParserOthers;
    FuriString* temp_str = furi_string_alloc();
    do {
        if(!flipper_format_rewind(flipper_format)) {
            FURI_LOG_E(TAG, "Rewind error");
            break;
        }
        if(!flipper_format_read_string(flipper_format, "Protocol", temp_str)) {
            FURI_LOG_E(TAG, "Missing Protocol");
            break;
        }
        ret = SubGhzTxRxStartTxStateOk;

        SubGhzRadioPreset* preset = instance->preset;
        instance->transmitter =
            subghz_transmitter_alloc_init(instance->environment, furi_string_get_cstr(temp_str));

        if(instance->transmitter) {
            if(subghz_transmitter_deserialize(instance->transmitter, flipper_format) ==
               SubGhzProtocolStatusOk) {
                if(strcmp(furi_string_get_cstr(preset->name), "") != 0) {
                    subghz_txrx_begin(
                        instance,
                        subghz_setting_get_preset_data_by_name(
                            instance->setting, furi_string_get_cstr(preset->name)));
                    if(preset->frequency) {
                        if(!subghz_txrx_tx(instance, preset->frequency)) {
                            FURI_LOG_E(TAG, "Only Rx");
                            ret = SubGhzTxRxStartTxStateErrorOnlyRx;
                        }
                    } else {
                        ret = SubGhzTxRxStartTxStateErrorParserOthers;
                    }

                } else {
                    FURI_LOG_E(
                        TAG, "Unknown name preset \" %s \"", furi_string_get_cstr(preset->name));
                    ret = SubGhzTxRxStartTxStateErrorParserOthers;
                }

                if(ret == SubGhzTxRxStartTxStateOk) {
                    //Start TX
                    subghz_devices_start_async_tx(
                        instance->radio_device, subghz_transmitter_yield, instance->transmitter);
                    subghz_txrx_radio_state(instance, SubGhzRadioBrokerStateAsyncTx);
                }
            } else {
                ret = SubGhzTxRxStartTxStateErrorParserOthers;
            }
        } else {
            ret = SubGhzTxRxStartTxStateErrorParserOthers;
        }
        if(ret != SubGhzTxRxStartTxStateOk) {
            subghz_transmitter_free(instance->transmitter);
            if(instance->txrx_state != SubGhzTxRxStateIDLE) {
                subghz_txrx_idle(instance);
            }
        }

    } while(false);
    furi_string_free(temp_str);
    return ret;
}

void subghz_txrx_rx_start(SubGhzTxRx* instance) {
    furi_assert(instance);
    subghz_txrx_stop(instance);
    subghz_txrx_begin(
        instance,
        subghz_setting_get_preset_data_by_name(
            subghz_txrx_get_setting(instance), furi_string_get_cstr(instance->preset->name)));
    subghz_txrx_rx(instance, instance->preset->frequency);
}

void subghz_txrx_set_need_save_callback(
    SubGhzTxRx* instance,
    SubGhzTxRxNeedSaveCallback callback,
    void* context) {
    furi_assert(instance);
    instance->need_save_callback = callback;
    instance->need_save_context = context;
}

static void subghz_txrx_tx_stop(SubGhzTxRx* instance) {
    furi_assert(instance);
    furi_assert(instance->txrx_state == SubGhzTxRxStateTx);
    subghz_txrx_radio_state(instance, SubGhzRadioBrokerStateCleaningUp);
    //Stop TX
    subghz_devices_stop_async_tx(instance->radio_device);
    subghz_transmitter_stop(instance->transmitter);
    subghz_transmitter_free(instance->transmitter);

    //if protocol dynamic then we save the last upload
    if(instance->decoder_result->protocol->type == SubGhzProtocolTypeDynamic) {
        if(instance->need_save_callback) {
            instance->need_save_callback(instance->need_save_context);
        }
    }
    subghz_txrx_idle(instance);
    subghz_txrx_speaker_off(instance);
}

FlipperFormat* subghz_txrx_get_fff_data(SubGhzTxRx* instance) {
    furi_assert(instance);
    return instance->fff_data;
}

SubGhzSetting* subghz_txrx_get_setting(SubGhzTxRx* instance) {
    furi_assert(instance);
    return instance->setting;
}

void subghz_txrx_stop(SubGhzTxRx* instance) {
    furi_assert(instance);

    switch(instance->txrx_state) {
    case SubGhzTxRxStateTx:
        subghz_txrx_tx_stop(instance);
        subghz_txrx_speaker_unmute(instance);
        break;
    case SubGhzTxRxStateRx:
        subghz_txrx_rx_end(instance);
        subghz_txrx_speaker_mute(instance);
        break;

    default:
        break;
    }
}

void subghz_txrx_hopper_update(
    SubGhzTxRx* instance,
    float stay_threshold,
    bool hop_frequency,
    bool hop_preset) {
    furi_assert(instance);

    switch(instance->hopper_state) {
    case SubGhzHopperStateOFF:
    case SubGhzHopperStatePause:
        return;
    default:
        break;
    }

    bool signal_present = subghz_txrx_radio_device_get_rssi(instance) > stay_threshold;
    if(instance->hopper_state == SubGhzHopperStateRSSITimeOut) {
        instance->hopper_hold_ticks++;
        if(signal_present) {
            instance->hopper_timeout = SUBGHZ_HOPPER_RELEASE_TICKS;
        }
        if(instance->hopper_hold_ticks < SUBGHZ_HOPPER_MAX_HOLD_TICKS &&
           (signal_present || instance->hopper_timeout > 0)) {
            if(!signal_present) instance->hopper_timeout--;
            return;
        }
        instance->hopper_state = SubGhzHopperStateRunning;
        instance->hopper_hold_ticks = 0;
    } else {
        if(signal_present) {
            instance->hopper_timeout = SUBGHZ_HOPPER_RELEASE_TICKS;
            instance->hopper_hold_ticks = 0;
            instance->hopper_state = SubGhzHopperStateRSSITimeOut;
            return;
        }
        instance->hopper_hold_ticks = 0;
        if(instance->hopper_timeout > 0) {
            instance->hopper_timeout--;
            return;
        }
    }

    size_t frequency_count = subghz_setting_get_hopper_frequency_count(instance->setting);
    size_t configured_preset_count = subghz_setting_get_hopper_preset_count(instance->setting);
    size_t preset_count = configured_preset_count > 0 ?
                              configured_preset_count :
                              subghz_setting_get_preset_count(instance->setting);
    size_t frequency_index = instance->hopper_idx_frequency;
    size_t preset_hopper_index = instance->preset_hopper_idx;
    SubGhzHopperPlan hopper_plan;
    if(!subghz_hopper_plan_next(
           &frequency_index,
           &preset_hopper_index,
           hop_frequency,
           hop_preset,
           frequency_count,
           preset_count,
           &hopper_plan)) {
        return;
    }
    instance->hopper_idx_frequency = (uint8_t)frequency_index;
    instance->preset_hopper_idx = preset_hopper_index;

    uint32_t frequency =
        hop_frequency ?
            subghz_setting_get_hopper_frequency(instance->setting, hopper_plan.frequency_index) :
            instance->preset->frequency;

    if(instance->txrx_state == SubGhzTxRxStateRx) {
        subghz_txrx_rx_end(instance);
    }
    if(instance->txrx_state == SubGhzTxRxStateIDLE) {
        subghz_receiver_reset(instance->receiver);
        if(hop_preset) {
            size_t preset_index = configured_preset_count > 0 ?
                                      subghz_setting_get_hopper_preset_index(
                                          instance->setting, hopper_plan.preset_hopper_index) :
                                      hopper_plan.preset_hopper_index;
            subghz_txrx_set_preset_internal(instance, frequency, preset_index, 0);
            subghz_devices_load_preset(
                instance->radio_device, FuriHalSubGhzPresetCustom, instance->preset->data);
            if(instance->radio_device_type == SubGhzRadioDeviceTypeAuto &&
               instance->diversity_radio_device) {
                subghz_devices_load_preset(
                    instance->diversity_radio_device,
                    FuriHalSubGhzPresetCustom,
                    instance->preset->data);
            }
        } else {
            instance->preset->frequency = frequency;
        }
        subghz_txrx_rx(instance, frequency);
        instance->hopper_timeout = SUBGHZ_HOPPER_DWELL_TICKS;
    }
}

SubGhzHopperState subghz_txrx_hopper_get_state(SubGhzTxRx* instance) {
    furi_assert(instance);
    return instance->hopper_state;
}

void subghz_txrx_hopper_set_state(SubGhzTxRx* instance, SubGhzHopperState state) {
    furi_assert(instance);
    instance->hopper_state = state;
    instance->hopper_timeout = state == SubGhzHopperStateRunning ? SUBGHZ_HOPPER_DWELL_TICKS : 0;
    instance->hopper_hold_ticks = 0;
}

void subghz_txrx_hopper_unpause(SubGhzTxRx* instance) {
    furi_assert(instance);
    if(instance->hopper_state == SubGhzHopperStatePause) {
        instance->hopper_state = SubGhzHopperStateRunning;
    }
}

void subghz_txrx_hopper_pause(SubGhzTxRx* instance) {
    furi_assert(instance);
    if(instance->hopper_state == SubGhzHopperStateRunning) {
        instance->hopper_state = SubGhzHopperStatePause;
    }
}

void subghz_txrx_speaker_on(SubGhzTxRx* instance) {
    furi_assert(instance);
    if(instance->debug_pin_state) {
        subghz_devices_set_async_mirror_pin(instance->radio_device, &gpio_ibutton);
    }

    if(instance->speaker_state == SubGhzSpeakerStateEnable) {
        if(furi_hal_speaker_acquire(30)) {
            if(!instance->debug_pin_state) {
                subghz_devices_set_async_mirror_pin(instance->radio_device, &gpio_speaker);
            }
        } else {
            instance->speaker_state = SubGhzSpeakerStateDisable;
        }
    }
}

void subghz_txrx_speaker_off(SubGhzTxRx* instance) {
    furi_assert(instance);
    if(instance->debug_pin_state) {
        subghz_devices_set_async_mirror_pin(instance->radio_device, NULL);
    }
    if(instance->speaker_state != SubGhzSpeakerStateDisable) {
        if(furi_hal_speaker_is_mine()) {
            if(!instance->debug_pin_state) {
                subghz_devices_set_async_mirror_pin(instance->radio_device, NULL);
            }
            furi_hal_speaker_release();
            if(instance->speaker_state == SubGhzSpeakerStateShutdown)
                instance->speaker_state = SubGhzSpeakerStateDisable;
        }
    }
}

void subghz_txrx_speaker_mute(SubGhzTxRx* instance) {
    furi_assert(instance);
    if(instance->debug_pin_state) {
        subghz_devices_set_async_mirror_pin(instance->radio_device, NULL);
    }
    if(instance->speaker_state == SubGhzSpeakerStateEnable) {
        if(furi_hal_speaker_is_mine()) {
            if(!instance->debug_pin_state) {
                subghz_devices_set_async_mirror_pin(instance->radio_device, NULL);
            }
        }
    }
}

void subghz_txrx_speaker_unmute(SubGhzTxRx* instance) {
    furi_assert(instance);
    if(instance->debug_pin_state) {
        subghz_devices_set_async_mirror_pin(instance->radio_device, &gpio_ibutton);
    }
    if(instance->speaker_state == SubGhzSpeakerStateEnable) {
        if(furi_hal_speaker_is_mine()) {
            if(!instance->debug_pin_state) {
                subghz_devices_set_async_mirror_pin(instance->radio_device, &gpio_speaker);
            }
        }
    }
}

void subghz_txrx_speaker_set_state(SubGhzTxRx* instance, SubGhzSpeakerState state) {
    furi_assert(instance);
    instance->speaker_state = state;
}

SubGhzSpeakerState subghz_txrx_speaker_get_state(SubGhzTxRx* instance) {
    furi_assert(instance);
    return instance->speaker_state;
}

bool subghz_txrx_load_decoder_by_name_protocol(SubGhzTxRx* instance, const char* name_protocol) {
    furi_assert(instance);
    furi_assert(name_protocol);
    bool res = false;
    instance->decoder_result =
        subghz_receiver_search_decoder_base_by_name(instance->receiver, name_protocol);
    if(instance->decoder_result) {
        res = true;
    }
    if(instance->diversity_receiver) {
        instance->diversity_decoder_result = subghz_receiver_search_decoder_base_by_name(
            instance->diversity_receiver, name_protocol);
        res = res && instance->diversity_decoder_result;
    }
    return res;
}

SubGhzProtocolDecoderBase* subghz_txrx_get_decoder(SubGhzTxRx* instance) {
    furi_assert(instance);
    return instance->decoder_result;
}

SubGhzProtocolDecoderBase* subghz_txrx_get_diversity_decoder(SubGhzTxRx* instance) {
    furi_assert(instance);
    return instance->diversity_decoder_result;
}

bool subghz_txrx_protocol_is_serializable(SubGhzTxRx* instance) {
    furi_assert(instance);
    return (instance->decoder_result->protocol->flag & SubGhzProtocolFlag_Save) ==
           SubGhzProtocolFlag_Save;
}

bool subghz_txrx_protocol_is_transmittable(SubGhzTxRx* instance, bool check_type) {
    furi_assert(instance);
    const SubGhzProtocol* protocol = instance->decoder_result->protocol;
    if(check_type) {
        return ((protocol->flag & SubGhzProtocolFlag_Send) == SubGhzProtocolFlag_Send) &&
               protocol->encoder->deserialize && protocol->type == SubGhzProtocolTypeStatic;
    }
    return ((protocol->flag & SubGhzProtocolFlag_Send) == SubGhzProtocolFlag_Send) &&
           protocol->encoder->deserialize;
}

void subghz_txrx_receiver_set_filter(SubGhzTxRx* instance, SubGhzProtocolFlag filter) {
    furi_assert(instance);
    instance->receiver_filter = filter;
    subghz_receiver_set_filter(instance->receiver, filter);
    if(instance->diversity_receiver) {
        subghz_receiver_set_filter(instance->diversity_receiver, filter);
    }
}

void subghz_txrx_set_rx_callback(
    SubGhzTxRx* instance,
    SubGhzReceiverCallback callback,
    void* context) {
    instance->rx_callback = callback;
    instance->rx_context = context;
}

void subghz_txrx_set_raw_file_encoder_worker_callback_end(
    SubGhzTxRx* instance,
    SubGhzProtocolEncoderRAWCallbackEnd callback,
    void* context) {
    subghz_protocol_raw_file_encoder_worker_set_callback_end(
        (SubGhzProtocolEncoderRAW*)subghz_transmitter_get_protocol_instance(instance->transmitter),
        callback,
        context);
}

bool subghz_txrx_radio_device_is_external_connected(SubGhzTxRx* instance, const char* name) {
    furi_assert(instance);

    bool is_connect = false;
    bool is_otg_enabled = furi_hal_power_is_otg_enabled();

    if(!is_otg_enabled) {
        subghz_txrx_radio_device_power_on(instance);
    }

    const SubGhzDevice* device = subghz_devices_get_by_name(name);
    if(device) {
        is_connect = subghz_devices_is_connect(device);
    }

    if(!is_otg_enabled) {
        subghz_txrx_radio_device_power_off(instance);
    }
    return is_connect;
}

static SubGhzRadioDeviceType
    subghz_txrx_radio_device_apply(SubGhzTxRx* instance, SubGhzRadioDeviceType radio_device_type) {
    furi_assert(instance);

    subghz_txrx_stop(instance);
    subghz_txrx_radio_state(instance, SubGhzRadioBrokerStateProbing);
    const bool wants_external = radio_device_type == SubGhzRadioDeviceTypeExternalCC1101 ||
                                radio_device_type == SubGhzRadioDeviceTypeAuto;
    const bool external_was_active = instance->radio_device &&
                                     instance->radio_device_type != SubGhzRadioDeviceTypeInternal;
    const bool external_connected =
        wants_external &&
        (external_was_active ||
         subghz_txrx_radio_device_is_external_connected(instance, SUBGHZ_DEVICE_CC1101_EXT_NAME));
    bool external_ready = false;

    if(external_connected) {
        subghz_txrx_radio_device_power_on(instance);
        const SubGhzDevice* external = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_EXT_NAME);
        external_ready = external && (external_was_active || subghz_devices_begin(external));
        if(external_ready) {
            instance->radio_device = external;
            instance->primary_receiver_context.source = SubGhzRadioDeviceTypeExternalCC1101;
            if(radio_device_type == SubGhzRadioDeviceTypeAuto) {
                subghz_txrx_diversity_alloc(instance);
                instance->radio_device_type = SubGhzRadioDeviceTypeAuto;
                subghz_radio_broker_set_selected_device(
                    instance->radio_broker, &instance->radio_lease, SubGhzRadioBrokerDeviceDual);
            } else {
                subghz_txrx_diversity_free(instance);
                instance->radio_device_type = SubGhzRadioDeviceTypeExternalCC1101;
                subghz_radio_broker_set_selected_device(
                    instance->radio_broker,
                    &instance->radio_lease,
                    SubGhzRadioBrokerDeviceExternalCC1101);
            }
        }
    }

    if(!external_ready) {
        subghz_txrx_diversity_free(instance);
        subghz_txrx_radio_device_power_off(instance);
        if(external_was_active) {
            subghz_devices_end(instance->radio_device);
        }
        instance->radio_device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
        instance->radio_device_type = SubGhzRadioDeviceTypeInternal;
        instance->primary_receiver_context.source = SubGhzRadioDeviceTypeInternal;
        subghz_radio_broker_set_selected_device(
            instance->radio_broker, &instance->radio_lease, SubGhzRadioBrokerDeviceInternal);
    }

    instance->last_rx_device_type = instance->primary_receiver_context.source;
    instance->last_rx_rssi = -127.0f;
    instance->last_rx_tick = 0;
    instance->last_rx_hash = 0;
    subghz_txrx_radio_state(instance, SubGhzRadioBrokerStateInitialized);
    return instance->radio_device_type;
}

SubGhzRadioDeviceType
    subghz_txrx_radio_device_set(SubGhzTxRx* instance, SubGhzRadioDeviceType radio_device_type) {
    furi_assert(instance);

    instance->preferred_radio_device_type = radio_device_type;
    return subghz_txrx_radio_device_apply(instance, radio_device_type);
}

SubGhzRadioDeviceType subghz_txrx_radio_device_reprobe_preferred(SubGhzTxRx* instance) {
    furi_assert(instance);

    const SubGhzRadioDeviceType preferred = instance->preferred_radio_device_type;
    if(preferred == SubGhzRadioDeviceTypeInternal) {
        return instance->radio_device_type == SubGhzRadioDeviceTypeInternal ?
                   instance->radio_device_type :
                   subghz_txrx_radio_device_apply(instance, SubGhzRadioDeviceTypeInternal);
    }

    if(instance->radio_device_type != SubGhzRadioDeviceTypeInternal) {
        const bool external_connected = subghz_txrx_radio_device_is_external_connected(
            instance, SUBGHZ_DEVICE_CC1101_EXT_NAME);
        if(external_connected) {
            return instance->radio_device_type;
        }

        FURI_LOG_W(TAG, "External live probe failed, retrying cold");
        subghz_txrx_radio_device_apply(instance, SubGhzRadioDeviceTypeInternal);
    }

    return subghz_txrx_radio_device_apply(instance, preferred);
}

SubGhzRadioDeviceType subghz_txrx_radio_device_fallback_internal(SubGhzTxRx* instance) {
    furi_assert(instance);
    return subghz_txrx_radio_device_apply(instance, SubGhzRadioDeviceTypeInternal);
}

SubGhzRadioDeviceType subghz_txrx_radio_device_get(SubGhzTxRx* instance) {
    furi_assert(instance);
    return instance->radio_device_type;
}

SubGhzRadioDeviceType subghz_txrx_radio_device_get_last_rx(SubGhzTxRx* instance) {
    furi_assert(instance);
    return instance->last_rx_device_type;
}

float subghz_txrx_radio_device_get_rssi(SubGhzTxRx* instance) {
    furi_assert(instance);
    const float primary_rssi = subghz_devices_get_rssi(instance->radio_device);
    if(instance->radio_device_type == SubGhzRadioDeviceTypeAuto &&
       instance->diversity_radio_device) {
        const float diversity_rssi = subghz_devices_get_rssi(instance->diversity_radio_device);
        return MAX(primary_rssi, diversity_rssi);
    }
    return primary_rssi;
}

float subghz_txrx_radio_device_get_last_rx_rssi(SubGhzTxRx* instance) {
    furi_assert(instance);
    return instance->last_rx_rssi;
}

const char* subghz_txrx_radio_device_get_name(SubGhzTxRx* instance) {
    furi_assert(instance);
    return subghz_devices_get_name(instance->radio_device);
}

bool subghz_txrx_radio_device_is_frequency_valid(SubGhzTxRx* instance, uint32_t frequency) {
    furi_assert(instance);
    return subghz_devices_is_frequency_valid(instance->radio_device, frequency);
}

bool subghz_txrx_radio_device_is_tx_allowed(SubGhzTxRx* instance, uint32_t frequency) {
    // TODO: Remake this function to check if the frequency is allowed on specific module - for modules not based on CC1101
    furi_assert(instance);
    UNUSED(frequency);
    /*
    furi_assert(instance->txrx_state != SubGhzTxRxStateSleep);

    subghz_devices_idle(instance->radio_device);
    subghz_devices_set_frequency(instance->radio_device, frequency);

    bool ret = subghz_devices_set_tx(instance->radio_device);
    subghz_devices_idle(instance->radio_device);

    return ret;
    */
    return true;
}

void subghz_txrx_set_debug_pin_state(SubGhzTxRx* instance, bool state) {
    furi_assert(instance);
    instance->debug_pin_state = state;
}

bool subghz_txrx_get_debug_pin_state(SubGhzTxRx* instance) {
    furi_assert(instance);
    return instance->debug_pin_state;
}

void subghz_txrx_reset_dynamic_and_custom_btns(SubGhzTxRx* instance) {
    furi_assert(instance);
    subghz_environment_reset_keeloq(instance->environment);

    faac_slh_reset_prog_mode();

    subghz_custom_btns_reset();
}

SubGhzReceiver* subghz_txrx_get_receiver(SubGhzTxRx* instance) {
    furi_assert(instance);
    return instance->receiver;
}

void subghz_txrx_set_default_preset(SubGhzTxRx* instance, uint32_t frequency) {
    furi_assert(instance);

    const char* default_modulation = "AM650";
    if(frequency == 0) {
        frequency = subghz_setting_get_default_frequency(subghz_txrx_get_setting(instance));
    }
    subghz_txrx_set_preset(instance, default_modulation, frequency, NULL, 0);
}

const char* subghz_txrx_set_preset_internal(
    SubGhzTxRx* instance,
    uint32_t frequency,
    uint8_t index,
    uint8_t tx_power) {
    furi_assert(instance);

    //Grab the prset name.
    SubGhzSetting* setting = subghz_txrx_get_setting(instance);
    const char* preset_name = subghz_setting_get_preset_name(setting, index);
    subghz_setting_set_default_frequency(setting, frequency);

    //Get the preset data now so we can set TX power.
    uint8_t* preset_data = subghz_setting_get_preset_data(setting, index);
    size_t preset_data_size = subghz_setting_get_preset_data_size(setting, index);

    //Edit TX power, if necessary.
    subghz_txrx_set_tx_power(preset_data, preset_data_size, tx_power);

    //Set the Updated Preset.
    subghz_txrx_set_preset(instance, preset_name, frequency, preset_data, preset_data_size);

    return preset_name;
}
