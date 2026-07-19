#pragma once

#include <furi_hal.h>
#include "../subghz_i.h"

typedef struct SubGhzFrequencyAnalyzerWorker SubGhzFrequencyAnalyzerWorker;

#define SUBGHZ_FREQUENCY_ANALYZER_PRESET_NAME_MAX 20U

typedef enum {
    SubGhzFrequencyAnalyzerWorkerModeFrequency,
    SubGhzFrequencyAnalyzerWorkerModePreset,
} SubGhzFrequencyAnalyzerWorkerMode;

typedef struct {
    size_t preset_index;
    char preset_name[SUBGHZ_FREQUENCY_ANALYZER_PRESET_NAME_MAX];
    float peak_rssi;
    float average_rssi;
    float noise_floor;
    float score;
} SubGhzFrequencyAnalyzerPresetResult;

typedef struct {
    uint32_t frequency;
    size_t completed;
    size_t total;
    bool complete;
    bool valid;
    SubGhzFrequencyAnalyzerPresetResult result;
} SubGhzFrequencyAnalyzerPresetProgress;

typedef void (*SubGhzFrequencyAnalyzerWorkerPairCallback)(
    void* context,
    uint32_t frequency,
    float rssi,
    bool signal);

typedef void (*SubGhzFrequencyAnalyzerWorkerPresetCallback)(
    void* context,
    const SubGhzFrequencyAnalyzerPresetProgress* progress);

typedef struct {
    uint32_t frequency_coarse;
    float rssi_coarse;
    uint32_t frequency_fine;
    float rssi_fine;
} FrequencyRSSI;

/** Allocate SubGhzFrequencyAnalyzerWorker
 *
 * @param context SubGhz* context
 * @return SubGhzFrequencyAnalyzerWorker*
 */
SubGhzFrequencyAnalyzerWorker* subghz_frequency_analyzer_worker_alloc(void* context);

/** Free SubGhzFrequencyAnalyzerWorker
 *
 * @param instance SubGhzFrequencyAnalyzerWorker instance
 */
void subghz_frequency_analyzer_worker_free(SubGhzFrequencyAnalyzerWorker* instance);

/** Pair callback SubGhzFrequencyAnalyzerWorker
 *
 * @param instance SubGhzFrequencyAnalyzerWorker instance
 * @param callback SubGhzFrequencyAnalyzerWorkerOverrunCallback callback
 * @param context
 */
void subghz_frequency_analyzer_worker_set_pair_callback(
    SubGhzFrequencyAnalyzerWorker* instance,
    SubGhzFrequencyAnalyzerWorkerPairCallback callback,
    void* context);

void subghz_frequency_analyzer_worker_set_preset_callback(
    SubGhzFrequencyAnalyzerWorker* instance,
    SubGhzFrequencyAnalyzerWorkerPresetCallback callback,
    void* context);

void subghz_frequency_analyzer_worker_set_mode(
    SubGhzFrequencyAnalyzerWorker* instance,
    SubGhzFrequencyAnalyzerWorkerMode mode,
    uint32_t frequency);

size_t subghz_frequency_analyzer_worker_get_preset_result_count(
    SubGhzFrequencyAnalyzerWorker* instance);

bool subghz_frequency_analyzer_worker_get_preset_result(
    SubGhzFrequencyAnalyzerWorker* instance,
    size_t rank,
    SubGhzFrequencyAnalyzerPresetResult* result);

/** Start SubGhzFrequencyAnalyzerWorker
 *
 * @param instance SubGhzFrequencyAnalyzerWorker instance
 * @param txrx pointer to SubGhzTxRx
 */
void subghz_frequency_analyzer_worker_start(SubGhzFrequencyAnalyzerWorker* instance);

/** Stop SubGhzFrequencyAnalyzerWorker
 *
 * @param instance SubGhzFrequencyAnalyzerWorker instance
 */
void subghz_frequency_analyzer_worker_stop(SubGhzFrequencyAnalyzerWorker* instance);

/** Check if worker is running
 * @param instance SubGhzFrequencyAnalyzerWorker instance
 * @return bool - true if running
 */
bool subghz_frequency_analyzer_worker_is_running(SubGhzFrequencyAnalyzerWorker* instance);

/** Set RSSI trigger level
 *
 * @param instance SubGhzFrequencyAnalyzerWorker instance
 * @param value RSSI level
 */
void subghz_frequency_analyzer_worker_set_trigger_level(
    SubGhzFrequencyAnalyzerWorker* instance,
    float value);

/** Get RSSI trigger level
 *
 * @param instance SubGhzFrequencyAnalyzerWorker instance
 * @return RSSI trigger level
 */
float subghz_frequency_analyzer_worker_get_trigger_level(SubGhzFrequencyAnalyzerWorker* instance);

// Round up the frequency
uint32_t subghz_frequency_analyzer_get_nearest_frequency(
    SubGhzFrequencyAnalyzerWorker* instance,
    uint32_t input);
