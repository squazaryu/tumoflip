#pragma once

#include "subghz_txrx.h"
#include <subghz_radio_broker/subghz_radio_broker.h>
#include <lib/subghz/protocols/plugin_registry.h>

struct SubGhzTxRx {
    SubGhzWorker* worker;

    SubGhzRadioBroker* radio_broker;
    SubGhzRadioBrokerLease radio_lease;

    SubGhzEnvironment* environment;
    SubGhzProtocolPackRegistry* protocol_pack_registry;
    SubGhzProtocolPackGroup protocol_pack_group;
    SubGhzReceiver* receiver;
    SubGhzTransmitter* transmitter;
    SubGhzProtocolDecoderBase* decoder_result;
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
    SubGhzRadioDeviceType radio_device_type;

    SubGhzTxRxNeedSaveCallback need_save_callback;
    void* need_save_context;
    SubGhzReceiverCallback rx_callback;
    void* rx_context;
    SubGhzProtocolFlag receiver_filter;

    bool debug_pin_state;
};
