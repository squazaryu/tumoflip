#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TUMOSPECTRUM_APP_VERSION       "1.0.0"
#define TUMOSPECTRUM_PATH_SIZE         192U
#define TUMOSPECTRUM_LABEL_SIZE        48U
#define TUMOSPECTRUM_NOTE_SIZE         96U
#define TUMOSPECTRUM_MAX_TIMINGS       512U
#define TUMOSPECTRUM_HISTOGRAM_BUCKETS 8U

typedef enum {
    TumoSpectrumStatusOk,
    TumoSpectrumStatusNoData,
    TumoSpectrumStatusUnsupported,
    TumoSpectrumStatusMalformed,
    TumoSpectrumStatusTooLarge,
    TumoSpectrumStatusIoError,
    TumoSpectrumStatusOutOfMemory,
} TumoSpectrumStatus;

typedef enum {
    TumoSpectrumCaptureNone,
    TumoSpectrumCaptureSubGhzRaw,
    TumoSpectrumCaptureSubGhzDecoded,
    TumoSpectrumCaptureInfraredRaw,
    TumoSpectrumCaptureInfraredParsed,
    TumoSpectrumCaptureTumoScope,
    TumoSpectrumCaptureFrequencyObservation,
} TumoSpectrumCaptureType;

typedef struct {
    uint32_t count;
    uint32_t positive_count;
    uint32_t negative_count;
    uint32_t minimum_us;
    uint32_t maximum_us;
    uint32_t average_us;
    uint64_t duration_us;
    uint32_t gap_threshold_us;
    uint16_t burst_count;
    uint8_t repeat_score;
    uint8_t histogram[TUMOSPECTRUM_HISTOGRAM_BUCKETS];
    uint32_t histogram_max_us;
    char candidate[TUMOSPECTRUM_LABEL_SIZE];
} TumoSpectrumAnalysis;

typedef struct {
    TumoSpectrumStatus status;
    TumoSpectrumCaptureType type;
    char path[TUMOSPECTRUM_PATH_SIZE];
    char name[TUMOSPECTRUM_LABEL_SIZE];
    char protocol[TUMOSPECTRUM_LABEL_SIZE];
    char preset[TUMOSPECTRUM_LABEL_SIZE];
    char detail[TUMOSPECTRUM_LABEL_SIZE];
    char note[TUMOSPECTRUM_NOTE_SIZE];
    uint64_t file_size;
    uint32_t frequency_hz;
    uint32_t version;
    uint32_t signal_count;
    uint32_t decoded_count;
    float duty_cycle;
    bool truncated;
    int32_t timings[TUMOSPECTRUM_MAX_TIMINGS];
    size_t timing_count;
    TumoSpectrumAnalysis analysis;
} TumoSpectrumCapture;

typedef struct {
    bool compatible;
    bool likely_same;
    int32_t frequency_delta_hz;
    int32_t pulse_delta;
    int32_t duration_delta_percent;
    uint8_t histogram_similarity;
    uint8_t overall_similarity;
} TumoSpectrumComparison;

const char* tumospectrum_status_name(TumoSpectrumStatus status);
const char* tumospectrum_type_name(TumoSpectrumCaptureType type);
bool tumospectrum_type_has_timings(TumoSpectrumCaptureType type);
bool tumospectrum_type_can_handoff(TumoSpectrumCaptureType type);
