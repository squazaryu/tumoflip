#include "tumospectrum_envelope_stats.h"

#include <string.h>

// Inspired by doctor-firmware / ARF Mod Analyzer (6958d6abb2). Deliberately
// reports amplitude evidence only: a flat envelope does not prove FSK/FM.
static bool tumo_envelope_level(const int16_t* samples, size_t count, size_t i, int16_t mid) {
    const bool current = samples[i] > mid;
    if(i == 0 || i + 1 == count) return current;
    const bool before = samples[i - 1] > mid;
    const bool after = samples[i + 1] > mid;
    return (before && current) || (current && after) || (before && after);
}

bool tumo_envelope_analyze(
    const int16_t* samples,
    size_t count,
    int16_t floor_dbm,
    TumoEnvelopeWindow* out) {
    if(!out) return false;
    *out = (TumoEnvelopeWindow){.kind = TumoEnvelopeInvalid};
    if(!samples || count < 16 || count > TUMO_ENVELOPE_SAMPLES || floor_dbm < TUMO_ENVELOPE_RSSI_MIN ||
       floor_dbm > -1) {
        return false;
    }

    int16_t sorted[TUMO_ENVELOPE_SAMPLES];
    for(size_t i = 0; i < count; i++) {
        if(samples[i] < TUMO_ENVELOPE_RSSI_MIN || samples[i] > -1) return false;
        sorted[i] = samples[i];
        for(size_t j = i; j > 0 && sorted[j] < sorted[j - 1]; j--) {
            const int16_t value = sorted[j];
            sorted[j] = sorted[j - 1];
            sorted[j - 1] = value;
        }
    }
    out->low_dbm = sorted[count / 10];
    out->high_dbm = sorted[(count * 9) / 10];
    const int16_t depth = out->high_dbm - out->low_dbm;
    if(out->high_dbm > -30) {
        out->kind = TumoEnvelopeTooStrong;
        return true;
    }
    if(out->high_dbm - floor_dbm < 10) {
        out->kind = TumoEnvelopeNoSignal;
        return true;
    }

    const int16_t mid = (out->low_dbm + out->high_dbm) / 2;
    bool previous = tumo_envelope_level(samples, count, 0, mid);
    size_t run_start = 0;
    int16_t run_min = samples[0];
    uint8_t quiet_runs = 0;
    for(size_t i = 1; i <= count; i++) {
        const bool current = i < count ? tumo_envelope_level(samples, count, i, mid) : !previous;
        if(current == previous && i < count) {
            if(samples[i] < run_min) run_min = samples[i];
            continue;
        }
        // Do not include the first sample of the NEXT run in this run's floor.
        if(!previous && run_start > 0 && i < count && i - run_start >= 2 &&
           run_min <= floor_dbm + 6) {
            quiet_runs++;
        }
        if(i < count) out->transitions++;
        previous = current;
        run_start = i;
        if(i < count) run_min = samples[i];
    }

    if(depth >= 10 && quiet_runs >= 2 && out->transitions >= 4) {
        out->kind = TumoEnvelopePulsed;
    } else if(depth <= 6) {
        out->kind = TumoEnvelopeFlat;
    } else {
        out->kind = TumoEnvelopeUnclear;
    }
    return true;
}

void tumo_envelope_session_reset(TumoEnvelopeSession* session) {
    if(!session) return;
    memset(session, 0, sizeof(*session));
    session->kind = TumoEnvelopeLearning;
}

void tumo_envelope_session_add(TumoEnvelopeSession* session, const TumoEnvelopeWindow* window) {
    if(!session || !window) return;
    if(window->kind == TumoEnvelopeNoSignal || window->kind == TumoEnvelopeTooStrong ||
       window->kind == TumoEnvelopeInvalid) {
        tumo_envelope_session_reset(session);
        session->kind = window->kind;
        return;
    }
    session->history[session->next] = window->kind;
    session->next = (session->next + 1U) % TUMO_ENVELOPE_HISTORY;
    if(session->count < TUMO_ENVELOPE_HISTORY) session->count++;
    session->agreement = 0;
    session->kind = TumoEnvelopeLearning;
    if(session->count < 6) return;
    unsigned pulsed = 0;
    unsigned flat = 0;
    for(unsigned i = 0; i < session->count; i++) {
        pulsed += session->history[i] == TumoEnvelopePulsed;
        flat += session->history[i] == TumoEnvelopeFlat;
    }
    const unsigned agree = 100U * (pulsed > flat ? pulsed : flat) / session->count;
    session->kind = TumoEnvelopeUnclear;
    if(agree >= 80) {
        session->kind = pulsed > flat ? TumoEnvelopePulsed : TumoEnvelopeFlat;
        session->agreement = agree;
    }
}

const char* tumo_envelope_kind_name(TumoEnvelopeKind kind) {
    switch(kind) {
    case TumoEnvelopeNoSignal:
        return "No signal";
    case TumoEnvelopePulsed:
        return "Pulsed";
    case TumoEnvelopeFlat:
        return "Flat carrier";
    case TumoEnvelopeTooStrong:
        return "Move away";
    case TumoEnvelopeUnclear:
        return "Unclear";
    case TumoEnvelopeInvalid:
        return "Bad sample";
    default:
        return "Measuring";
    }
}
