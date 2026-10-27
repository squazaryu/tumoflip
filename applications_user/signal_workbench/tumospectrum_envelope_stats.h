#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TUMO_ENVELOPE_SAMPLES 128U
#define TUMO_ENVELOPE_HISTORY 24U
#define TUMO_ENVELOPE_RSSI_MIN (-138)

// RSSI envelope evidence, NOT a modulation/protocol identification.
typedef enum {
    TumoEnvelopeLearning,
    TumoEnvelopeNoSignal,
    TumoEnvelopePulsed,
    TumoEnvelopeFlat,
    TumoEnvelopeUnclear,
    TumoEnvelopeTooStrong,
    TumoEnvelopeInvalid,
} TumoEnvelopeKind;

typedef struct {
    TumoEnvelopeKind kind;
    int16_t low_dbm;
    int16_t high_dbm;
    uint8_t transitions;
} TumoEnvelopeWindow;

typedef struct {
    TumoEnvelopeKind history[TUMO_ENVELOPE_HISTORY];
    uint8_t count;
    uint8_t next;
    TumoEnvelopeKind kind;
    uint8_t agreement;
} TumoEnvelopeSession;

bool tumo_envelope_analyze(
    const int16_t* samples,
    size_t count,
    int16_t floor_dbm,
    TumoEnvelopeWindow* out);
void tumo_envelope_session_reset(TumoEnvelopeSession* session);
void tumo_envelope_session_add(TumoEnvelopeSession* session, const TumoEnvelopeWindow* window);
const char* tumo_envelope_kind_name(TumoEnvelopeKind kind);
