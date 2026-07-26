#include "xremote_subghz.h"

#include <applications/drivers/subghz/cc1101_ext/cc1101_ext_interconnect.h>
#include <flipper_format/flipper_format.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/devices/cc1101_int/cc1101_int_interconnect.h>
#include <lib/subghz/devices/devices.h>
#include <lib/subghz/protocols/plugin_registry.h>
#include <lib/subghz/protocols/protocol_items.h>
#include <lib/subghz/subghz_protocol_registry.h>
#include <lib/subghz/transmitter.h>
#include <subghz_radio_broker/subghz_radio_broker.h>

#include <stdlib.h>
#include <string.h>

#define XREMOTE_SUBGHZ_OWNER           "xremote_device"
#define XREMOTE_SUBGHZ_ACQUIRE_TIMEOUT 500U
#define XREMOTE_SUBGHZ_TX_TIMEOUT_MS   5000U
#define XREMOTE_SUBGHZ_PLUGIN_PATH     EXT_PATH("apps_data/subghz/plugins")

static const uint8_t xremote_subghz_princeton_buttons[] = {0x1U, 0x2U, 0x4U, 0x8U};

static bool xremote_subghz_read_string(
    FlipperFormat* format,
    const char* key,
    char* output,
    size_t output_size) {
    FuriString* value = furi_string_alloc();
    const bool success = flipper_format_read_string(format, key, value);
    if(success) snprintf(output, output_size, "%s", furi_string_get_cstr(value));
    furi_string_free(value);
    return success;
}

static uint64_t xremote_subghz_key_from_bytes(const uint8_t key_data[8]) {
    uint64_t key = 0U;
    for(size_t i = 0U; i < 8U; i++)
        key = (key << 8U) | key_data[i];
    return key;
}

static bool xremote_subghz_is_changing_code(const char* protocol) {
    static const char* const changing_protocols[] = {
        SUBGHZ_PROTOCOL_KEELOQ_NAME,
        SUBGHZ_PROTOCOL_NICE_FLOR_S_NAME,
        SUBGHZ_PROTOCOL_SECPLUS_V1_NAME,
        SUBGHZ_PROTOCOL_SECPLUS_V2_NAME,
        SUBGHZ_PROTOCOL_SOMFY_TELIS_NAME,
        SUBGHZ_PROTOCOL_SOMFY_KEYTIS_NAME,
        SUBGHZ_PROTOCOL_FAAC_SLH_NAME,
        SUBGHZ_PROTOCOL_ALUTECH_AT_4N_NAME,
        SUBGHZ_PROTOCOL_CAME_ATOMO_NAME,
        SUBGHZ_PROTOCOL_KINGGATES_STYLO_4K_NAME,
        SUBGHZ_PROTOCOL_BENINCA_ARC_NAME,
        SUBGHZ_PROTOCOL_JAROLIFT_NAME,
        SUBGHZ_PROTOCOL_PHOENIX_V2_NAME,
    };
    for(size_t i = 0U; i < COUNT_OF(changing_protocols); i++) {
        if(strcmp(protocol, changing_protocols[i]) == 0) return true;
    }
    return false;
}

static XRemoteDeviceAdapter xremote_subghz_detect_adapter(const XRemoteSubGhzInfo* info) {
    if(strcmp(info->protocol, "Princeton") != 0 || info->bit_count != 24U) {
        return XRemoteDeviceAdapterReplayOnly;
    }
    const uint8_t button = info->key & 0xFU;
    for(size_t i = 0U; i < COUNT_OF(xremote_subghz_princeton_buttons); i++) {
        if(button == xremote_subghz_princeton_buttons[i]) {
            return XRemoteDeviceAdapterPrinceton4;
        }
    }
    return XRemoteDeviceAdapterReplayOnly;
}

static uint8_t xremote_subghz_original_variant(uint64_t key) {
    const uint8_t button = key & 0xFU;
    for(size_t i = 0U; i < COUNT_OF(xremote_subghz_princeton_buttons); i++) {
        if(button == xremote_subghz_princeton_buttons[i]) return (uint8_t)i;
    }
    return 0U;
}

XRemoteSubGhzStatus
    xremote_subghz_inspect(Storage* storage, const char* path, XRemoteSubGhzInfo* info) {
    if(!storage || !path || !info) return XRemoteSubGhzStatusInvalidFile;
    memset(info, 0, sizeof(*info));
    if(!storage_file_exists(storage, path)) return XRemoteSubGhzStatusMissingFile;

    FlipperFormat* format = flipper_format_buffered_file_alloc(storage);
    FuriString* header = furi_string_alloc();
    uint32_t version = 0U;
    uint8_t key_data[8] = {0};
    XRemoteSubGhzStatus status = XRemoteSubGhzStatusInvalidFile;
    do {
        if(!flipper_format_buffered_file_open_existing(format, path)) break;
        if(!flipper_format_read_header(format, header, &version)) break;
        if(strcmp(furi_string_get_cstr(header), "Flipper SubGhz Key File") != 0 &&
           strcmp(furi_string_get_cstr(header), "Flipper SubGhz RAW File") != 0) {
            break;
        }
        flipper_format_rewind(format);
        if(!flipper_format_read_uint32(format, "Frequency", &info->frequency_hz, 1U)) break;
        flipper_format_rewind(format);
        if(!xremote_subghz_read_string(
               format, "Protocol", info->protocol, sizeof(info->protocol))) {
            break;
        }
        flipper_format_rewind(format);
        flipper_format_read_uint32(format, "Bit", &info->bit_count, 1U);
        flipper_format_rewind(format);
        if(flipper_format_read_hex(format, "Key", key_data, sizeof(key_data))) {
            info->key = xremote_subghz_key_from_bytes(key_data);
        }
        info->changing_code = xremote_subghz_is_changing_code(info->protocol);
        info->adapter = xremote_subghz_detect_adapter(info);
        info->original_variant = xremote_subghz_original_variant(info->key);
        status = XRemoteSubGhzStatusOk;
    } while(false);

    furi_string_free(header);
    flipper_format_free(format);
    return status;
}

static bool xremote_subghz_preset_from_name(const char* name, FuriHalSubGhzPreset* preset) {
    if(strcmp(name, "FuriHalSubGhzPresetOok270Async") == 0) {
        *preset = FuriHalSubGhzPresetOok270Async;
    } else if(strcmp(name, "FuriHalSubGhzPresetOok650Async") == 0) {
        *preset = FuriHalSubGhzPresetOok650Async;
    } else if(strcmp(name, "FuriHalSubGhzPreset2FSKDev238Async") == 0) {
        *preset = FuriHalSubGhzPreset2FSKDev238Async;
    } else if(strcmp(name, "FuriHalSubGhzPreset2FSKDev12KAsync") == 0) {
        *preset = FuriHalSubGhzPreset2FSKDev12KAsync;
    } else if(strcmp(name, "FuriHalSubGhzPreset2FSKDev476Async") == 0) {
        *preset = FuriHalSubGhzPreset2FSKDev476Async;
    } else if(strcmp(name, "FuriHalSubGhzPresetMSK99_97KbAsync") == 0) {
        *preset = FuriHalSubGhzPresetMSK99_97KbAsync;
    } else if(strcmp(name, "FuriHalSubGhzPresetGFSK9_99KbAsync") == 0) {
        *preset = FuriHalSubGhzPresetGFSK9_99KbAsync;
    } else {
        return false;
    }
    return true;
}

XRemoteSubGhzStatus xremote_subghz_send(
    Storage* storage,
    const char* path,
    XRemoteDeviceRadio radio,
    XRemoteDeviceAdapter adapter,
    uint8_t variant) {
    XRemoteSubGhzInfo info;
    XRemoteSubGhzStatus status = xremote_subghz_inspect(storage, path, &info);
    if(status != XRemoteSubGhzStatusOk) return status;
    if(info.changing_code) return XRemoteSubGhzStatusChangingCodeBlocked;
    if(!furi_hal_subghz_is_tx_allowed(info.frequency_hz)) {
        return XRemoteSubGhzStatusRegionBlocked;
    }
    if(adapter == XRemoteDeviceAdapterPrinceton4 &&
       info.adapter != XRemoteDeviceAdapterPrinceton4) {
        return XRemoteSubGhzStatusInvalidFile;
    }

    FlipperFormat* format = flipper_format_buffered_file_alloc(storage);
    FuriString* protocol = furi_string_alloc();
    FuriString* preset_name = furi_string_alloc();
    SubGhzEnvironment* environment = NULL;
    SubGhzProtocolPackRegistry* protocol_pack = NULL;
    SubGhzTransmitter* transmitter = NULL;
    SubGhzRadioBroker* broker = NULL;
    SubGhzRadioBrokerLease lease = {0};
    const SubGhzDevice* device = NULL;
    FuriHalSubGhzPreset preset = FuriHalSubGhzPresetIDLE;
    bool broker_acquired = false;
    bool devices_initialized = false;
    bool external_powered = false;
    bool external_begun = false;
    bool tx_started = false;
    status = XRemoteSubGhzStatusRadioError;

    do {
        if(!flipper_format_buffered_file_open_existing(format, path)) {
            status = XRemoteSubGhzStatusMissingFile;
            break;
        }
        if(!flipper_format_read_string(format, "Protocol", protocol)) {
            status = XRemoteSubGhzStatusInvalidFile;
            break;
        }
        flipper_format_rewind(format);
        if(!flipper_format_read_string(format, "Preset", preset_name)) {
            status = XRemoteSubGhzStatusInvalidFile;
            break;
        }
        if(!xremote_subghz_preset_from_name(furi_string_get_cstr(preset_name), &preset)) {
            status = XRemoteSubGhzStatusUnsupportedPreset;
            break;
        }

        broker = furi_record_open(RECORD_SUBGHZ_RADIO_BROKER);
        if(!subghz_radio_broker_acquire(
               broker, XREMOTE_SUBGHZ_OWNER, XREMOTE_SUBGHZ_ACQUIRE_TIMEOUT, &lease)) {
            status = XRemoteSubGhzStatusRadioBusy;
            break;
        }
        broker_acquired = true;

        environment = subghz_environment_alloc();
        if(!environment) break;
        protocol_pack = subghz_protocol_pack_registry_alloc(
            &subghz_protocol_registry, XREMOTE_SUBGHZ_PLUGIN_PATH, SubGhzProtocolPackGroupCore);
        if(!protocol_pack) break;
        subghz_environment_set_protocol_registry(
            environment, subghz_protocol_pack_registry_get(protocol_pack));

        subghz_block_generic_global_reset(NULL);
        if(adapter == XRemoteDeviceAdapterPrinceton4) {
            const uint8_t button = xremote_subghz_princeton_buttons
                [variant % COUNT_OF(xremote_subghz_princeton_buttons)];
            subghz_block_generic_global_button_override_set(button);
        }
        transmitter = subghz_transmitter_alloc_init(environment, furi_string_get_cstr(protocol));
        if(!transmitter) {
            status = XRemoteSubGhzStatusInvalidFile;
            break;
        }
        flipper_format_rewind(format);
        if(subghz_transmitter_deserialize(transmitter, format) != SubGhzProtocolStatusOk) {
            status = XRemoteSubGhzStatusInvalidFile;
            break;
        }

        subghz_devices_init();
        devices_initialized = true;

        if(radio == XRemoteDeviceRadioExternal) {
            external_powered = subghz_radio_broker_external_power_on(broker, &lease);
            device = external_powered ? subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_EXT_NAME) :
                                        NULL;
            if(!device || !subghz_devices_is_connect(device) || !subghz_devices_begin(device)) {
                status = XRemoteSubGhzStatusExternalUnavailable;
                break;
            }
            external_begun = true;
            subghz_radio_broker_set_selected_device(
                broker, &lease, SubGhzRadioBrokerDeviceExternalCC1101);
        } else {
            device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
            if(!device) break;
            subghz_radio_broker_set_selected_device(
                broker, &lease, SubGhzRadioBrokerDeviceInternal);
        }

        subghz_radio_broker_set_state(broker, &lease, SubGhzRadioBrokerStateInitialized);
        if(!subghz_devices_is_frequency_valid(device, info.frequency_hz)) {
            status = XRemoteSubGhzStatusRegionBlocked;
            break;
        }
        subghz_devices_reset(device);
        subghz_devices_idle(device);
        subghz_devices_load_preset(device, preset, NULL);
        if(subghz_devices_set_frequency(device, info.frequency_hz) == 0U ||
           !subghz_devices_set_tx(device)) {
            break;
        }
        subghz_radio_broker_set_state(broker, &lease, SubGhzRadioBrokerStateTx);
        if(!subghz_devices_start_async_tx(device, subghz_transmitter_yield, transmitter)) break;
        tx_started = true;
        subghz_radio_broker_set_state(broker, &lease, SubGhzRadioBrokerStateAsyncTx);

        const uint32_t started = furi_get_tick();
        while(!subghz_devices_is_async_complete_tx(device)) {
            if(furi_get_tick() - started > furi_ms_to_ticks(XREMOTE_SUBGHZ_TX_TIMEOUT_MS)) {
                status = XRemoteSubGhzStatusTimeout;
                break;
            }
            furi_delay_ms(5U);
        }
        if(status != XRemoteSubGhzStatusTimeout) status = XRemoteSubGhzStatusOk;
    } while(false);

    if(broker_acquired) {
        subghz_radio_broker_set_state(broker, &lease, SubGhzRadioBrokerStateCleaningUp);
    }
    if(tx_started) subghz_devices_stop_async_tx(device);
    if(device) {
        subghz_devices_idle(device);
        subghz_devices_sleep(device);
    }
    if(external_begun) subghz_devices_end(device);
    if(external_powered && broker_acquired) {
        subghz_radio_broker_external_power_off(broker, &lease);
    }
    if(devices_initialized) subghz_devices_deinit();
    if(broker_acquired) subghz_radio_broker_release(broker, &lease);
    if(broker) furi_record_close(RECORD_SUBGHZ_RADIO_BROKER);
    if(transmitter) subghz_transmitter_free(transmitter);
    if(protocol_pack) subghz_protocol_pack_registry_free(protocol_pack);
    if(environment) subghz_environment_free(environment);
    subghz_block_generic_global_reset(NULL);
    furi_string_free(preset_name);
    furi_string_free(protocol);
    flipper_format_free(format);
    return status;
}

const char* xremote_subghz_status_name(XRemoteSubGhzStatus status) {
    switch(status) {
    case XRemoteSubGhzStatusOk:
        return "Sent";
    case XRemoteSubGhzStatusMissingFile:
        return "Missing RF file";
    case XRemoteSubGhzStatusInvalidFile:
        return "Invalid RF file";
    case XRemoteSubGhzStatusUnsupportedPreset:
        return "Custom preset unsupported";
    case XRemoteSubGhzStatusChangingCodeBlocked:
        return "Changing-code blocked";
    case XRemoteSubGhzStatusRegionBlocked:
        return "TX blocked by region";
    case XRemoteSubGhzStatusRadioBusy:
        return "Radio is busy";
    case XRemoteSubGhzStatusExternalUnavailable:
        return "External unavailable";
    case XRemoteSubGhzStatusTimeout:
        return "TX timeout";
    case XRemoteSubGhzStatusRadioError:
    default:
        return "Radio error";
    }
}

const char* xremote_subghz_variant_name(uint8_t variant) {
    static const char* const names[] = {"B1", "B2", "B3", "B4"};
    return names[variant % COUNT_OF(names)];
}
