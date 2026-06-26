// helpers/rolljam_tx_chain.h
#pragma once

#include "rolljam_types.h"

#ifdef ENABLE_SHIELD_RX_SCENE

#include <lib/subghz/subghz_setting.h>
#include <lib/subghz/devices/devices.h>

#include "helpers/radio_device_loader.h"

typedef struct {
    const SubGhzDevice* device;
    const GpioPin* data_gpio;
    uint8_t* preset_data;
    size_t preset_data_size;
    FuriString* preset_name;
    uint32_t frequency;
    RollJamTxRxState state;
} RollJamTxChain;

RollJamTxChain* rolljam_tx_chain_alloc(void);
void rolljam_tx_chain_free(RollJamTxChain* chain);

bool rolljam_tx_chain_acquire_device(RollJamTxChain* chain);

bool rolljam_tx_chain_configure(
    RollJamTxChain* chain,
    SubGhzSetting* setting,
    uint32_t rx_frequency,
    int32_t offset_hz,
    uint8_t tx_power);

bool rolljam_tx_chain_start_carrier(RollJamTxChain* chain);
void rolljam_tx_chain_stop(RollJamTxChain* chain);

#endif // ENABLE_SHIELD_RX_SCENE
