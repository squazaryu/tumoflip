#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    TumoNetRadioResultOk,
    TumoNetRadioResultBusy,
    TumoNetRadioResultExternalPower,
    TumoNetRadioResultExternalDriver,
    TumoNetRadioResultNoExternal,
    TumoNetRadioResultExternalBegin,
    TumoNetRadioResultRegion,
    TumoNetRadioResultInit,
    TumoNetRadioResultTx,
    TumoNetRadioResultTxStartTimeout,
    TumoNetRadioResultTxEndTimeout,
    TumoNetRadioResultRxTimeout,
    TumoNetRadioResultCrc,
    TumoNetRadioResultCancelled,
} TumoNetRadioResult;

typedef enum {
    TumoNetRadioDirectionInternalToExternal,
    TumoNetRadioDirectionExternalToInternal,
} TumoNetRadioDirection;

typedef struct TumoNetRadio TumoNetRadio;

TumoNetRadio* tumonet_radio_alloc(void);
void tumonet_radio_free(TumoNetRadio* radio);

TumoNetRadioResult tumonet_radio_open(TumoNetRadio* radio);
TumoNetRadioResult tumonet_radio_open_with_owner(TumoNetRadio* radio, const char* owner);
void tumonet_radio_close(TumoNetRadio* radio);

TumoNetRadioResult tumonet_radio_transfer(
    TumoNetRadio* radio,
    TumoNetRadioDirection direction,
    const uint8_t* frame,
    uint8_t frame_size,
    uint8_t* received,
    uint8_t* received_size,
    const volatile bool* cancel_requested);

uint32_t tumonet_radio_frequency(const TumoNetRadio* radio);
bool tumonet_radio_result_is_retryable(TumoNetRadioResult result);
const char* tumonet_radio_result_name(TumoNetRadioResult result);
