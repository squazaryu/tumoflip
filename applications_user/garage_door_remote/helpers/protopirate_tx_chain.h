// helpers/protopirate_tx_chain.h
#pragma once

#include "protopirate_types.h"

#ifdef ENABLE_SHIELD_RX_SCENE

#include <lib/subghz/subghz_setting.h>
#include <lib/subghz/devices/devices.h>

#include "radio_device_loader.h"

typedef struct {
    const SubGhzDevice* device;
    const GpioPin* data_gpio;
    uint8_t* preset_data;
    size_t preset_data_size;
    FuriString* preset_name;
    uint32_t frequency;
    ProtoPirateTxRxState state;
} ProtoPirateTxChain;

ProtoPirateTxChain* protopirate_tx_chain_alloc(void);
void protopirate_tx_chain_free(ProtoPirateTxChain* chain);

bool protopirate_tx_chain_acquire_device(ProtoPirateTxChain* chain);

bool protopirate_tx_chain_configure(
    ProtoPirateTxChain* chain,
    SubGhzSetting* setting,
    uint32_t rx_frequency,
    int32_t offset_hz,
    uint8_t tx_power);

bool protopirate_tx_chain_start_carrier(ProtoPirateTxChain* chain);
void protopirate_tx_chain_stop(ProtoPirateTxChain* chain);

#endif // ENABLE_SHIELD_RX_SCENE
