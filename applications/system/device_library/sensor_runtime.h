#pragma once
#include "sensor_cards.h"
#include <furi.h>
#include <lib/subghz/devices/devices.h>
#include <lib/subghz/environment.h>
#include <lib/subghz/receiver.h>
#include <subghz_radio_broker/subghz_radio_broker.h>

typedef struct {
    SensorCards* cards;
    SubGhzRadioBroker* broker;
    SubGhzRadioBrokerLease lease;
    const SubGhzDevice* device;
    SubGhzEnvironment* environment;
    SubGhzReceiver* receiver;
    FlipperFormat* format;
    FuriStreamBuffer* stream;
    uint32_t frequency;
    uint32_t lost;
    volatile bool overrun;
    bool devices;
    bool listening;
} SensorRuntime;

bool sensor_runtime_start(SensorRuntime* runtime, SensorCards* cards, uint32_t frequency);
void sensor_runtime_stop(SensorRuntime* runtime);
bool sensor_runtime_tick(SensorRuntime* runtime);
