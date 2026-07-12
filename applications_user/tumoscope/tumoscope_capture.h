#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TUMOSCOPE_CAPTURE_MAX_SAMPLES 8192U

typedef enum {
    TumoScopeTriggerAuto,
    TumoScopeTriggerPc0Rising,
    TumoScopeTriggerPc0Falling,
    TumoScopeTriggerPc1Rising,
    TumoScopeTriggerPc1Falling,
    TumoScopeTriggerPc3Rising,
    TumoScopeTriggerPc3Falling,
    TumoScopeTriggerPc0High,
    TumoScopeTriggerPc0Low,
    TumoScopeTriggerPc1High,
    TumoScopeTriggerPc1Low,
    TumoScopeTriggerPc3High,
    TumoScopeTriggerPc3Low,
} TumoScopeTrigger;

typedef struct {
    uint32_t sample_rate;
    size_t sample_count;
    TumoScopeTrigger trigger;
    uint8_t pretrigger_percent;
} TumoScopeCaptureConfig;

typedef struct TumoScopeCapture TumoScopeCapture;

TumoScopeCapture* tumoscope_capture_alloc(void);
void tumoscope_capture_free(TumoScopeCapture* capture);

bool tumoscope_capture_start(TumoScopeCapture* capture, const TumoScopeCaptureConfig* config);
void tumoscope_capture_stop(TumoScopeCapture* capture);

bool tumoscope_capture_is_running(const TumoScopeCapture* capture);
bool tumoscope_capture_is_triggered(const TumoScopeCapture* capture);
bool tumoscope_capture_is_complete(const TumoScopeCapture* capture);
bool tumoscope_capture_has_error(const TumoScopeCapture* capture);
size_t tumoscope_capture_progress(const TumoScopeCapture* capture);
size_t tumoscope_capture_count(const TumoScopeCapture* capture);
size_t tumoscope_capture_trigger_index(const TumoScopeCapture* capture);

bool tumoscope_capture_copy(const TumoScopeCapture* capture, uint8_t* output, size_t capacity);
