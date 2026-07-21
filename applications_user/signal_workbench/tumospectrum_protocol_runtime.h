#pragma once

#include "protocol_profile_storage.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct TumoSpectrumProtocolRuntime TumoSpectrumProtocolRuntime;

typedef enum {
    TumoSpectrumProtocolRadioIdle,
    TumoSpectrumProtocolRadioListening,
    TumoSpectrumProtocolRadioBusy,
    TumoSpectrumProtocolRadioInvalidFrequency,
    TumoSpectrumProtocolRadioError,
} TumoSpectrumProtocolRadioStatus;

typedef struct {
    ProtocolProfileDecodeResult decode;
    uint64_t changed_mask;
    uint32_t match_count;
} TumoSpectrumProtocolObservation;

typedef void (*TumoSpectrumProtocolObservationCallback)(
    const TumoSpectrumProtocolObservation* observation,
    void* context);

TumoSpectrumProtocolRuntime* tumospectrum_protocol_runtime_alloc(void);
void tumospectrum_protocol_runtime_free(TumoSpectrumProtocolRuntime* runtime);

TumoSpectrumProtocolRadioStatus tumospectrum_protocol_runtime_start(
    TumoSpectrumProtocolRuntime* runtime,
    const ProtocolProfilePackage* package,
    uint32_t current_api,
    TumoSpectrumProtocolObservationCallback callback,
    void* callback_context);

void tumospectrum_protocol_runtime_stop(TumoSpectrumProtocolRuntime* runtime);
void tumospectrum_protocol_runtime_tick(TumoSpectrumProtocolRuntime* runtime);

bool tumospectrum_protocol_runtime_is_listening(const TumoSpectrumProtocolRuntime* runtime);
TumoSpectrumProtocolRadioStatus
    tumospectrum_protocol_runtime_status(const TumoSpectrumProtocolRuntime* runtime);
ProtocolProfileStatus
    tumospectrum_protocol_runtime_decode_status(const TumoSpectrumProtocolRuntime* runtime);
uint16_t tumospectrum_protocol_runtime_pulse_count(const TumoSpectrumProtocolRuntime* runtime);
float tumospectrum_protocol_runtime_rssi(const TumoSpectrumProtocolRuntime* runtime);
const char* tumospectrum_protocol_radio_status_name(TumoSpectrumProtocolRadioStatus status);
