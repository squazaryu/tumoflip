#pragma once

#include "tumospectrum_types.h"

#define TUMOSPECTRUM_SET_MIN_SAMPLES 3U
#define TUMOSPECTRUM_SET_MAX_SAMPLES 4U
#define TUMOSPECTRUM_MAX_CLUSTERS    4U
#define TUMOSPECTRUM_SET_LABEL_SIZE  32U

typedef enum {
    TumoSpectrumEncodingUnknown,
    TumoSpectrumEncodingPulsePair,
    TumoSpectrumEncodingPwm,
    TumoSpectrumEncodingPpm,
    TumoSpectrumEncodingManchester,
} TumoSpectrumEncoding;

typedef enum {
    TumoSpectrumReplayUnsupported,
    TumoSpectrumReplayInsufficient,
    TumoSpectrumReplayStaticLike,
    TumoSpectrumReplayChanging,
} TumoSpectrumReplayClass;

typedef struct {
    bool compatible;
    bool frame_lengths_match;
    uint8_t sample_count;
    uint8_t cluster_count;
    uint8_t stable_percent;
    uint16_t frame_count;
    uint16_t reference_points;
    uint16_t aligned_points;
    uint16_t stable_points;
    uint16_t changing_points;
    uint32_t clusters_us[TUMOSPECTRUM_MAX_CLUSTERS];
    TumoSpectrumEncoding encoding;
    TumoSpectrumReplayClass replay_class;
} TumoSpectrumInference;

typedef struct {
    char device_label[TUMOSPECTRUM_SET_LABEL_SIZE];
    char control_label[TUMOSPECTRUM_SET_LABEL_SIZE];
    TumoSpectrumCaptureType type;
    TumoSpectrumCapture samples[TUMOSPECTRUM_SET_MAX_SAMPLES];
    size_t sample_count;
    TumoSpectrumInference inference;
    bool inferred;
} TumoSpectrumCaptureSet;

TumoSpectrumInference
    tumospectrum_infer_captures(const TumoSpectrumCapture* captures, size_t capture_count);

const char* tumospectrum_encoding_name(TumoSpectrumEncoding encoding);
const char* tumospectrum_replay_class_name(TumoSpectrumReplayClass replay_class);
