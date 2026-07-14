#pragma once

#include "tumospectrum_types.h"

#include <storage/storage.h>

TumoSpectrumStatus tumospectrum_parse_subghz(
    Storage* storage,
    const char* path,
    TumoSpectrumCapture* capture);
TumoSpectrumStatus tumospectrum_parse_infrared(
    Storage* storage,
    const char* path,
    TumoSpectrumCapture* capture);
TumoSpectrumStatus tumospectrum_parse_tumoscope(
    Storage* storage,
    const char* path,
    TumoSpectrumCapture* capture);
TumoSpectrumStatus tumospectrum_import_frequency_observation(
    Storage* storage,
    TumoSpectrumCapture* capture);
