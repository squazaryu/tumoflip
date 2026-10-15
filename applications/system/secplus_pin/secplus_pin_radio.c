#include "secplus_pin_radio.h"
#include "../subghz_app_radio.h"

#define TAG "SecPlusPin"

/* one frame is a few tens of ms; this is a stall guard, not a normal bound */
#define SECPLUS_PIN_TX_TIMEOUT_MS 2000u

/* Every name the sub-ghz file format can carry, mirroring FuriHalSubGhzPreset.
 * Anything not listed here has to come with Custom_preset_data. */
static const struct {
    const char* name;
    FuriHalSubGhzPreset preset;
} secplus_pin_presets[] = {
    {"FuriHalSubGhzPresetOok270Async", FuriHalSubGhzPresetOok270Async},
    {"FuriHalSubGhzPresetOok650Async", FuriHalSubGhzPresetOok650Async},
    {"FuriHalSubGhzPreset2FSKDev238Async", FuriHalSubGhzPreset2FSKDev238Async},
    {"FuriHalSubGhzPreset2FSKDev12KAsync", FuriHalSubGhzPreset2FSKDev12KAsync},
    {"FuriHalSubGhzPreset2FSKDev476Async", FuriHalSubGhzPreset2FSKDev476Async},
    {"FuriHalSubGhzPresetMSK99_97KbAsync", FuriHalSubGhzPresetMSK99_97KbAsync},
    {"FuriHalSubGhzPresetGFSK9_99KbAsync", FuriHalSubGhzPresetGFSK9_99KbAsync},
};

bool secplus_pin_preset_by_name(const char* name, FuriHalSubGhzPreset* out) {
    for(size_t i = 0; i < COUNT_OF(secplus_pin_presets); i++) {
        if(!strcmp(name, secplus_pin_presets[i].name)) {
            *out = secplus_pin_presets[i].preset;
            return true;
        }
    }
    *out = FuriHalSubGhzPresetCustom;
    return false;
}

SecPlusPinTxStatus secplus_pin_radio_tx(
    const SecPlusPinPreset* preset,
    FlipperFormat* fff,
    bool allow_external,
    bool* external_used) {
    furi_assert(preset);
    furi_assert(fff);

    SecPlusPinTxStatus status = SecPlusPinTxError;
    if(external_used) *external_used = false;
    SubGhzAppRadio radio = {0};
    const bool acquired =
        allow_external ?
            subghz_app_radio_acquire(&radio, "Security+ PIN", preset->frequency) :
            subghz_app_radio_acquire_internal(&radio, "Security+ PIN", preset->frequency);
    if(!acquired) return status;
    if(external_used) *external_used = radio.external;
    SubGhzEnvironment* environment = subghz_environment_alloc();
    subghz_environment_set_protocol_registry(environment, (void*)&subghz_protocol_registry);
    SubGhzTransmitter* transmitter =
        subghz_transmitter_alloc_init(environment, SUBGHZ_PROTOCOL_SECPLUS_V2_NAME);

    do {
        if(!transmitter) break;
        if(!flipper_format_rewind(fff)) break;
        if(subghz_transmitter_deserialize(transmitter, fff) != SubGhzProtocolStatusOk) break;

        const SubGhzDevice* device = radio.device;
        if(!subghz_app_radio_is_connected(&radio)) break;

        /* An unnamed preset can only be loaded from Custom_preset_data; without it
         * furi_hal_subghz_load_custom_preset() would furi_check(NULL) and crash. */
        FuriHalSubGhzPreset preset_id;
        if(!secplus_pin_preset_by_name(furi_string_get_cstr(preset->preset_name), &preset_id) &&
           (preset->custom_preset == NULL)) {
            FURI_LOG_E(TAG, "Unsupported preset %s", furi_string_get_cstr(preset->preset_name));
            break;
        }

        subghz_devices_reset(device);
        subghz_devices_load_preset(device, preset_id, preset->custom_preset);
        subghz_devices_set_frequency(device, preset->frequency);

        furi_hal_power_suppress_charge_enter();
        /* The radio has to be switched into TX before the transfer is started. Without
         * this the DMA never runs, so is_async_complete_tx() never turns true and the
         * wait below would spin forever. It also fails when the region does not allow
         * transmitting on this frequency, which is the honest place to give up. */
        if(subghz_devices_set_tx(device)) {
            if(subghz_devices_start_async_tx(device, subghz_transmitter_yield, transmitter)) {
                /* bounded, so a stall can never take the UI down with it */
                uint32_t waited_ms = 0;
                while(subghz_app_radio_is_connected(&radio) &&
                      !subghz_devices_is_async_complete_tx(device) &&
                      (waited_ms < SECPLUS_PIN_TX_TIMEOUT_MS)) {
                    furi_delay_ms(2);
                    waited_ms += 2;
                }
                if(subghz_app_radio_is_connected(&radio) &&
                   subghz_devices_is_async_complete_tx(device)) {
                    status = SecPlusPinTxOk;
                } else {
                    FURI_LOG_E(TAG, "TX did not complete in time");
                }
                subghz_devices_stop_async_tx(device);
            }
            subghz_transmitter_stop(transmitter);
        } else {
            FURI_LOG_E(TAG, "TX not allowed on %lu Hz in this region", preset->frequency);
            status = SecPlusPinTxNotAllowed;
        }
        furi_hal_power_suppress_charge_exit();

    } while(false);

    if(transmitter) subghz_transmitter_free(transmitter);
    subghz_environment_free(environment);
    subghz_app_radio_release(&radio);
    return status;
}
