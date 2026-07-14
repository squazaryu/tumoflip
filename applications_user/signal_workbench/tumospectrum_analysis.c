#include "tumospectrum_analysis.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static uint32_t tumospectrum_magnitude(int32_t value) {
    return value == INT32_MIN ? UINT32_MAX : (uint32_t)abs(value);
}

static uint8_t tumospectrum_clamp_percent(uint64_t value) {
    return value > 100U ? 100U : (uint8_t)value;
}

const char* tumospectrum_status_name(TumoSpectrumStatus status) {
    switch(status) {
    case TumoSpectrumStatusOk:
        return "Ready";
    case TumoSpectrumStatusNoData:
        return "No signal data";
    case TumoSpectrumStatusUnsupported:
        return "Unsupported format";
    case TumoSpectrumStatusMalformed:
        return "Malformed file";
    case TumoSpectrumStatusTooLarge:
        return "File too large";
    case TumoSpectrumStatusIoError:
        return "Cannot read file";
    case TumoSpectrumStatusOutOfMemory:
        return "Not enough memory";
    default:
        return "Unknown error";
    }
}

const char* tumospectrum_type_name(TumoSpectrumCaptureType type) {
    switch(type) {
    case TumoSpectrumCaptureSubGhzRaw:
        return "SUB RAW";
    case TumoSpectrumCaptureSubGhzDecoded:
        return "SUB DECODED";
    case TumoSpectrumCaptureInfraredRaw:
        return "IR RAW";
    case TumoSpectrumCaptureInfraredParsed:
        return "IR DECODED";
    case TumoSpectrumCaptureTumoScope:
        return "GPIO CAPTURE";
    case TumoSpectrumCaptureFrequencyObservation:
        return "RF NOTE";
    default:
        return "NO CAPTURE";
    }
}

bool tumospectrum_type_has_timings(TumoSpectrumCaptureType type) {
    return type == TumoSpectrumCaptureSubGhzRaw || type == TumoSpectrumCaptureInfraredRaw ||
           type == TumoSpectrumCaptureTumoScope;
}

bool tumospectrum_type_can_handoff(TumoSpectrumCaptureType type) {
    return type == TumoSpectrumCaptureSubGhzRaw || type == TumoSpectrumCaptureSubGhzDecoded ||
           type == TumoSpectrumCaptureInfraredRaw || type == TumoSpectrumCaptureInfraredParsed;
}

static uint8_t tumospectrum_repeat_score(const TumoSpectrumCapture* capture) {
    if(capture->timing_count < 12U) return 0U;

    const size_t half = capture->timing_count / 2U;
    const size_t pairs = half < 64U ? half : 64U;
    uint32_t matches = 0U;
    for(size_t index = 0U; index < pairs; index++) {
        const uint32_t first = tumospectrum_magnitude(capture->timings[index]);
        const uint32_t second = tumospectrum_magnitude(capture->timings[index + half]);
        const uint32_t larger = first > second ? first : second;
        const uint32_t difference = first > second ? first - second : second - first;
        if(larger != 0U && ((uint64_t)difference * 100U / larger) <= 18U) matches++;
    }
    return pairs == 0U ? 0U : tumospectrum_clamp_percent((uint64_t)matches * 100U / pairs);
}

void tumospectrum_analyze(TumoSpectrumCapture* capture) {
    memset(&capture->analysis, 0, sizeof(capture->analysis));
    if(!tumospectrum_type_has_timings(capture->type) || capture->timing_count == 0U) return;

    TumoSpectrumAnalysis* analysis = &capture->analysis;
    uint64_t total = 0U;
    for(size_t index = 0U; index < capture->timing_count; index++) {
        const int32_t timing = capture->timings[index];
        const uint32_t magnitude = tumospectrum_magnitude(timing);
        if(magnitude == 0U || magnitude == UINT32_MAX) continue;
        if(analysis->count == 0U || magnitude < analysis->minimum_us) {
            analysis->minimum_us = magnitude;
        }
        if(magnitude > analysis->maximum_us) analysis->maximum_us = magnitude;
        total += magnitude;
        if(timing >= 0) {
            analysis->positive_count++;
        } else {
            analysis->negative_count++;
        }
        analysis->count++;
    }
    if(analysis->count == 0U) return;

    analysis->duration_us = total;
    analysis->average_us = (uint32_t)(total / analysis->count);
    analysis->gap_threshold_us = analysis->minimum_us * 8U;
    if(analysis->gap_threshold_us < 5000U) analysis->gap_threshold_us = 5000U;
    analysis->burst_count = 1U;

    analysis->histogram_max_us = analysis->maximum_us;
    const uint32_t width = (analysis->histogram_max_us + TUMOSPECTRUM_HISTOGRAM_BUCKETS - 1U) /
                           TUMOSPECTRUM_HISTOGRAM_BUCKETS;
    uint32_t histogram_counts[TUMOSPECTRUM_HISTOGRAM_BUCKETS] = {0};
    uint32_t histogram_peak = 0U;
    for(size_t index = 0U; index < capture->timing_count; index++) {
        const int32_t timing = capture->timings[index];
        const uint32_t magnitude = tumospectrum_magnitude(timing);
        if(magnitude == 0U || magnitude == UINT32_MAX) continue;
        size_t bucket = width == 0U ? 0U : (magnitude - 1U) / width;
        if(bucket >= TUMOSPECTRUM_HISTOGRAM_BUCKETS) bucket = TUMOSPECTRUM_HISTOGRAM_BUCKETS - 1U;
        histogram_counts[bucket]++;
        if(histogram_counts[bucket] > histogram_peak) histogram_peak = histogram_counts[bucket];
        if(timing < 0 && magnitude >= analysis->gap_threshold_us && analysis->burst_count < UINT16_MAX) {
            analysis->burst_count++;
        }
    }
    for(size_t index = 0U; index < TUMOSPECTRUM_HISTOGRAM_BUCKETS; index++) {
        analysis->histogram[index] = histogram_peak == 0U ? 0U :
                                                               (uint8_t)((histogram_counts[index] * 100U) /
                                                                         histogram_peak);
    }
    analysis->repeat_score = tumospectrum_repeat_score(capture);

    uint8_t populated = 0U;
    for(size_t index = 0U; index < TUMOSPECTRUM_HISTOGRAM_BUCKETS; index++) {
        if(analysis->histogram[index] >= 15U) populated++;
    }
    if(analysis->repeat_score >= 70U) {
        strlcpy(analysis->candidate, "Repeated frame", sizeof(analysis->candidate));
    } else if(populated <= 3U) {
        strlcpy(analysis->candidate, "Pulse pairs", sizeof(analysis->candidate));
    } else {
        strlcpy(analysis->candidate, "Variable timing", sizeof(analysis->candidate));
    }
}

static bool tumospectrum_types_compatible(
    TumoSpectrumCaptureType first,
    TumoSpectrumCaptureType second) {
    return (first == second) && tumospectrum_type_has_timings(first);
}

TumoSpectrumComparison tumospectrum_compare(
    const TumoSpectrumCapture* first,
    const TumoSpectrumCapture* second) {
    TumoSpectrumComparison comparison = {0};
    comparison.compatible = first && second && first->status == TumoSpectrumStatusOk &&
                            second->status == TumoSpectrumStatusOk &&
                            tumospectrum_types_compatible(first->type, second->type);
    if(!comparison.compatible) return comparison;

    const int64_t frequency_delta = (int64_t)second->frequency_hz - first->frequency_hz;
    comparison.frequency_delta_hz = frequency_delta > INT32_MAX ? INT32_MAX :
                                    frequency_delta < INT32_MIN ? INT32_MIN :
                                                                  (int32_t)frequency_delta;
    const int64_t pulse_delta = (int64_t)second->analysis.count - first->analysis.count;
    comparison.pulse_delta = pulse_delta > INT32_MAX ? INT32_MAX :
                             pulse_delta < INT32_MIN ? INT32_MIN :
                                                       (int32_t)pulse_delta;
    if(first->analysis.duration_us > 0U) {
        const int64_t duration_delta =
            (int64_t)second->analysis.duration_us - (int64_t)first->analysis.duration_us;
        comparison.duration_delta_percent =
            (int32_t)(duration_delta * 100 / (int64_t)first->analysis.duration_us);
    }

    uint32_t histogram_difference = 0U;
    for(size_t index = 0U; index < TUMOSPECTRUM_HISTOGRAM_BUCKETS; index++) {
        const int difference = (int)first->analysis.histogram[index] -
                               (int)second->analysis.histogram[index];
        histogram_difference += (uint32_t)abs(difference);
    }
    const uint32_t average_difference = histogram_difference / TUMOSPECTRUM_HISTOGRAM_BUCKETS;
    comparison.histogram_similarity =
        average_difference >= 100U ? 0U : (uint8_t)(100U - average_difference);

    uint32_t frequency_score = 100U;
    if(first->frequency_hz != 0U && second->frequency_hz != 0U) {
        const uint32_t delta = comparison.frequency_delta_hz < 0 ?
                                   (uint32_t)(-comparison.frequency_delta_hz) :
                                   (uint32_t)comparison.frequency_delta_hz;
        const uint64_t ppm = (uint64_t)delta * 1000000U / first->frequency_hz;
        frequency_score = ppm >= 10000U ? 0U : 100U - (uint32_t)(ppm / 100U);
    }
    const uint32_t duration_difference = comparison.duration_delta_percent < 0 ?
                                             (uint32_t)(-comparison.duration_delta_percent) :
                                             (uint32_t)comparison.duration_delta_percent;
    const uint32_t duration_score = duration_difference >= 100U ? 0U : 100U - duration_difference;
    comparison.overall_similarity = (uint8_t)((comparison.histogram_similarity * 2U +
                                               frequency_score + duration_score) /
                                              4U);
    comparison.likely_same = comparison.overall_similarity >= 75U;
    return comparison;
}
