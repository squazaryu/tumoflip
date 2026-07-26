#pragma once

#include "tumospectrum_types.h"

#include <storage/storage.h>

#define TUMOSPECTRUM_BAND_SESSION_MAX_SAMPLES 4U

typedef struct {
    uint32_t frequency_hz;
    size_t sample_count;
    char sample_paths[TUMOSPECTRUM_BAND_SESSION_MAX_SAMPLES][TUMOSPECTRUM_PATH_SIZE];
} TumoSpectrumBandSession;

bool tumospectrum_band_session_load(Storage* storage, TumoSpectrumBandSession* session);
bool tumospectrum_band_session_save(Storage* storage, const TumoSpectrumBandSession* session);
bool tumospectrum_band_session_append(
    Storage* storage,
    uint32_t frequency_hz,
    const char* path,
    TumoSpectrumBandSession* session);
bool tumospectrum_band_session_clear(Storage* storage);
