#pragma once

#include "subghz_types.h"

#include <lib/subghz/subghz_worker.h>
#include <lib/subghz/subghz_setting.h>
#include <lib/subghz/receiver.h>
#include <lib/subghz/transmitter.h>
#include <lib/subghz/protocols/raw.h>
#include <lib/subghz/protocols/plugin_registry.h>
#include <lib/subghz/devices/devices.h>
#include <subghz_radio_broker/subghz_radio_broker.h>

typedef struct SubGhzTxRx SubGhzTxRx;

typedef void (*SubGhzTxRxNeedSaveCallback)(void* context);

typedef enum {
    SubGhzTxRxStartTxStateOk,
    SubGhzTxRxStartTxStateErrorOnlyRx,
    SubGhzTxRxStartTxStateErrorParserOthers,
    SubGhzTxRxStartTxStateErrorCapability,
} SubGhzTxRxStartTxState;

/**
 * Allocate SubGhzTxRx
 * 
 * @return SubGhzTxRx* pointer to SubGhzTxRx
 */
SubGhzTxRx* subghz_txrx_alloc(SubGhzProtocolPackGroup protocol_pack_group);

/**
 * Free SubGhzTxRx
 * 
 * @param instance Pointer to a SubGhzTxRx
 */
void subghz_txrx_free(SubGhzTxRx* instance);

/**
 * Replace the active external protocol group while preserving RX state.
 *
 * @param instance Pointer to a SubGhzTxRx
 * @param protocol_pack_group Group to load
 * @return true when the group was applied
 */
bool subghz_txrx_reload_protocol_pack(
    SubGhzTxRx* instance,
    SubGhzProtocolPackGroup protocol_pack_group);

/**
 * Get the active protocol pack group.
 *
 * @param instance Pointer to a SubGhzTxRx
 * @return Currently loaded protocol pack group
 */
SubGhzProtocolPackGroup subghz_txrx_get_protocol_pack_group(SubGhzTxRx* instance);

/**
 * Get the load report for the active protocol pack group.
 *
 * @param instance Pointer to a SubGhzTxRx
 * @return Report owned by SubGhzTxRx and valid until the next pack reload
 */
const SubGhzProtocolPackReport* subghz_txrx_get_protocol_pack_report(SubGhzTxRx* instance);

/**
 * Check if the database is loaded
 * 
 * @param instance Pointer to a SubGhzTxRx
 * @return bool True if the database is loaded
 */
bool subghz_txrx_is_database_loaded(SubGhzTxRx* instance);

/**
 * Set preset 
 * 
 * @param instance Pointer to a SubGhzTxRx
 * @param preset_name Name of preset
 * @param frequency Frequency in Hz
 * @param preset_data Data of preset
 * @param preset_data_size Size of preset data
 */
void subghz_txrx_set_preset(
    SubGhzTxRx* instance,
    const char* preset_name,
    uint32_t frequency,
    uint8_t* preset_data,
    size_t preset_data_size);

/**
 * Set TX Power
 * 
 * @param preset_data Data of preset
 * @param preset_data_size Size of preset data
 * @param tx_power Menu Index of TX Power Setting. (Saves iterating in Config enter)
 */
uint8_t* subghz_txrx_set_tx_power(uint8_t* preset_data, size_t preset_data_size, uint8_t tx_power);

/**
 * Get name of preset
 * 
 * @param instance Pointer to a SubGhzTxRx
 * @param preset String of preset 
 * @return const char*  Name of preset
 */
const char* subghz_txrx_get_preset_name(SubGhzTxRx* instance, const char* preset);

/**
 * Get of preset
 * 
 * @param instance Pointer to a SubGhzTxRx
 * @return SubGhzRadioPreset Preset
 */
SubGhzRadioPreset subghz_txrx_get_preset(SubGhzTxRx* instance);

/**
 * Get string frequency and modulation
 * 
 * @param instance Pointer to a SubGhzTxRx
 * @param frequency Pointer to a string frequency
 * @param modulation Pointer to a string modulation
 */
void subghz_txrx_get_frequency_and_modulation(
    SubGhzTxRx* instance,
    FuriString* frequency,
    FuriString* modulation,
    bool long_name);

/**
 * Start TX CC1101
 * 
 * @param instance Pointer to a SubGhzTxRx
 * @param flipper_format Pointer to a FlipperFormat
 * @return SubGhzTxRxStartTxState 
 */
SubGhzTxRxStartTxState subghz_txrx_tx_start(SubGhzTxRx* instance, FlipperFormat* flipper_format);

SubGhzRadioBrokerValidation subghz_txrx_get_last_validation(SubGhzTxRx* instance);

/**
 * Rebuild protocol data without starting TX.
 *
 * @param instance Pointer to a SubGhzTxRx
 * @param flipper_format Pointer to a FlipperFormat
 * @return bool True when encoder deserialization updated protocol data successfully
 */
bool subghz_txrx_rebuild_from_fff(SubGhzTxRx* instance, FlipperFormat* flipper_format);

/**
 * Start RX CC1101
 * 
 * @param instance Pointer to a SubGhzTxRx
 */
void subghz_txrx_rx_start(SubGhzTxRx* instance);

/**
 * Stop TX/RX CC1101
 * 
 * @param instance Pointer to a SubGhzTxRx
 */
void subghz_txrx_stop(SubGhzTxRx* instance);

/**
 * Set sleep mode CC1101
 * 
 * @param instance Pointer to a SubGhzTxRx
 */
void subghz_txrx_sleep(SubGhzTxRx* instance);

/**
 * Update frequency CC1101 in automatic mode (hopper)
 * 
 * @param instance Pointer to a SubGhzTxRx
 * @param stay_threshold RSSI theshold over which to stay before hopping
 */
void subghz_txrx_hopper_update(
    SubGhzTxRx* instance,
    float stay_threshold,
    bool hop_frequency,
    bool hop_preset);

/**
 * Get state hopper
 * 
 * @param instance Pointer to a SubGhzTxRx
 * @return SubGhzHopperState 
 */
SubGhzHopperState subghz_txrx_hopper_get_state(SubGhzTxRx* instance);

/**
 * Set state hopper
 * 
 * @param instance Pointer to a SubGhzTxRx
 * @param state State hopper
 */
void subghz_txrx_hopper_set_state(SubGhzTxRx* instance, SubGhzHopperState state);

/**
 * Unpause hopper
 * 
 * @param instance Pointer to a SubGhzTxRx
 */
void subghz_txrx_hopper_unpause(SubGhzTxRx* instance);

/**
 * Set pause hopper
 * 
 * @param instance Pointer to a SubGhzTxRx
 */
void subghz_txrx_hopper_pause(SubGhzTxRx* instance);

/**
 * Speaker on
 * 
 * @param instance Pointer to a SubGhzTxRx 
 */
void subghz_txrx_speaker_on(SubGhzTxRx* instance);

/**
 * Speaker off
 * 
 * @param instance Pointer to a SubGhzTxRx 
 */
void subghz_txrx_speaker_off(SubGhzTxRx* instance);

/**
 * Speaker mute
 * 
 * @param instance Pointer to a SubGhzTxRx 
 */
void subghz_txrx_speaker_mute(SubGhzTxRx* instance);

/**
 * Speaker unmute
 * 
 * @param instance Pointer to a SubGhzTxRx 
 */
void subghz_txrx_speaker_unmute(SubGhzTxRx* instance);

/**
 * Set state speaker
 * 
 * @param instance Pointer to a SubGhzTxRx 
 * @param state State speaker
 */
void subghz_txrx_speaker_set_state(SubGhzTxRx* instance, SubGhzSpeakerState state);

/**
 * Get state speaker
 * 
 * @param instance Pointer to a SubGhzTxRx 
 * @return SubGhzSpeakerState 
 */
SubGhzSpeakerState subghz_txrx_speaker_get_state(SubGhzTxRx* instance);

/**
 * load decoder by name protocol
 * 
 * @param instance Pointer to a SubGhzTxRx
 * @param name_protocol Name protocol
 * @return bool True if the decoder is loaded 
 */
bool subghz_txrx_load_decoder_by_name_protocol(SubGhzTxRx* instance, const char* name_protocol);

/**
 * Get decoder
 * 
 * @param instance Pointer to a SubGhzTxRx
 * @return SubGhzProtocolDecoderBase* Pointer to a SubGhzProtocolDecoderBase
 */
SubGhzProtocolDecoderBase* subghz_txrx_get_decoder(SubGhzTxRx* instance);

/**
 * Set callback for save data
 * 
 * @param instance Pointer to a SubGhzTxRx
 * @param callback Callback for save data
 * @param context Context for callback
 */
void subghz_txrx_set_need_save_callback(
    SubGhzTxRx* instance,
    SubGhzTxRxNeedSaveCallback callback,
    void* context);

/**
 * Get pointer to a load data key
 * 
 * @param instance Pointer to a SubGhzTxRx
 * @return FlipperFormat* 
 */
FlipperFormat* subghz_txrx_get_fff_data(SubGhzTxRx* instance);

/**
 * Get pointer to a SugGhzSetting
 * 
 * @param instance Pointer to a SubGhzTxRx
 * @return SubGhzSetting* 
 */
SubGhzSetting* subghz_txrx_get_setting(SubGhzTxRx* instance);

/**
 * Is it possible to save this protocol
 * 
 * @param instance Pointer to a SubGhzTxRx 
 * @return bool True if it is possible to save this protocol
 */
bool subghz_txrx_protocol_is_serializable(SubGhzTxRx* instance);

/**
 * Is it possible to send this protocol
 * 
 * @param instance Pointer to a SubGhzTxRx 
 * @return bool True if it is possible to send this protocol
 */
bool subghz_txrx_protocol_is_transmittable(SubGhzTxRx* instance, bool check_type);

/**
 * Set filter, what types of decoder to use 
 * 
 * @param instance Pointer to a SubGhzTxRx
 * @param filter Filter
 */
void subghz_txrx_receiver_set_filter(SubGhzTxRx* instance, SubGhzProtocolFlag filter);

/**
 * Set callback for receive data
 * 
 * @param instance Pointer to a SubGhzTxRx
 * @param callback Callback for receive data
 * @param context Context for callback
 */
void subghz_txrx_set_rx_callback(
    SubGhzTxRx* instance,
    SubGhzReceiverCallback callback,
    void* context);

/**
 * Set callback for Raw decoder, end of data transfer  
 * 
 * @param instance Pointer to a SubGhzTxRx
 * @param callback Callback for Raw decoder, end of data transfer 
 * @param context Context for callback
 */
void subghz_txrx_set_raw_file_encoder_worker_callback_end(
    SubGhzTxRx* instance,
    SubGhzProtocolEncoderRAWCallbackEnd callback,
    void* context);

/* Checking if an external radio device is connected
* 
* @param instance Pointer to a SubGhzTxRx
* @param name Name of external radio device
* @return bool True if is connected to the external radio device
*/
bool subghz_txrx_radio_device_is_external_connected(SubGhzTxRx* instance, const char* name);

/* Set the selected radio device to use
*
* @param instance Pointer to a SubGhzTxRx
* @param radio_device_type Radio device type
* @return SubGhzRadioDeviceType Type of installed radio device
*/
SubGhzRadioDeviceType
    subghz_txrx_radio_device_set(SubGhzTxRx* instance, SubGhzRadioDeviceType radio_device_type);

/* Probe and restore the preferred external radio mode before starting RX.
*
* @param instance Pointer to a SubGhzTxRx
* @return SubGhzRadioDeviceType Type of installed radio device
*/
SubGhzRadioDeviceType subghz_txrx_radio_device_reprobe_preferred(SubGhzTxRx* instance);

/* Fall back to the internal radio without overwriting the preferred mode.
*
* @param instance Pointer to a SubGhzTxRx
* @return SubGhzRadioDeviceType Type of installed radio device
*/
SubGhzRadioDeviceType subghz_txrx_radio_device_fallback_internal(SubGhzTxRx* instance);

/* Get the selected radio device to use
*
* @param instance Pointer to a SubGhzTxRx
* @return SubGhzRadioDeviceType Type of installed radio device
*/
SubGhzRadioDeviceType subghz_txrx_radio_device_get(SubGhzTxRx* instance);

/**
 * Return the radio which produced the most recent decoded frame.
 *
 * @param instance Pointer to a SubGhzTxRx
 * @return Internal or external radio source
 */
SubGhzRadioDeviceType subghz_txrx_radio_device_get_last_rx(SubGhzTxRx* instance);

/* Get RSSI the selected radio device to use
*
* @param instance Pointer to a SubGhzTxRx
* @return float RSSI
*/
float subghz_txrx_radio_device_get_rssi(SubGhzTxRx* instance);

/**
 * Return the RSSI captured with the most recent decoded frame.
 *
 * @param instance Pointer to a SubGhzTxRx
 * @return RSSI in dBm
 */
float subghz_txrx_radio_device_get_last_rx_rssi(SubGhzTxRx* instance);

/**
 * Return the secondary decoder selected by name in dual receive mode.
 *
 * @param instance Pointer to a SubGhzTxRx
 * @return Secondary decoder or NULL outside dual receive mode
 */
SubGhzProtocolDecoderBase* subghz_txrx_get_diversity_decoder(SubGhzTxRx* instance);

/* Get name the selected radio device to use
*
* @param instance Pointer to a SubGhzTxRx
* @return const char* Name of installed radio device
*/
const char* subghz_txrx_radio_device_get_name(SubGhzTxRx* instance);

/* Get intelligence whether frequency the selected radio device to use
*
* @param instance Pointer to a SubGhzTxRx
* @return bool True if the frequency is valid
*/
bool subghz_txrx_radio_device_is_frequency_valid(SubGhzTxRx* instance, uint32_t frequency);

/** Start a bounded receive-only analyzer probe with the selected preset. */
bool subghz_txrx_analyzer_begin(SubGhzTxRx* instance, size_t preset_index, uint32_t frequency);

/** Stop a receive-only analyzer probe and leave the selected radio idle. */
void subghz_txrx_analyzer_end(SubGhzTxRx* instance);

bool subghz_txrx_radio_device_is_tx_allowed(SubGhzTxRx* instance, uint32_t frequency);

void subghz_txrx_set_debug_pin_state(SubGhzTxRx* instance, bool state);
bool subghz_txrx_get_debug_pin_state(SubGhzTxRx* instance);

void subghz_txrx_reset_dynamic_and_custom_btns(SubGhzTxRx* instance);

/** Reset all decoder instances owned by this RX session.
 *
 * In diversity mode the primary and secondary receivers keep independent parser
 * state, so both must be reset before asynchronous RX is restarted.
 */
void subghz_txrx_receiver_reset(SubGhzTxRx* instance);

SubGhzReceiver* subghz_txrx_get_receiver(SubGhzTxRx* instance); // TODO use only in DecodeRaw

/** Feed one sample to the decoders and count it against the decoded air-time clock. */
void subghz_txrx_decode(SubGhzTxRx* instance, bool level, uint32_t duration);

/** Return decoded air time in milliseconds; wall-clock pauses are excluded. */
uint32_t subghz_txrx_get_air_time_ms(SubGhzTxRx* instance);

/**
 * @brief Set current preset AM650 without additional params
 * 
 * @param instance - instance Pointer to a SubGhzTxRx
 * @param frequency - frequency of preset, if pass 0 then taking default frequency 433.92MHz
 */
void subghz_txrx_set_default_preset(SubGhzTxRx* instance, uint32_t frequency);

/**
 * @brief Set current preset by index
 * 
 * @param instance  - instance Pointer to a SubGhzTxRx
 * @param frequency - frequency of new preset
 * @param index - index of preset taken from SubGhzSetting
 * @param tx_power - index of TX Power menu index option to use.
 * @return const char* -  name of preset
 */
const char* subghz_txrx_set_preset_internal(
    SubGhzTxRx* instance,
    uint32_t frequency,
    uint8_t index,
    uint8_t tx_power);
