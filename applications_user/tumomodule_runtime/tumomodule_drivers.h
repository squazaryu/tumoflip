#pragma once

#include <expansion/expansion.h>

#include "tumomodule_core.h"

typedef enum {
    TumoModuleProbeIdle = 0,
    TumoModuleProbeRunning,
    TumoModuleProbeDetected,
    TumoModuleProbeMissing,
    TumoModuleProbeBusy,
    TumoModuleProbeIncompatible,
    TumoModuleProbeError,
} TumoModuleProbeStatus;

typedef struct {
    Expansion* expansion;
} TumoModuleDriverContext;

typedef struct {
    TumoModuleProbeStatus status;
    char detail[48];
} TumoModuleProbeResult;

void tumomodule_driver_probe(
    const TumoModuleManifest* manifest,
    TumoModuleDriverContext* context,
    TumoModuleProbeResult* result);

const char* tumomodule_probe_status_name(TumoModuleProbeStatus status);
