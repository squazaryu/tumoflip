#pragma once

#include "protocol_profile_package.h"
#include "tumospectrum_inference.h"

typedef enum {
    TumoSpectrumProfileBuildOk = 0,
    TumoSpectrumProfileBuildInvalidArgument,
    TumoSpectrumProfileBuildNameInvalid,
    TumoSpectrumProfileBuildSubGhzRawRequired,
    TumoSpectrumProfileBuildNeedMoreSamples,
    TumoSpectrumProfileBuildInferenceRequired,
    TumoSpectrumProfileBuildUnsupportedEncoding,
    TumoSpectrumProfileBuildFrequencyMissing,
    TumoSpectrumProfileBuildFrequencyMismatch,
    TumoSpectrumProfileBuildFrameMismatch,
    TumoSpectrumProfileBuildTooManyBits,
    TumoSpectrumProfileBuildAmbiguousSymbols,
    TumoSpectrumProfileBuildUnsafeProfile,
} TumoSpectrumProfileBuildStatus;

TumoSpectrumProfileBuildStatus tumospectrum_profile_builder_build(
    const TumoSpectrumCaptureSet* capture_set,
    const char* name,
    uint32_t current_api,
    ProtocolProfilePackage* package);

const char* tumospectrum_profile_builder_status_name(TumoSpectrumProfileBuildStatus status);
