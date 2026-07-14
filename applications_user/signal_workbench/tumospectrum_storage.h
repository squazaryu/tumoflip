#pragma once

#include "tumospectrum_types.h"

#include <furi.h>
#include <storage/storage.h>

#define TUMOSPECTRUM_DATA_DIR     EXT_PATH("apps_data/signal_workbench")
#define TUMOSPECTRUM_REPORT_DIR   TUMOSPECTRUM_DATA_DIR "/reports"
#define TUMOSPECTRUM_NOTEBOOK_DIR TUMOSPECTRUM_DATA_DIR "/notebook"
#define TUMOSPECTRUM_NOTEBOOK_CSV TUMOSPECTRUM_NOTEBOOK_DIR "/observations.csv"

bool tumospectrum_storage_prepare(Storage* storage);
bool tumospectrum_storage_save(
    Storage* storage,
    const TumoSpectrumCapture* capture,
    const TumoSpectrumCapture* compared,
    const TumoSpectrumComparison* comparison,
    char* report_path,
    size_t report_path_size);
bool tumospectrum_storage_load_latest(Storage* storage, FuriString* output);
void tumospectrum_storage_build_text(
    const TumoSpectrumCapture* capture,
    const TumoSpectrumCapture* compared,
    const TumoSpectrumComparison* comparison,
    FuriString* output);
