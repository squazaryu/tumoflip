#pragma once

#include "tumospectrum_envelope_stats.h"

typedef struct TumoSpectrumEnvelope TumoSpectrumEnvelope;

typedef enum {
    TumoEnvelopeIdle,
    TumoEnvelopeRunning,
    TumoEnvelopeBusy,
    TumoEnvelopeMissing,
    TumoEnvelopeError,
} TumoEnvelopeStatus;

typedef struct {
    TumoEnvelopeStatus status;
    TumoEnvelopeKind kind;
    uint32_t frequency_hz;
    uint16_t sample_period_us;
    int16_t floor_dbm;
    int16_t high_dbm;
    uint8_t agreement;
    bool external;
    bool calibrated;
    int16_t samples[TUMO_ENVELOPE_SAMPLES];
} TumoEnvelopeSnapshot;

TumoSpectrumEnvelope* tumospectrum_envelope_alloc(void);
void tumospectrum_envelope_free(TumoSpectrumEnvelope* envelope);
bool tumospectrum_envelope_start(TumoSpectrumEnvelope* envelope, uint32_t frequency, bool external);
void tumospectrum_envelope_stop(TumoSpectrumEnvelope* envelope);
void tumospectrum_envelope_tick(TumoSpectrumEnvelope* envelope);
void tumospectrum_envelope_calibrate(TumoSpectrumEnvelope* envelope);
bool tumospectrum_envelope_tune(TumoSpectrumEnvelope* envelope, int direction);
bool tumospectrum_envelope_cycle_radio(TumoSpectrumEnvelope* envelope);
void tumospectrum_envelope_next_band(TumoSpectrumEnvelope* envelope);
void tumospectrum_envelope_snapshot(
    const TumoSpectrumEnvelope* envelope,
    TumoEnvelopeSnapshot* snapshot);
