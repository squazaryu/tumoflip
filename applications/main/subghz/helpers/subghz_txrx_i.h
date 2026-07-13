#pragma once

#include "subghz_txrx.h"
#include <subghz_radio_broker/subghz_radio_broker.h>
#include <lib/subghz/protocols/plugin_registry_i.h>

const SubGhzProtocolPackReport* subghz_txrx_get_protocol_pack_report(SubGhzTxRx* instance);

typedef struct {
    SubGhzTxRx* instance;
    SubGhzRadioDeviceType source;
} SubGhzTxRxReceiverContext;

struct SubGhzTxRx {
    SubGhzWorker* worker;
    SubGhzWorker* diversity_worker;

    SubGhzRadioBroker* radio_broker;
    SubGhzRadioBrokerLease radio_lease;

    SubGhzEnvironment* environment;
    SubGhzProtocolPackRegistry* protocol_pack_registry;
    SubGhzProtocolPackGroup protocol_pack_group;
    SubGhzReceiver* receiver;
    SubGhzReceiver* diversity_receiver;
    SubGhzTransmitter* transmitter;
    SubGhzProtocolDecoderBase* decoder_result;
    SubGhzProtocolDecoderBase* diversity_decoder_result;
    FlipperFormat* fff_data;

    SubGhzRadioPreset* preset;
    SubGhzSetting* setting;

    uint8_t hopper_timeout;
    uint8_t hopper_hold_ticks;
    uint8_t hopper_idx_frequency;
    bool is_database_loaded;
    SubGhzHopperState hopper_state;

    size_t preset_hopper_idx;

    SubGhzTxRxState txrx_state;
    SubGhzSpeakerState speaker_state;
    const SubGhzDevice* radio_device;
    const SubGhzDevice* diversity_radio_device;
    SubGhzRadioDeviceType radio_device_type;
    SubGhzRadioDeviceType preferred_radio_device_type;
    SubGhzRadioDeviceType last_rx_device_type;
    float last_rx_rssi;
    uint32_t last_rx_tick;
    uint8_t last_rx_hash;
    bool diversity_rx_active;

    FuriMutex* rx_callback_mutex;
    SubGhzTxRxReceiverContext primary_receiver_context;
    SubGhzTxRxReceiverContext diversity_receiver_context;

    SubGhzTxRxNeedSaveCallback need_save_callback;
    void* need_save_context;
    SubGhzReceiverCallback rx_callback;
    void* rx_context;
    SubGhzProtocolFlag receiver_filter;

    bool debug_pin_state;
};
