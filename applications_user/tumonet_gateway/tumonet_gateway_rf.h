#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../tumonet_bench/radio/tumonet_radio.h"

typedef struct {
    uint8_t fragments;
    uint8_t retries;
    uint32_t frequency;
    TumoNetRadioResult radio_result;
    char status[24];
} TumoNetGatewayRfResult;

bool tumonet_gateway_rf_deliver(
    const uint8_t* message,
    size_t message_size,
    volatile bool* cancel_requested,
    TumoNetGatewayRfResult* result);
