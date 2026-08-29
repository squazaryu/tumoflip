#pragma once

#include <furi.h>
#include <furi_hal_subghz.h>
#include <lib/subghz/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RECORD_SUBGHZ_RADIO_BROKER                    "subghz_radio_broker"
#define SUBGHZ_RADIO_BROKER_OWNER_MAX                 31U
#define SUBGHZ_RADIO_BROKER_CAPABILITY_SCHEMA_VERSION 1U

typedef struct SubGhzRadioBroker SubGhzRadioBroker;

typedef enum {
    SubGhzRadioBrokerDeviceInternal,
    SubGhzRadioBrokerDeviceExternalCC1101,
    SubGhzRadioBrokerDeviceDual,
} SubGhzRadioBrokerDevice;

typedef enum {
    SubGhzRadioBrokerStateIdle,
    SubGhzRadioBrokerStateAcquired,
    SubGhzRadioBrokerStateProbing,
    SubGhzRadioBrokerStateInitialized,
    SubGhzRadioBrokerStateRx,
    SubGhzRadioBrokerStateTx,
    SubGhzRadioBrokerStateAsyncRx,
    SubGhzRadioBrokerStateAsyncTx,
    SubGhzRadioBrokerStateCleaningUp,
    SubGhzRadioBrokerStateExternalPowerOn,
    SubGhzRadioBrokerStateReleasing,
    SubGhzRadioBrokerStateError,
} SubGhzRadioBrokerState;

typedef enum {
    SubGhzRadioBrokerBandNone = 0,
    SubGhzRadioBrokerBand315 = (1U << 0),
    SubGhzRadioBrokerBand433 = (1U << 1),
    SubGhzRadioBrokerBand868 = (1U << 2),
} SubGhzRadioBrokerBand;

typedef enum {
    SubGhzRadioBrokerPresetNone = 0,
    SubGhzRadioBrokerPresetOok270 = (1U << 0),
    SubGhzRadioBrokerPresetOok650 = (1U << 1),
    SubGhzRadioBrokerPresetFsk238 = (1U << 2),
    SubGhzRadioBrokerPresetFsk12K = (1U << 3),
    SubGhzRadioBrokerPresetFsk476 = (1U << 4),
    SubGhzRadioBrokerPresetCustom = (1U << 5),
} SubGhzRadioBrokerPreset;

typedef enum {
    SubGhzRadioBrokerCapabilityDeviceNone = 0,
    SubGhzRadioBrokerCapabilityDeviceInternal = (1U << 0),
    SubGhzRadioBrokerCapabilityDeviceExternal = (1U << 1),
} SubGhzRadioBrokerCapabilityDevice;

typedef enum {
    SubGhzRadioBrokerDirectionNone = 0,
    SubGhzRadioBrokerDirectionReceive = (1U << 0),
    SubGhzRadioBrokerDirectionTransmit = (1U << 1),
} SubGhzRadioBrokerDirection;

typedef struct {
    uint8_t schema_version;
    uint8_t bands;
    uint8_t presets;
    uint8_t devices;
    uint8_t directions;
} SubGhzRadioBrokerProtocolCapability;

typedef enum {
    SubGhzRadioBrokerValidationOk,
    SubGhzRadioBrokerValidationInvalidProtocol,
    SubGhzRadioBrokerValidationInvalidFrequency,
    SubGhzRadioBrokerValidationUnsupportedBand,
    SubGhzRadioBrokerValidationUnsupportedPreset,
    SubGhzRadioBrokerValidationUnsupportedDevice,
    SubGhzRadioBrokerValidationReceiveUnsupported,
    SubGhzRadioBrokerValidationTransmitUnsupported,
} SubGhzRadioBrokerValidation;

static inline SubGhzRadioBrokerProtocolCapability
    subghz_radio_broker_protocol_capability(const SubGhzProtocol* protocol) {
    SubGhzRadioBrokerProtocolCapability capability = {
        .schema_version = SUBGHZ_RADIO_BROKER_CAPABILITY_SCHEMA_VERSION,
        .bands = SubGhzRadioBrokerBandNone,
        .presets = SubGhzRadioBrokerPresetNone,
        .devices = SubGhzRadioBrokerCapabilityDeviceInternal |
                   SubGhzRadioBrokerCapabilityDeviceExternal,
        .directions = SubGhzRadioBrokerDirectionNone,
    };
    if(!protocol) return capability;

    if(protocol->flag & SubGhzProtocolFlag_315) capability.bands |= SubGhzRadioBrokerBand315;
    if(protocol->flag & SubGhzProtocolFlag_433) capability.bands |= SubGhzRadioBrokerBand433;
    if(protocol->flag & SubGhzProtocolFlag_868) capability.bands |= SubGhzRadioBrokerBand868;

    if(protocol->flag & SubGhzProtocolFlag_AM) {
        capability.presets |= SubGhzRadioBrokerPresetOok270 | SubGhzRadioBrokerPresetOok650 |
                              SubGhzRadioBrokerPresetCustom;
    }
    if(protocol->flag & SubGhzProtocolFlag_FM) {
        capability.presets |= SubGhzRadioBrokerPresetFsk238 | SubGhzRadioBrokerPresetFsk12K |
                              SubGhzRadioBrokerPresetFsk476 | SubGhzRadioBrokerPresetCustom;
    }

    if((protocol->flag & SubGhzProtocolFlag_Decodable) && protocol->decoder) {
        capability.directions |= SubGhzRadioBrokerDirectionReceive;
    }
    if((protocol->flag & SubGhzProtocolFlag_Send) && protocol->encoder) {
        capability.directions |= SubGhzRadioBrokerDirectionTransmit;
    }
    return capability;
}

static inline SubGhzRadioBrokerPreset
    subghz_radio_broker_preset_from_short_name(const char* preset_name) {
    if(!preset_name || !preset_name[0]) return SubGhzRadioBrokerPresetNone;

    if(preset_name[0] == 'A' && preset_name[1] == 'M') {
        if(preset_name[2] == '2') return SubGhzRadioBrokerPresetOok270;
        if(preset_name[2] == '6') return SubGhzRadioBrokerPresetOok650;
    } else if(preset_name[0] == 'F' && preset_name[1] == 'M') {
        if(preset_name[2] == '2') return SubGhzRadioBrokerPresetFsk238;
        if(preset_name[2] == '1') return SubGhzRadioBrokerPresetFsk12K;
        if(preset_name[2] == '4') return SubGhzRadioBrokerPresetFsk476;
    } else if(preset_name[0] == 'C') {
        return SubGhzRadioBrokerPresetCustom;
    }
    return SubGhzRadioBrokerPresetNone;
}

static inline SubGhzRadioBrokerValidation subghz_radio_broker_validate_protocol(
    const SubGhzProtocol* protocol,
    uint32_t frequency,
    SubGhzRadioBrokerPreset preset,
    SubGhzRadioBrokerDevice device,
    bool transmit) {
    if(!protocol) return SubGhzRadioBrokerValidationInvalidProtocol;

    const SubGhzRadioBrokerProtocolCapability capability =
        subghz_radio_broker_protocol_capability(protocol);
    uint8_t device_mask = SubGhzRadioBrokerCapabilityDeviceNone;
    if(device == SubGhzRadioBrokerDeviceInternal) {
        device_mask = SubGhzRadioBrokerCapabilityDeviceInternal;
    } else if(device == SubGhzRadioBrokerDeviceExternalCC1101) {
        device_mask = SubGhzRadioBrokerCapabilityDeviceExternal;
    } else if(device == SubGhzRadioBrokerDeviceDual) {
        device_mask = SubGhzRadioBrokerCapabilityDeviceInternal |
                      SubGhzRadioBrokerCapabilityDeviceExternal;
    }
    if(!device_mask || !(capability.devices & device_mask)) {
        return SubGhzRadioBrokerValidationUnsupportedDevice;
    }

    if(!furi_hal_subghz_is_frequency_valid(frequency)) {
        return SubGhzRadioBrokerValidationInvalidFrequency;
    }
    const SubGhzRadioBrokerBand band = frequency <= 361000000U ? SubGhzRadioBrokerBand315 :
                                       frequency <= 481000000U ? SubGhzRadioBrokerBand433 :
                                                                 SubGhzRadioBrokerBand868;
    if(!(capability.bands & band)) return SubGhzRadioBrokerValidationUnsupportedBand;

    if((preset == SubGhzRadioBrokerPresetNone) || !(capability.presets & preset)) {
        return SubGhzRadioBrokerValidationUnsupportedPreset;
    }

    if(transmit && !(capability.directions & SubGhzRadioBrokerDirectionTransmit)) {
        return SubGhzRadioBrokerValidationTransmitUnsupported;
    }
    if(!transmit && !(capability.directions & SubGhzRadioBrokerDirectionReceive)) {
        return SubGhzRadioBrokerValidationReceiveUnsupported;
    }
    return SubGhzRadioBrokerValidationOk;
}

typedef struct {
    uint32_t token;
} SubGhzRadioBrokerLease;

typedef struct {
    bool busy;
    bool external_powered;
    SubGhzRadioBrokerDevice selected_device;
    char owner[SUBGHZ_RADIO_BROKER_OWNER_MAX + 1];
} SubGhzRadioBrokerStatus;

typedef struct {
    SubGhzRadioBrokerStatus base;
    SubGhzRadioBrokerState state;
} SubGhzRadioBrokerStatusV2;

bool subghz_radio_broker_acquire(
    SubGhzRadioBroker* broker,
    const char* owner,
    uint32_t timeout,
    SubGhzRadioBrokerLease* lease);

void subghz_radio_broker_release(SubGhzRadioBroker* broker, SubGhzRadioBrokerLease* lease);

bool subghz_radio_broker_external_power_on(
    SubGhzRadioBroker* broker,
    const SubGhzRadioBrokerLease* lease);

void subghz_radio_broker_external_power_off(
    SubGhzRadioBroker* broker,
    const SubGhzRadioBrokerLease* lease);

bool subghz_radio_broker_set_selected_device(
    SubGhzRadioBroker* broker,
    const SubGhzRadioBrokerLease* lease,
    SubGhzRadioBrokerDevice device);

bool subghz_radio_broker_set_state(
    SubGhzRadioBroker* broker,
    const SubGhzRadioBrokerLease* lease,
    SubGhzRadioBrokerState state);

void subghz_radio_broker_get_status(SubGhzRadioBroker* broker, SubGhzRadioBrokerStatus* status);

void subghz_radio_broker_get_status_v2(
    SubGhzRadioBroker* broker,
    SubGhzRadioBrokerStatusV2* status);

#ifdef __cplusplus
}
#endif
