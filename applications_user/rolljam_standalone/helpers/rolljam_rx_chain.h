// helpers/rolljam_rx_chain.h
#pragma once

#include "rolljam_types.h"

#if defined(ENABLE_DUAL_RX_SCENE) || defined(ENABLE_SHIELD_RX_SCENE)

#include <lib/subghz/subghz_worker.h>
#include <lib/subghz/receiver.h>
#include <lib/subghz/subghz_setting.h>
#include <lib/subghz/devices/devices.h>
#include <lib/subghz/types.h>
#include <lib/flipper_application/plugins/plugin_manager.h>
#include <lib/flipper_application/plugins/composite_resolver.h>

#include "helpers/radio_device_loader.h"
#include "../protocols/protocol_items.h"
#include "../protocols/rolljam_protocol_plugins.h"

typedef struct {
    char label; // 'A' or 'B' (display tag)
    const SubGhzDevice* device;
    bool is_external;

    SubGhzWorker* worker;
    SubGhzReceiver* receiver;
    SubGhzEnvironment* environment;

    CompositeApiResolver* resolver;
    PluginManager* plugin_manager;
    const RollJamProtocolPlugin* plugin;
    const SubGhzProtocolRegistry* registry;
    SubGhzProtocolRegistry* merged_registry;
    RollJamProtocolRegistryFilter filter;

    // Support for multiple plugins to avoid memory limits
    CompositeApiResolver* resolvers[4];
    PluginManager* managers[4];
    const RollJamProtocolPlugin* plugins[4];
    uint8_t plugin_count;

    SubGhzRadioPreset preset; // .name is an owned FuriString
    uint8_t* base_preset_data;
    size_t base_preset_data_size;

    uint8_t* owned_preset_data;
    uint32_t frequency;
    uint32_t rx_bandwidth_hz;

    RollJamTxRxState state;
} RollJamRxChain;

RollJamRxChain* rolljam_rx_chain_alloc(char label);

void rolljam_rx_chain_free(RollJamRxChain* chain);

bool rolljam_rx_chain_acquire_device(
    RollJamRxChain* chain,
    SubGhzRadioDeviceType type);

bool rolljam_rx_chain_set_preset(
    RollJamRxChain* chain,
    SubGhzSetting* setting,
    const char* preset_name,
    uint32_t frequency);

bool rolljam_rx_chain_set_preset_data(
    RollJamRxChain* chain,
    const char* preset_name,
    uint8_t* preset_data,
    size_t preset_data_size,
    uint32_t frequency);

bool rolljam_rx_chain_apply_shield_profile(RollJamRxChain* chain);

bool rolljam_rx_chain_init_receiver(RollJamRxChain* chain);

void rolljam_rx_chain_set_decode_callback(
    RollJamRxChain* chain,
    SubGhzReceiverCallback callback,
    void* context);

bool rolljam_rx_chain_start(RollJamRxChain* chain);

void rolljam_rx_chain_stop(RollJamRxChain* chain);

float rolljam_rx_chain_get_rssi(RollJamRxChain* chain);

#endif // ENABLE_DUAL_RX_SCENE || ENABLE_SHIELD_RX_SCENE
