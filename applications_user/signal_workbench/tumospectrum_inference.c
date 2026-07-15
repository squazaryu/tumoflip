#include "tumospectrum_inference.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define TUMOSPECTRUM_TIMING_TOLERANCE_PERCENT  20U
#define TUMOSPECTRUM_CLUSTER_TOLERANCE_PERCENT 24U
#define TUMOSPECTRUM_STATIC_THRESHOLD_PERCENT  90U
#define TUMOSPECTRUM_MIN_FRAME_POINTS          8U

typedef struct {
    size_t start;
    size_t count;
} TumoSpectrumFrame;

typedef struct {
    uint32_t center;
    uint16_t count;
} TumoSpectrumCluster;

static uint32_t tumospectrum_inference_magnitude(int32_t value) {
    return value == INT32_MIN ? UINT32_MAX : (uint32_t)abs(value);
}

static bool tumospectrum_inference_within_percent(
    uint32_t first,
    uint32_t second,
    uint32_t tolerance_percent) {
    if(first == 0U || second == 0U || first == UINT32_MAX || second == UINT32_MAX) return false;
    const uint32_t larger = first > second ? first : second;
    const uint32_t difference = first > second ? first - second : second - first;
    return (uint64_t)difference * 100U <= (uint64_t)larger * tolerance_percent;
}

static uint32_t tumospectrum_inference_gap_threshold(const TumoSpectrumCapture* capture) {
    if(capture->analysis.gap_threshold_us >= 2500U) return capture->analysis.gap_threshold_us;

    uint32_t minimum = UINT32_MAX;
    for(size_t index = 0U; index < capture->timing_count; index++) {
        const uint32_t magnitude = tumospectrum_inference_magnitude(capture->timings[index]);
        if(magnitude != 0U && magnitude != UINT32_MAX && magnitude < minimum) minimum = magnitude;
    }
    if(minimum == UINT32_MAX) return 5000U;
    const uint64_t threshold = (uint64_t)minimum * 8U;
    return threshold < 5000U ? 5000U : threshold > UINT32_MAX ? UINT32_MAX : (uint32_t)threshold;
}

static TumoSpectrumFrame
    tumospectrum_inference_reference_frame(const TumoSpectrumCapture* capture) {
    const uint32_t gap_threshold = tumospectrum_inference_gap_threshold(capture);
    TumoSpectrumFrame best = {0};
    size_t current_start = 0U;

    for(size_t index = 0U; index <= capture->timing_count; index++) {
        const bool at_end = index == capture->timing_count;
        const bool separator = !at_end && capture->timings[index] < 0 &&
                               tumospectrum_inference_magnitude(capture->timings[index]) >=
                                   gap_threshold;
        if(!at_end && !separator) continue;

        const size_t count = index - current_start;
        if(count >= TUMOSPECTRUM_MIN_FRAME_POINTS && count > best.count) {
            best.start = current_start;
            best.count = count;
        }
        current_start = index + 1U;
    }

    if(best.count == 0U && capture->timing_count >= TUMOSPECTRUM_MIN_FRAME_POINTS) {
        best.start = 0U;
        best.count = capture->timing_count;
    }
    return best;
}

static void tumospectrum_inference_add_cluster(
    TumoSpectrumCluster* clusters,
    uint8_t* cluster_count,
    uint32_t magnitude) {
    if(magnitude == 0U || magnitude == UINT32_MAX) return;

    for(uint8_t index = 0U; index < *cluster_count; index++) {
        if(tumospectrum_inference_within_percent(
               clusters[index].center, magnitude, TUMOSPECTRUM_CLUSTER_TOLERANCE_PERCENT)) {
            const uint64_t weighted =
                (uint64_t)clusters[index].center * clusters[index].count + magnitude;
            if(clusters[index].count < UINT16_MAX) clusters[index].count++;
            clusters[index].center = (uint32_t)(weighted / clusters[index].count);
            return;
        }
    }

    if(*cluster_count < TUMOSPECTRUM_MAX_CLUSTERS) {
        clusters[*cluster_count].center = magnitude;
        clusters[*cluster_count].count = 1U;
        (*cluster_count)++;
    }
}

static void
    tumospectrum_inference_sort_clusters(TumoSpectrumCluster* clusters, uint8_t cluster_count) {
    for(uint8_t outer = 0U; outer < cluster_count; outer++) {
        for(uint8_t inner = outer + 1U; inner < cluster_count; inner++) {
            if(clusters[inner].center < clusters[outer].center) {
                const TumoSpectrumCluster temporary = clusters[outer];
                clusters[outer] = clusters[inner];
                clusters[inner] = temporary;
            }
        }
    }
}

static uint8_t tumospectrum_inference_axis_cluster_count(
    const TumoSpectrumCapture* capture,
    TumoSpectrumFrame frame,
    bool positive) {
    TumoSpectrumCluster clusters[2] = {0};
    uint8_t count = 0U;
    for(size_t offset = 0U; offset < frame.count; offset++) {
        const int32_t timing = capture->timings[frame.start + offset];
        if((timing >= 0) != positive) continue;
        const uint32_t magnitude = tumospectrum_inference_magnitude(timing);
        bool matched = false;
        for(uint8_t index = 0U; index < count; index++) {
            if(tumospectrum_inference_within_percent(
                   clusters[index].center, magnitude, TUMOSPECTRUM_CLUSTER_TOLERANCE_PERCENT)) {
                matched = true;
                break;
            }
        }
        if(!matched && count < 2U) {
            clusters[count].center = magnitude;
            clusters[count].count = 1U;
            count++;
        }
    }
    return count;
}

static TumoSpectrumEncoding
    tumospectrum_inference_encoding(const TumoSpectrumCapture* capture, TumoSpectrumFrame frame) {
    if(frame.count < TUMOSPECTRUM_MIN_FRAME_POINTS) return TumoSpectrumEncodingUnknown;

    size_t opposite_pairs = 0U;
    size_t near_equal_pairs = 0U;
    for(size_t offset = 0U; offset + 1U < frame.count; offset += 2U) {
        const int32_t first = capture->timings[frame.start + offset];
        const int32_t second = capture->timings[frame.start + offset + 1U];
        if((first >= 0) == (second >= 0)) continue;
        opposite_pairs++;
        if(tumospectrum_inference_within_percent(
               tumospectrum_inference_magnitude(first),
               tumospectrum_inference_magnitude(second),
               TUMOSPECTRUM_TIMING_TOLERANCE_PERCENT)) {
            near_equal_pairs++;
        }
    }
    if(opposite_pairs * 2U < frame.count / 2U) return TumoSpectrumEncodingUnknown;

    const uint8_t positive_clusters =
        tumospectrum_inference_axis_cluster_count(capture, frame, true);
    const uint8_t negative_clusters =
        tumospectrum_inference_axis_cluster_count(capture, frame, false);
    if(near_equal_pairs * 100U >= opposite_pairs * 80U && positive_clusters <= 1U &&
       negative_clusters <= 1U) {
        return TumoSpectrumEncodingManchester;
    }
    if(positive_clusters >= 2U && negative_clusters <= 1U) return TumoSpectrumEncodingPwm;
    if(positive_clusters <= 1U && negative_clusters >= 2U) return TumoSpectrumEncodingPpm;
    if(positive_clusters >= 2U && negative_clusters >= 2U) {
        return TumoSpectrumEncodingPulsePair;
    }
    return TumoSpectrumEncodingUnknown;
}

TumoSpectrumInference
    tumospectrum_infer_captures(const TumoSpectrumCapture* captures, size_t capture_count) {
    TumoSpectrumInference result = {
        .replay_class = TumoSpectrumReplayUnsupported,
    };
    if(!captures || capture_count == 0U || capture_count > TUMOSPECTRUM_SET_MAX_SAMPLES) {
        return result;
    }

    const TumoSpectrumCapture* reference = &captures[0];
    if(reference->status != TumoSpectrumStatusOk ||
       !tumospectrum_type_has_timings(reference->type)) {
        return result;
    }

    TumoSpectrumFrame frames[TUMOSPECTRUM_SET_MAX_SAMPLES] = {0};
    size_t aligned_points = SIZE_MAX;
    uint32_t frame_count = 0U;
    for(size_t sample = 0U; sample < capture_count; sample++) {
        const TumoSpectrumCapture* capture = &captures[sample];
        if(capture->status != TumoSpectrumStatusOk || capture->type != reference->type ||
           !tumospectrum_type_has_timings(capture->type)) {
            return result;
        }
        if(reference->frequency_hz != 0U && capture->frequency_hz != 0U) {
            const uint32_t delta = reference->frequency_hz > capture->frequency_hz ?
                                       reference->frequency_hz - capture->frequency_hz :
                                       capture->frequency_hz - reference->frequency_hz;
            if(delta > 100000U) return result;
        }
        frames[sample] = tumospectrum_inference_reference_frame(capture);
        if(frames[sample].count < TUMOSPECTRUM_MIN_FRAME_POINTS) return result;
        if(frames[sample].count < aligned_points) aligned_points = frames[sample].count;
        frame_count += capture->analysis.burst_count;
    }

    result.compatible = true;
    result.sample_count = (uint8_t)capture_count;
    result.reference_points = frames[0].count > UINT16_MAX ? UINT16_MAX :
                                                             (uint16_t)frames[0].count;
    result.aligned_points = aligned_points > UINT16_MAX ? UINT16_MAX : (uint16_t)aligned_points;
    result.frame_count = frame_count > UINT16_MAX ? UINT16_MAX : (uint16_t)frame_count;
    result.frame_lengths_match = true;
    for(size_t sample = 1U; sample < capture_count; sample++) {
        if(frames[sample].count != frames[0].count) result.frame_lengths_match = false;
    }

    for(size_t offset = 0U; offset < aligned_points; offset++) {
        const int32_t expected = reference->timings[frames[0].start + offset];
        const uint32_t expected_magnitude = tumospectrum_inference_magnitude(expected);
        bool stable = true;
        for(size_t sample = 1U; sample < capture_count; sample++) {
            const int32_t actual = captures[sample].timings[frames[sample].start + offset];
            if((expected >= 0) != (actual >= 0) || !tumospectrum_inference_within_percent(
                                                       expected_magnitude,
                                                       tumospectrum_inference_magnitude(actual),
                                                       TUMOSPECTRUM_TIMING_TOLERANCE_PERCENT)) {
                stable = false;
                break;
            }
        }
        if(stable) {
            result.stable_points++;
        } else {
            result.changing_points++;
        }
    }
    result.stable_percent =
        result.aligned_points == 0U ?
            0U :
            (uint8_t)((uint32_t)result.stable_points * 100U / result.aligned_points);

    TumoSpectrumCluster clusters[TUMOSPECTRUM_MAX_CLUSTERS] = {0};
    for(size_t sample = 0U; sample < capture_count; sample++) {
        for(size_t offset = 0U; offset < frames[sample].count; offset++) {
            tumospectrum_inference_add_cluster(
                clusters,
                &result.cluster_count,
                tumospectrum_inference_magnitude(
                    captures[sample].timings[frames[sample].start + offset]));
        }
    }
    tumospectrum_inference_sort_clusters(clusters, result.cluster_count);
    for(uint8_t index = 0U; index < result.cluster_count; index++) {
        result.clusters_us[index] = clusters[index].center;
    }

    result.encoding = tumospectrum_inference_encoding(reference, frames[0]);
    if(capture_count < TUMOSPECTRUM_SET_MIN_SAMPLES) {
        result.replay_class = TumoSpectrumReplayInsufficient;
    } else if(
        result.frame_lengths_match &&
        result.stable_percent >= TUMOSPECTRUM_STATIC_THRESHOLD_PERCENT) {
        result.replay_class = TumoSpectrumReplayStaticLike;
    } else {
        result.replay_class = TumoSpectrumReplayChanging;
    }
    return result;
}

const char* tumospectrum_encoding_name(TumoSpectrumEncoding encoding) {
    switch(encoding) {
    case TumoSpectrumEncodingPulsePair:
        return "Pulse pairs";
    case TumoSpectrumEncodingPwm:
        return "PWM candidate";
    case TumoSpectrumEncodingPpm:
        return "PPM candidate";
    case TumoSpectrumEncodingManchester:
        return "Manchester-like";
    default:
        return "Unknown encoding";
    }
}

const char* tumospectrum_replay_class_name(TumoSpectrumReplayClass replay_class) {
    switch(replay_class) {
    case TumoSpectrumReplayInsufficient:
        return "Need 3 samples";
    case TumoSpectrumReplayStaticLike:
        return "Static-like";
    case TumoSpectrumReplayChanging:
        return "Changing / unknown";
    default:
        return "Unsupported";
    }
}
