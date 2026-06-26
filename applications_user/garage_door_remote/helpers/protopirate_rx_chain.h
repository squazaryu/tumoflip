// helpers/protopirate_rx_chain.h
#pragma once

#include "protopirate_types.h"

#if defined(ENABLE_DUAL_RX_SCENE) || defined(ENABLE_SHIELD_RX_SCENE)

#include <lib/subghz/subghz_worker.h>
#include <lib/subghz/receiver.h>
#include <lib/subghz/subghz_setting.h>
#include <lib/subghz/devices/devices.h>
#include <lib/subghz/types.h>
#include <lib/flipper_application/plugins/plugin_manager.h>
#include <lib/flipper_application/plugins/composite_resolver.h>

#include "radio_device_loader.h"
#include "../protocols/protocol_items.h"
#include "../protocols/protopirate_protocol_plugins.h"

typedef struct {
    char label; // 'A' or 'B' (display tag)
    const SubGhzDevice* device;
    bool is_external;

    SubGhzWorker* worker;
    SubGhzReceiver* receiver;
    SubGhzEnvironment* environment;

    CompositeApiResolver* resolver;
    PluginManager* plugin_manager;
    const ProtoPirateProtocolPlugin* plugin;
    const SubGhzProtocolRegistry* registry;
    ProtoPirateProtocolRegistryFilter filter;

    SubGhzRadioPreset preset; // .name is an owned FuriString
    uint8_t* base_preset_data;
    size_t base_preset_data_size;

    uint8_t* owned_preset_data;
    uint32_t frequency;
    uint32_t rx_bandwidth_hz;

    ProtoPirateTxRxState state;
} ProtoPirateRxChain;

ProtoPirateRxChain* protopirate_rx_chain_alloc(char label);

void protopirate_rx_chain_free(ProtoPirateRxChain* chain);

bool protopirate_rx_chain_acquire_device(
    ProtoPirateRxChain* chain,
    SubGhzRadioDeviceType type);

bool protopirate_rx_chain_set_preset(
    ProtoPirateRxChain* chain,
    SubGhzSetting* setting,
    const char* preset_name,
    uint32_t frequency);

bool protopirate_rx_chain_set_preset_data(
    ProtoPirateRxChain* chain,
    const char* preset_name,
    uint8_t* preset_data,
    size_t preset_data_size,
    uint32_t frequency);

bool protopirate_rx_chain_apply_shield_profile(ProtoPirateRxChain* chain);

bool protopirate_rx_chain_init_receiver(ProtoPirateRxChain* chain);

void protopirate_rx_chain_set_decode_callback(
    ProtoPirateRxChain* chain,
    SubGhzReceiverCallback callback,
    void* context);

bool protopirate_rx_chain_start(ProtoPirateRxChain* chain);

void protopirate_rx_chain_stop(ProtoPirateRxChain* chain);

float protopirate_rx_chain_get_rssi(ProtoPirateRxChain* chain);

#endif // ENABLE_DUAL_RX_SCENE || ENABLE_SHIELD_RX_SCENE
