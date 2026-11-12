#pragma once
#include "../views/subghz_frequency_analyzer.h"

#define SUBGHZ_ANALYZER_PLUGIN_APP_ID "TumoflipAnalyzer"

typedef struct {
    SubGhzFrequencyAnalyzer* (*alloc)(SubGhzTxRx*);
    void (*free)(SubGhzFrequencyAnalyzer*);
    View* (*get_view)(SubGhzFrequencyAnalyzer*);
    void (*set_callback)(SubGhzFrequencyAnalyzer*, SubGhzFrequencyAnalyzerCallback, void*);
    uint32_t (*get_frequency_to_save)(SubGhzFrequencyAnalyzer*);
    bool (*get_observation)(SubGhzFrequencyAnalyzer*, SubGhzFrequencyAnalyzerObservation*);
    bool (*get_selected_preset)(SubGhzFrequencyAnalyzer*, uint32_t*, uint32_t*);
    SubGHzFrequencyAnalyzerFeedbackLevel (*feedback_level)(
        SubGhzFrequencyAnalyzer*, SubGHzFrequencyAnalyzerFeedbackLevel, bool);
    float (*get_trigger_level)(SubGhzFrequencyAnalyzer*);
} SubGhzAnalyzerPlugin;
