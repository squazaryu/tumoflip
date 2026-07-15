#include "tumospectrum_inference.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TUMOSPECTRUM_TIMING_TOLERANCE_PERCENT  20U
#define TUMOSPECTRUM_CLUSTER_TOLERANCE_PERCENT 24U
#define TUMOSPECTRUM_STATIC_THRESHOLD_PERCENT  90U
#define TUMOSPECTRUM_MIN_FRAME_POINTS          8U
#define TUMOSPECTRUM_DECODE_TOLERANCE_PERCENT  35U

typedef struct {
    size_t start;
    size_t count;
} TumoSpectrumFrame;

typedef struct {
    uint32_t center;
    uint16_t count;
} TumoSpectrumCluster;

typedef struct {
    uint8_t bits[TUMOSPECTRUM_BITSET_SIZE];
    uint8_t known[TUMOSPECTRUM_BITSET_SIZE];
    uint8_t count;
} TumoSpectrumDecodedBits;

typedef struct {
    TumoSpectrumEncoding encoding;
    uint32_t positive[2];
    uint32_t negative[2];
    uint8_t positive_count;
    uint8_t negative_count;
} TumoSpectrumDecodeProfile;

bool tumospectrum_bitset_get(const uint8_t* bitset, size_t index) {
    return bitset && index < TUMOSPECTRUM_MAX_BITS &&
           (bitset[index / 8U] & (uint8_t)(1U << (7U - (index % 8U)))) != 0U;
}

static void tumospectrum_bitset_set(uint8_t* bitset, size_t index, bool value) {
    if(!bitset || index >= TUMOSPECTRUM_MAX_BITS) return;
    const uint8_t mask = (uint8_t)(1U << (7U - (index % 8U)));
    if(value) {
        bitset[index / 8U] |= mask;
    } else {
        bitset[index / 8U] &= (uint8_t)~mask;
    }
}

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

static uint8_t tumospectrum_inference_axis_clusters(
    const TumoSpectrumCapture* capture,
    TumoSpectrumFrame frame,
    bool positive,
    uint32_t centers[2]) {
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
                const uint64_t weighted =
                    (uint64_t)clusters[index].center * clusters[index].count + magnitude;
                if(clusters[index].count < UINT16_MAX) clusters[index].count++;
                clusters[index].center = (uint32_t)(weighted / clusters[index].count);
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
    if(count == 2U && clusters[1].center < clusters[0].center) {
        const TumoSpectrumCluster temporary = clusters[0];
        clusters[0] = clusters[1];
        clusters[1] = temporary;
    }
    for(uint8_t index = 0U; index < count; index++)
        centers[index] = clusters[index].center;
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

    uint32_t positive_centers[2] = {0};
    uint32_t negative_centers[2] = {0};
    const uint8_t positive_clusters =
        tumospectrum_inference_axis_clusters(capture, frame, true, positive_centers);
    const uint8_t negative_clusters =
        tumospectrum_inference_axis_clusters(capture, frame, false, negative_centers);
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

static TumoSpectrumDecodeProfile tumospectrum_inference_decode_profile(
    const TumoSpectrumCapture* capture,
    TumoSpectrumFrame frame,
    TumoSpectrumEncoding encoding) {
    TumoSpectrumDecodeProfile profile = {.encoding = encoding};
    profile.positive_count =
        tumospectrum_inference_axis_clusters(capture, frame, true, profile.positive);
    profile.negative_count =
        tumospectrum_inference_axis_clusters(capture, frame, false, profile.negative);
    return profile;
}

static bool tumospectrum_inference_classify_long(
    uint32_t magnitude,
    const uint32_t centers[2],
    uint8_t center_count,
    bool* is_long) {
    if(!is_long || center_count < 2U || centers[0] == 0U || centers[1] == 0U) return false;
    const uint32_t low_delta = magnitude > centers[0] ? magnitude - centers[0] :
                                                        centers[0] - magnitude;
    const uint32_t high_delta = magnitude > centers[1] ? magnitude - centers[1] :
                                                         centers[1] - magnitude;
    const uint32_t selected = low_delta <= high_delta ? centers[0] : centers[1];
    if(!tumospectrum_inference_within_percent(
           magnitude, selected, TUMOSPECTRUM_DECODE_TOLERANCE_PERCENT)) {
        return false;
    }
    *is_long = high_delta < low_delta;
    return true;
}

static TumoSpectrumDecodedBits tumospectrum_inference_decode_bits(
    const TumoSpectrumCapture* capture,
    TumoSpectrumFrame frame,
    const TumoSpectrumDecodeProfile* profile) {
    TumoSpectrumDecodedBits decoded = {0};
    if(!capture || !profile || profile->encoding == TumoSpectrumEncodingUnknown ||
       profile->encoding == TumoSpectrumEncodingManchester) {
        return decoded;
    }

    for(size_t offset = 0U; offset + 1U < frame.count && decoded.count < TUMOSPECTRUM_MAX_BITS;
        offset += 2U) {
        const int32_t first = capture->timings[frame.start + offset];
        const int32_t second = capture->timings[frame.start + offset + 1U];
        const uint8_t bit_index = decoded.count++;
        if((first >= 0) == (second >= 0)) continue;

        bool bit = false;
        bool known = false;
        if(profile->encoding == TumoSpectrumEncodingPwm) {
            const int32_t mark = first >= 0 ? first : second;
            known = tumospectrum_inference_classify_long(
                tumospectrum_inference_magnitude(mark),
                profile->positive,
                profile->positive_count,
                &bit);
        } else if(profile->encoding == TumoSpectrumEncodingPpm) {
            const int32_t space = first < 0 ? first : second;
            known = tumospectrum_inference_classify_long(
                tumospectrum_inference_magnitude(space),
                profile->negative,
                profile->negative_count,
                &bit);
        } else if(profile->encoding == TumoSpectrumEncodingPulsePair) {
            bool first_long = false;
            bool second_long = false;
            const uint32_t* first_centers = first >= 0 ? profile->positive : profile->negative;
            const uint32_t* second_centers = second >= 0 ? profile->positive : profile->negative;
            const uint8_t first_count = first >= 0 ? profile->positive_count :
                                                     profile->negative_count;
            const uint8_t second_count = second >= 0 ? profile->positive_count :
                                                       profile->negative_count;
            known = tumospectrum_inference_classify_long(
                        tumospectrum_inference_magnitude(first),
                        first_centers,
                        first_count,
                        &first_long) &&
                    tumospectrum_inference_classify_long(
                        tumospectrum_inference_magnitude(second),
                        second_centers,
                        second_count,
                        &second_long) &&
                    first_long != second_long;
            bit = first_long;
        }
        if(known) {
            tumospectrum_bitset_set(decoded.known, bit_index, true);
            tumospectrum_bitset_set(decoded.bits, bit_index, bit);
        }
    }
    return decoded;
}

static TumoSpectrumFieldKind
    tumospectrum_inference_field_kind(const TumoSpectrumInference* result, size_t index) {
    if(tumospectrum_bitset_get(result->stable_mask, index)) return TumoSpectrumFieldStable;
    if(tumospectrum_bitset_get(result->changing_mask, index)) return TumoSpectrumFieldChanging;
    return TumoSpectrumFieldUnknown;
}

void tumospectrum_inference_rebuild_fields(TumoSpectrumInference* result) {
    if(!result) return;
    result->field_count = 0U;
    if(result->bit_count == 0U) return;
    size_t start = 0U;
    TumoSpectrumFieldKind current = tumospectrum_inference_field_kind(result, 0U);
    for(size_t index = 1U; index <= result->bit_count; index++) {
        const bool boundary = index == result->bit_count ||
                              tumospectrum_inference_field_kind(result, index) != current;
        if(!boundary) continue;
        if(result->field_count < TUMOSPECTRUM_MAX_FIELDS) {
            TumoSpectrumField* field = &result->fields[result->field_count++];
            field->kind = current;
            field->start = (uint8_t)start;
            field->length = (uint8_t)(index - start);
        }
        start = index;
        if(index < result->bit_count) current = tumospectrum_inference_field_kind(result, index);
    }
}

static bool tumospectrum_inference_decode_value(
    const TumoSpectrumDecodedBits* decoded,
    size_t start,
    size_t length,
    uint32_t* value) {
    if(!decoded || !value || length == 0U || length > TUMOSPECTRUM_COUNTER_MAX_BITS ||
       start + length > decoded->count) {
        return false;
    }
    uint32_t result = 0U;
    for(size_t index = start; index < start + length; index++) {
        if(!tumospectrum_bitset_get(decoded->known, index)) return false;
        result = (result << 1U) | (tumospectrum_bitset_get(decoded->bits, index) ? 1U : 0U);
    }
    *value = result;
    return true;
}

static void tumospectrum_inference_find_counter(
    TumoSpectrumInference* result,
    const TumoSpectrumDecodedBits* decoded,
    size_t capture_count) {
    if(capture_count < TUMOSPECTRUM_SET_MIN_SAMPLES) return;
    size_t start = 0U;
    while(start < result->bit_count) {
        if(!tumospectrum_bitset_get(result->changing_mask, start)) {
            start++;
            continue;
        }
        size_t end = start + 1U;
        while(end < result->bit_count && tumospectrum_bitset_get(result->changing_mask, end))
            end++;
        const size_t length = end - start;
        if(length >= 2U && length <= TUMOSPECTRUM_COUNTER_MAX_BITS) {
            uint32_t values[TUMOSPECTRUM_SET_MAX_SAMPLES] = {0};
            bool valid = true;
            for(size_t sample = 0U; sample < capture_count; sample++) {
                if(!tumospectrum_inference_decode_value(
                       &decoded[sample], start, length, &values[sample])) {
                    valid = false;
                    break;
                }
            }
            if(valid) {
                const uint32_t mask = (1UL << length) - 1UL;
                bool incrementing = true;
                bool decrementing = true;
                for(size_t sample = 1U; sample < capture_count; sample++) {
                    incrementing &= values[sample] == ((values[sample - 1U] + 1U) & mask);
                    decrementing &= values[sample] == ((values[sample - 1U] - 1U) & mask);
                }
                if(incrementing || decrementing) {
                    result->counter_direction = incrementing ? TumoSpectrumCounterIncrementing :
                                                               TumoSpectrumCounterDecrementing;
                    result->counter_start = (uint8_t)start;
                    result->counter_length = (uint8_t)length;
                    result->counter_confidence = 100U;
                    return;
                }
            }
        }
        start = end;
    }
}

static uint8_t
    tumospectrum_inference_crc8(const uint8_t* data, size_t length, uint8_t polynomial) {
    uint8_t crc = 0U;
    for(size_t index = 0U; index < length; index++) {
        crc ^= data[index];
        for(uint8_t bit = 0U; bit < 8U; bit++) {
            crc = (crc & 0x80U) ? (uint8_t)((crc << 1U) ^ polynomial) : (uint8_t)(crc << 1U);
        }
    }
    return crc;
}

static void tumospectrum_inference_find_checksums(
    TumoSpectrumInference* result,
    const TumoSpectrumDecodedBits* decoded,
    size_t capture_count) {
    if(capture_count < TUMOSPECTRUM_SET_MIN_SAMPLES || result->bit_count < 16U ||
       result->bit_count % 8U != 0U) {
        return;
    }
    const size_t byte_count = result->bit_count / 8U;
    uint8_t bytes[TUMOSPECTRUM_SET_MAX_SAMPLES][TUMOSPECTRUM_BITSET_SIZE] = {0};
    for(size_t sample = 0U; sample < capture_count; sample++) {
        for(size_t byte = 0U; byte < byte_count; byte++) {
            uint32_t value = 0U;
            if(!tumospectrum_inference_decode_value(&decoded[sample], byte * 8U, 8U, &value)) {
                return;
            }
            bytes[sample][byte] = (uint8_t)value;
        }
    }

    const uint8_t candidates[] = {
        TumoSpectrumChecksumXor8,
        TumoSpectrumChecksumSum8,
        TumoSpectrumChecksumCrc8Poly07,
        TumoSpectrumChecksumCrc8Poly31,
    };
    for(size_t candidate_index = 0U; candidate_index < sizeof(candidates) / sizeof(candidates[0]);
        candidate_index++) {
        bool matches = true;
        for(size_t sample = 0U; sample < capture_count; sample++) {
            uint8_t expected = 0U;
            if(candidates[candidate_index] == TumoSpectrumChecksumXor8) {
                for(size_t byte = 0U; byte + 1U < byte_count; byte++)
                    expected ^= bytes[sample][byte];
            } else if(candidates[candidate_index] == TumoSpectrumChecksumSum8) {
                for(size_t byte = 0U; byte + 1U < byte_count; byte++) {
                    expected = (uint8_t)(expected + bytes[sample][byte]);
                }
            } else {
                const uint8_t polynomial =
                    candidates[candidate_index] == TumoSpectrumChecksumCrc8Poly07 ? 0x07U : 0x31U;
                expected = tumospectrum_inference_crc8(bytes[sample], byte_count - 1U, polynomial);
            }
            if(expected != bytes[sample][byte_count - 1U]) {
                matches = false;
                break;
            }
        }
        if(matches) result->checksum_candidates |= candidates[candidate_index];
    }
    if(result->checksum_candidates != 0U) {
        result->checksum_start = (uint8_t)(result->bit_count - 8U);
        result->checksum_confidence = 100U;
    }
}

static void tumospectrum_inference_analyze_bits(
    TumoSpectrumInference* result,
    const TumoSpectrumCapture* captures,
    const TumoSpectrumFrame* frames,
    size_t capture_count) {
    const TumoSpectrumDecodeProfile profile =
        tumospectrum_inference_decode_profile(&captures[0], frames[0], result->encoding);
    TumoSpectrumDecodedBits decoded[TUMOSPECTRUM_SET_MAX_SAMPLES] = {0};
    for(size_t sample = 0U; sample < capture_count; sample++) {
        decoded[sample] =
            tumospectrum_inference_decode_bits(&captures[sample], frames[sample], &profile);
        if(decoded[sample].count == 0U || decoded[sample].count != decoded[0].count) return;
    }

    result->bit_count = decoded[0].count;
    memcpy(result->reference_bits, decoded[0].bits, sizeof(result->reference_bits));
    for(size_t index = 0U; index < result->bit_count; index++) {
        bool all_known = true;
        bool all_equal = true;
        const bool reference_bit = tumospectrum_bitset_get(decoded[0].bits, index);
        for(size_t sample = 0U; sample < capture_count; sample++) {
            if(!tumospectrum_bitset_get(decoded[sample].known, index)) {
                all_known = false;
                break;
            }
            if(tumospectrum_bitset_get(decoded[sample].bits, index) != reference_bit) {
                all_equal = false;
            }
        }
        if(!all_known) {
            result->unknown_bits++;
        } else if(all_equal) {
            result->stable_bits++;
            tumospectrum_bitset_set(result->known_mask, index, true);
            tumospectrum_bitset_set(result->stable_mask, index, true);
        } else {
            result->changing_bits++;
            tumospectrum_bitset_set(result->known_mask, index, true);
            tumospectrum_bitset_set(result->changing_mask, index, true);
        }
    }
    tumospectrum_inference_rebuild_fields(result);
    tumospectrum_inference_find_counter(result, decoded, capture_count);
    tumospectrum_inference_find_checksums(result, decoded, capture_count);
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
    tumospectrum_inference_analyze_bits(&result, captures, frames, capture_count);
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

const char* tumospectrum_field_kind_name(TumoSpectrumFieldKind kind) {
    switch(kind) {
    case TumoSpectrumFieldStable:
        return "stable";
    case TumoSpectrumFieldChanging:
        return "changing";
    default:
        return "unknown";
    }
}

const char* tumospectrum_counter_direction_name(TumoSpectrumCounterDirection direction) {
    switch(direction) {
    case TumoSpectrumCounterIncrementing:
        return "incrementing";
    case TumoSpectrumCounterDecrementing:
        return "decrementing";
    default:
        return "none";
    }
}

void tumospectrum_format_checksum_candidates(uint8_t candidates, char* output, size_t output_size) {
    if(!output || output_size == 0U) return;
    output[0] = '\0';
    const struct {
        uint8_t flag;
        const char* name;
    } names[] = {
        {TumoSpectrumChecksumXor8, "XOR-8"},
        {TumoSpectrumChecksumSum8, "SUM-8"},
        {TumoSpectrumChecksumCrc8Poly07, "CRC-8/07"},
        {TumoSpectrumChecksumCrc8Poly31, "CRC-8/31"},
    };
    for(size_t index = 0U; index < sizeof(names) / sizeof(names[0]); index++) {
        if((candidates & names[index].flag) == 0U) continue;
        const size_t used = strlen(output);
        if(used >= output_size - 1U) break;
        snprintf(
            output + used, output_size - used, "%s%s", used == 0U ? "" : ", ", names[index].name);
    }
    if(output[0] == '\0') snprintf(output, output_size, "none");
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
