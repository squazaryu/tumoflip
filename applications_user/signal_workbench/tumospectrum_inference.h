#pragma once

#include "tumospectrum_types.h"

#define TUMOSPECTRUM_SET_MIN_SAMPLES  3U
#define TUMOSPECTRUM_SET_MAX_SAMPLES  4U
#define TUMOSPECTRUM_MAX_CLUSTERS     4U
#define TUMOSPECTRUM_SET_LABEL_SIZE   32U
#define TUMOSPECTRUM_MAX_BITS         96U
#define TUMOSPECTRUM_BITSET_SIZE      ((TUMOSPECTRUM_MAX_BITS + 7U) / 8U)
#define TUMOSPECTRUM_MAX_FIELDS       12U
#define TUMOSPECTRUM_COUNTER_MAX_BITS 16U

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

typedef enum {
    TumoSpectrumFieldStable,
    TumoSpectrumFieldChanging,
    TumoSpectrumFieldUnknown,
} TumoSpectrumFieldKind;

typedef enum {
    TumoSpectrumCounterNone,
    TumoSpectrumCounterIncrementing,
    TumoSpectrumCounterDecrementing,
} TumoSpectrumCounterDirection;

typedef enum {
    TumoSpectrumChecksumXor8 = 1U << 0,
    TumoSpectrumChecksumSum8 = 1U << 1,
    TumoSpectrumChecksumCrc8Poly07 = 1U << 2,
    TumoSpectrumChecksumCrc8Poly31 = 1U << 3,
} TumoSpectrumChecksumCandidate;

typedef struct {
    TumoSpectrumFieldKind kind;
    uint8_t start;
    uint8_t length;
} TumoSpectrumField;

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
    uint8_t bit_count;
    uint8_t stable_bits;
    uint8_t changing_bits;
    uint8_t unknown_bits;
    uint8_t reference_bits[TUMOSPECTRUM_BITSET_SIZE];
    uint8_t known_mask[TUMOSPECTRUM_BITSET_SIZE];
    uint8_t stable_mask[TUMOSPECTRUM_BITSET_SIZE];
    uint8_t changing_mask[TUMOSPECTRUM_BITSET_SIZE];
    TumoSpectrumField fields[TUMOSPECTRUM_MAX_FIELDS];
    uint8_t field_count;
    TumoSpectrumCounterDirection counter_direction;
    uint8_t counter_start;
    uint8_t counter_length;
    uint8_t counter_confidence;
    uint8_t checksum_candidates;
    uint8_t checksum_start;
    uint8_t checksum_confidence;
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
const char* tumospectrum_field_kind_name(TumoSpectrumFieldKind kind);
const char* tumospectrum_counter_direction_name(TumoSpectrumCounterDirection direction);
void tumospectrum_format_checksum_candidates(uint8_t candidates, char* output, size_t output_size);
bool tumospectrum_bitset_get(const uint8_t* bitset, size_t index);
void tumospectrum_inference_rebuild_fields(TumoSpectrumInference* inference);
