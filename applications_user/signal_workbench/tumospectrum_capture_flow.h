#pragma once

#include "tumospectrum_types.h"

#include <storage/storage.h>

#define TUMOSPECTRUM_CAPTURE_LAUNCH_ARG "tumospectrum_raw"

bool tumospectrum_capture_flow_prepare(Storage* storage, TumoSpectrumCaptureType type);

bool tumospectrum_capture_flow_resume(
    Storage* storage,
    TumoSpectrumCaptureType* type,
    char* path,
    size_t path_size,
    bool* needs_selection);
