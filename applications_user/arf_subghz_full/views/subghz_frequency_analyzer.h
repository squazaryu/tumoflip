#pragma once

#include <gui/view.h>
#include "../helpers/subghz_custom_event.h"
#include "../helpers/subghz_txrx.h"

typedef enum {
    SubGHzFrequencyAnalyzerFeedbackLevelAll,
    SubGHzFrequencyAnalyzerFeedbackLevelVibro,
    SubGHzFrequencyAnalyzerFeedbackLevelMute
} SubGHzFrequencyAnalyzerFeedbackLevel;

typedef struct SubGhzFrequencyAnalyzer SubGhzFrequencyAnalyzer;

typedef void (*SubGhzFrequencyAnalyzerCallback)(SubGhzCustomEvent event, void* context);

typedef enum {
    SubGhzFrequencyAnalyzerObservationSourceLive,
    SubGhzFrequencyAnalyzerObservationSourceHistory,
} SubGhzFrequencyAnalyzerObservationSource;

typedef enum {
    SubGhzFrequencyAnalyzerNotebookTagField,
    SubGhzFrequencyAnalyzerNotebookTagTest,
    SubGhzFrequencyAnalyzerNotebookTagNoise,
    SubGhzFrequencyAnalyzerNotebookTagOther,
    SubGhzFrequencyAnalyzerNotebookTagCount,
} SubGhzFrequencyAnalyzerNotebookTag;

typedef struct {
    bool valid;
    uint32_t frequency;
    int16_t rssi_dbm_x10;
    int16_t trigger_dbm_x10;
    uint8_t history_index;
    uint8_t rx_count;
    bool signal;
    bool is_ext_radio;
    SubGhzFrequencyAnalyzerObservationSource source;
    SubGhzFrequencyAnalyzerNotebookTag notebook_tag;
} SubGhzFrequencyAnalyzerObservation;

void subghz_frequency_analyzer_set_callback(
    SubGhzFrequencyAnalyzer* subghz_frequency_analyzer,
    SubGhzFrequencyAnalyzerCallback callback,
    void* context);

SubGhzFrequencyAnalyzer* subghz_frequency_analyzer_alloc(SubGhzTxRx* txrx);

void subghz_frequency_analyzer_free(SubGhzFrequencyAnalyzer* subghz_static);

View* subghz_frequency_analyzer_get_view(SubGhzFrequencyAnalyzer* subghz_static);

uint32_t subghz_frequency_analyzer_get_frequency_to_save(SubGhzFrequencyAnalyzer* instance);

bool subghz_frequency_analyzer_get_observation(
    SubGhzFrequencyAnalyzer* instance,
    SubGhzFrequencyAnalyzerObservation* observation);

SubGHzFrequencyAnalyzerFeedbackLevel subghz_frequency_analyzer_feedback_level(
    SubGhzFrequencyAnalyzer* instance,
    SubGHzFrequencyAnalyzerFeedbackLevel level,
    bool update);

float subghz_frequency_analyzer_get_trigger_level(SubGhzFrequencyAnalyzer* instance);
