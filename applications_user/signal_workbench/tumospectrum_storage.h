#pragma once

#include "tumospectrum_inference.h"

#include <furi.h>
#include <storage/storage.h>

#define TUMOSPECTRUM_DATA_DIR          EXT_PATH("apps_data/signal_workbench")
#define TUMOSPECTRUM_REPORT_DIR        TUMOSPECTRUM_DATA_DIR "/reports"
#define TUMOSPECTRUM_NOTEBOOK_DIR      TUMOSPECTRUM_DATA_DIR "/notebook"
#define TUMOSPECTRUM_NOTEBOOK_CSV      TUMOSPECTRUM_NOTEBOOK_DIR "/observations.csv"
#define TUMOSPECTRUM_SET_DIR           TUMOSPECTRUM_DATA_DIR "/capture_sets"
#define TUMOSPECTRUM_LATEST_SET        TUMOSPECTRUM_SET_DIR "/latest.tsp"
#define TUMOSPECTRUM_LATEST_SET_REPORT TUMOSPECTRUM_REPORT_DIR "/latest_set.json"

bool tumospectrum_storage_prepare(Storage* storage);
bool tumospectrum_storage_save(
    Storage* storage,
    const TumoSpectrumCapture* capture,
    const TumoSpectrumCapture* compared,
    const TumoSpectrumComparison* comparison,
    char* report_path,
    size_t report_path_size);
bool tumospectrum_storage_load_latest(Storage* storage, FuriString* output);
bool tumospectrum_storage_save_set(
    Storage* storage,
    const TumoSpectrumCaptureSet* capture_set,
    char* profile_path,
    size_t profile_path_size,
    char* report_path,
    size_t report_path_size);
bool tumospectrum_storage_load_latest_set(Storage* storage, TumoSpectrumCaptureSet* capture_set);
void tumospectrum_storage_build_set_text(
    const TumoSpectrumCaptureSet* capture_set,
    FuriString* output);
void tumospectrum_storage_build_text(
    const TumoSpectrumCapture* capture,
    const TumoSpectrumCapture* compared,
    const TumoSpectrumComparison* comparison,
    FuriString* output);
