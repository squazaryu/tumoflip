#pragma once

#include "tumospectrum_types.h"

#include <storage/storage.h>

#define TUMOSPECTRUM_CAPTURE_LAUNCH_ARG "tumospectrum_raw"

typedef enum {
    TumoSpectrumCaptureResumeResult,
    TumoSpectrumCaptureResumeBandMap,
} TumoSpectrumCaptureResume;

bool tumospectrum_capture_flow_prepare(Storage* storage, TumoSpectrumCaptureType type);

bool tumospectrum_capture_flow_prepare_route(
    Storage* storage,
    TumoSpectrumCaptureType type,
    TumoSpectrumCaptureResume resume,
    uint32_t frequency_hz);

bool tumospectrum_capture_flow_resume(
    Storage* storage,
    TumoSpectrumCaptureType* type,
    char* path,
    size_t path_size,
    bool* needs_selection);

bool tumospectrum_capture_flow_resume_route(
    Storage* storage,
    TumoSpectrumCaptureType* type,
    TumoSpectrumCaptureResume* resume,
    uint32_t* frequency_hz,
    char* path,
    size_t path_size,
    bool* needs_selection);
