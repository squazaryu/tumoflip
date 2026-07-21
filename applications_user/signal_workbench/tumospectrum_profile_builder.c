#include "tumospectrum_profile_builder.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#define TUMOSPECTRUM_BUILDER_MIN_FRAME_POINTS           8U
#define TUMOSPECTRUM_BUILDER_CLUSTER_SEPARATION_PERCENT 45U
#define TUMOSPECTRUM_BUILDER_CLASS_TOLERANCE_PERCENT    35U

typedef struct {
    size_t start;
    size_t count;
} TumoSpectrumBuilderFrame;

typedef struct {
    uint32_t center[2];
    uint8_t count;
} TumoSpectrumBuilderAxis;

typedef struct {
    uint8_t offset;
    uint8_t bit_count;
    uint8_t zero_signature;
    uint8_t one_signature;
    bool inverted;
    TumoSpectrumBuilderAxis high;
    TumoSpectrumBuilderAxis low;
} TumoSpectrumBuilderCandidate;

static uint32_t tumospectrum_builder_magnitude(int32_t value) {
    if(value == INT32_MIN) return UINT32_MAX;
    return value < 0 ? (uint32_t)(-value) : (uint32_t)value;
}

static uint32_t tumospectrum_builder_gap_threshold(const TumoSpectrumCapture* capture) {
    uint32_t minimum = UINT32_MAX;
    for(size_t index = 0U; index < capture->timing_count; index++) {
        const uint32_t value = tumospectrum_builder_magnitude(capture->timings[index]);
        if(value > 0U && value < minimum) minimum = value;
    }
    if(minimum == UINT32_MAX) return 5000U;
    const uint64_t scaled = (uint64_t)minimum * 8U;
    return scaled < 5000U ? 5000U : scaled > UINT32_MAX ? UINT32_MAX : (uint32_t)scaled;
}

static TumoSpectrumBuilderFrame
    tumospectrum_builder_reference_frame(const TumoSpectrumCapture* capture) {
    const uint32_t threshold = tumospectrum_builder_gap_threshold(capture);
    TumoSpectrumBuilderFrame best = {0};
    size_t start = 0U;
    for(size_t index = 0U; index <= capture->timing_count; index++) {
        const bool end = index == capture->timing_count;
        const bool separator = !end && capture->timings[index] < 0 &&
                               tumospectrum_builder_magnitude(capture->timings[index]) >=
                                   threshold;
        if(!end && !separator) continue;
        const size_t count = index - start;
        if(count >= TUMOSPECTRUM_BUILDER_MIN_FRAME_POINTS && count > best.count) {
            best.start = start;
            best.count = count;
        }
        start = index + 1U;
    }
    if(best.count == 0U && capture->timing_count >= TUMOSPECTRUM_BUILDER_MIN_FRAME_POINTS) {
        best.count = capture->timing_count;
    }
    return best;
}

static bool tumospectrum_builder_name_valid(const char* name) {
    if(name == NULL) return false;
    const size_t length = strlen(name);
    if(length == 0U || length >= ProtocolProfileNameSize) return false;
    for(size_t index = 0U; index < length; index++) {
        const unsigned char value = (unsigned char)name[index];
        if(value < 0x20U || value > 0x7EU || value == '"') return false;
    }
    return true;
}

static bool tumospectrum_builder_axis_values(
    const TumoSpectrumCaptureSet* set,
    const TumoSpectrumBuilderFrame* frames,
    size_t offset,
    bool high_axis,
    uint32_t* minimum,
    uint32_t* maximum,
    uint64_t* sum,
    uint32_t* count) {
    *minimum = UINT32_MAX;
    *maximum = 0U;
    *sum = 0U;
    *count = 0U;
    for(size_t sample = 0U; sample < set->sample_count; sample++) {
        const TumoSpectrumCapture* capture = &set->samples[sample];
        for(size_t cursor = offset; cursor + 1U < frames[sample].count; cursor += 2U) {
            const int32_t first = capture->timings[frames[sample].start + cursor];
            const int32_t second = capture->timings[frames[sample].start + cursor + 1U];
            if(first == 0 || second == 0 || (first > 0) == (second > 0)) return false;
            const int32_t selected = high_axis ? (first > 0 ? first : second) :
                                                 (first < 0 ? first : second);
            const uint32_t value = tumospectrum_builder_magnitude(selected);
            if(value < 40U || value > 1000000U) return false;
            if(value < *minimum) *minimum = value;
            if(value > *maximum) *maximum = value;
            *sum += value;
            (*count)++;
        }
    }
    return *count > 0U;
}

static bool tumospectrum_builder_axis_model(
    const TumoSpectrumCaptureSet* set,
    const TumoSpectrumBuilderFrame* frames,
    size_t offset,
    bool high_axis,
    TumoSpectrumBuilderAxis* model) {
    uint32_t minimum = 0U;
    uint32_t maximum = 0U;
    uint64_t total = 0U;
    uint32_t total_count = 0U;
    if(!tumospectrum_builder_axis_values(
           set, frames, offset, high_axis, &minimum, &maximum, &total, &total_count)) {
        return false;
    }
    memset(model, 0, sizeof(*model));
    if((uint64_t)maximum * 100U <
       (uint64_t)minimum * (100U + TUMOSPECTRUM_BUILDER_CLUSTER_SEPARATION_PERCENT)) {
        model->center[0] = (uint32_t)(total / total_count);
        model->count = 1U;
        return true;
    }

    uint32_t short_center = minimum;
    uint32_t long_center = maximum;
    for(uint8_t iteration = 0U; iteration < 12U; iteration++) {
        uint64_t sums[2] = {0U, 0U};
        uint32_t counts[2] = {0U, 0U};
        for(size_t sample = 0U; sample < set->sample_count; sample++) {
            const TumoSpectrumCapture* capture = &set->samples[sample];
            for(size_t cursor = offset; cursor + 1U < frames[sample].count; cursor += 2U) {
                const int32_t first = capture->timings[frames[sample].start + cursor];
                const int32_t second = capture->timings[frames[sample].start + cursor + 1U];
                const int32_t selected = high_axis ? (first > 0 ? first : second) :
                                                     (first < 0 ? first : second);
                const uint32_t value = tumospectrum_builder_magnitude(selected);
                const uint32_t short_delta = value > short_center ? value - short_center :
                                                                    short_center - value;
                const uint32_t long_delta = value > long_center ? value - long_center :
                                                                  long_center - value;
                const uint8_t cluster = short_delta <= long_delta ? 0U : 1U;
                sums[cluster] += value;
                counts[cluster]++;
            }
        }
        if(counts[0] == 0U || counts[1] == 0U) return false;
        const uint32_t updated_short = (uint32_t)(sums[0] / counts[0]);
        const uint32_t updated_long = (uint32_t)(sums[1] / counts[1]);
        if(updated_short == short_center && updated_long == long_center) break;
        short_center = updated_short;
        long_center = updated_long;
    }
    if(long_center < short_center) {
        const uint32_t temporary = short_center;
        short_center = long_center;
        long_center = temporary;
    }
    if((uint64_t)long_center * 100U <
       (uint64_t)short_center * (100U + TUMOSPECTRUM_BUILDER_CLUSTER_SEPARATION_PERCENT)) {
        return false;
    }
    model->center[0] = short_center;
    model->center[1] = long_center;
    model->count = 2U;
    return true;
}

static bool tumospectrum_builder_classify(
    uint32_t value,
    const TumoSpectrumBuilderAxis* model,
    uint8_t* value_class) {
    if(model->count == 0U || value_class == NULL) return false;
    uint8_t selected = 0U;
    if(model->count == 2U) {
        const uint32_t short_delta = value > model->center[0] ? value - model->center[0] :
                                                                model->center[0] - value;
        const uint32_t long_delta = value > model->center[1] ? value - model->center[1] :
                                                               model->center[1] - value;
        selected = long_delta < short_delta ? 1U : 0U;
    }
    const uint32_t center = model->center[selected];
    const uint32_t delta = value > center ? value - center : center - value;
    if((uint64_t)delta * 100U > (uint64_t)center * TUMOSPECTRUM_BUILDER_CLASS_TOLERANCE_PERCENT) {
        return false;
    }
    *value_class = selected;
    return true;
}

static uint8_t tumospectrum_builder_popcount4(uint8_t value) {
    value = (uint8_t)(value - ((value >> 1U) & 0x55U));
    value = (uint8_t)((value & 0x33U) + ((value >> 2U) & 0x33U));
    return (uint8_t)((value + (value >> 4U)) & 0x0FU);
}

static bool tumospectrum_builder_candidate(
    const TumoSpectrumCaptureSet* set,
    const TumoSpectrumBuilderFrame* frames,
    uint8_t offset,
    TumoSpectrumBuilderCandidate* candidate) {
    const size_t remaining = frames[0].count - offset;
    if((remaining & 1U) != 0U) return false;
    const size_t bit_count = remaining / 2U;
    if(bit_count < 4U || bit_count > ProtocolProfileMaximumBits) return false;

    bool polarity_set = false;
    bool first_positive = false;
    for(size_t sample = 0U; sample < set->sample_count; sample++) {
        const TumoSpectrumCapture* capture = &set->samples[sample];
        for(size_t cursor = offset; cursor + 1U < frames[sample].count; cursor += 2U) {
            const int32_t first = capture->timings[frames[sample].start + cursor];
            const int32_t second = capture->timings[frames[sample].start + cursor + 1U];
            if(first == 0 || second == 0 || (first > 0) == (second > 0)) return false;
            if(!polarity_set) {
                first_positive = first > 0;
                polarity_set = true;
            } else if((first > 0) != first_positive) {
                return false;
            }
        }
    }

    TumoSpectrumBuilderAxis high = {0};
    TumoSpectrumBuilderAxis low = {0};
    if(!tumospectrum_builder_axis_model(set, frames, offset, true, &high) ||
       !tumospectrum_builder_axis_model(set, frames, offset, false, &low) ||
       (high.count == 1U && low.count == 1U)) {
        return false;
    }

    uint8_t signatures = 0U;
    for(size_t sample = 0U; sample < set->sample_count; sample++) {
        const TumoSpectrumCapture* capture = &set->samples[sample];
        for(size_t cursor = offset; cursor + 1U < frames[sample].count; cursor += 2U) {
            const int32_t first = capture->timings[frames[sample].start + cursor];
            const int32_t second = capture->timings[frames[sample].start + cursor + 1U];
            const uint32_t high_value = tumospectrum_builder_magnitude(first > 0 ? first : second);
            const uint32_t low_value = tumospectrum_builder_magnitude(first < 0 ? first : second);
            uint8_t high_class = 0U;
            uint8_t low_class = 0U;
            if(!tumospectrum_builder_classify(high_value, &high, &high_class) ||
               !tumospectrum_builder_classify(low_value, &low, &low_class)) {
                return false;
            }
            signatures |= (uint8_t)(1U << ((high_class << 1U) | low_class));
        }
    }
    if(tumospectrum_builder_popcount4(signatures) != 2U) return false;

    uint8_t first_signature = 0U;
    while((signatures & (uint8_t)(1U << first_signature)) == 0U)
        first_signature++;
    uint8_t second_signature = (uint8_t)(first_signature + 1U);
    while((signatures & (uint8_t)(1U << second_signature)) == 0U)
        second_signature++;
    *candidate = (TumoSpectrumBuilderCandidate){
        .offset = offset,
        .bit_count = (uint8_t)bit_count,
        .zero_signature = first_signature,
        .one_signature = second_signature,
        .inverted = !first_positive,
        .high = high,
        .low = low,
    };
    return true;
}

static bool tumospectrum_builder_decode_pair(
    int32_t first,
    int32_t second,
    const TumoSpectrumBuilderCandidate* candidate,
    uint8_t* bit,
    uint32_t* high_value,
    uint32_t* low_value) {
    *high_value = tumospectrum_builder_magnitude(first > 0 ? first : second);
    *low_value = tumospectrum_builder_magnitude(first < 0 ? first : second);
    uint8_t high_class = 0U;
    uint8_t low_class = 0U;
    if(!tumospectrum_builder_classify(*high_value, &candidate->high, &high_class) ||
       !tumospectrum_builder_classify(*low_value, &candidate->low, &low_class)) {
        return false;
    }
    const uint8_t signature = (uint8_t)((high_class << 1U) | low_class);
    if(signature == candidate->zero_signature) {
        *bit = 0U;
    } else if(signature == candidate->one_signature) {
        *bit = 1U;
    } else {
        return false;
    }
    return true;
}

static uint8_t tumospectrum_builder_popcount64(uint64_t value) {
    uint8_t count = 0U;
    while(value != 0U) {
        count += (uint8_t)(value & 1U);
        value >>= 1U;
    }
    return count;
}

static bool tumospectrum_builder_checksum_matches(
    ProtocolProfileChecksum checksum,
    uint64_t value,
    uint8_t bit_count) {
    if(checksum == ProtocolProfileChecksumParityEvenLast ||
       checksum == ProtocolProfileChecksumParityOddLast) {
        const uint8_t parity = tumospectrum_builder_popcount64(value >> 1U) & 1U;
        const uint8_t expected = checksum == ProtocolProfileChecksumParityEvenLast ? parity :
                                                                                     parity ^ 1U;
        return (uint8_t)(value & 1U) == expected;
    }
    if(bit_count < 16U || (bit_count % 8U) != 0U) return false;
    const uint8_t byte_count = bit_count / 8U;
    uint8_t expected = 0U;
    for(uint8_t index = 0U; index + 1U < byte_count; index++) {
        const uint8_t shift = (uint8_t)((byte_count - index - 1U) * 8U);
        const uint8_t byte = (uint8_t)(value >> shift);
        if(checksum == ProtocolProfileChecksumXor8Last) {
            expected ^= byte;
        } else if(checksum == ProtocolProfileChecksumSum8Last) {
            expected = (uint8_t)(expected + byte);
        } else {
            return false;
        }
    }
    return (uint8_t)value == expected;
}

static ProtocolProfileChecksum tumospectrum_builder_checksum(
    const uint64_t* values,
    size_t count,
    uint8_t bit_count,
    bool* ambiguous) {
    size_t distinct = 0U;
    for(size_t outer = 0U; outer < count; outer++) {
        bool seen = false;
        for(size_t inner = 0U; inner < outer; inner++)
            seen |= values[inner] == values[outer];
        if(!seen) distinct++;
    }
    if(distinct < 3U) return ProtocolProfileChecksumNone;
    const ProtocolProfileChecksum candidates[] = {
        ProtocolProfileChecksumParityEvenLast,
        ProtocolProfileChecksumParityOddLast,
        ProtocolProfileChecksumXor8Last,
        ProtocolProfileChecksumSum8Last,
    };
    ProtocolProfileChecksum selected = ProtocolProfileChecksumNone;
    uint8_t matches = 0U;
    for(size_t candidate = 0U; candidate < sizeof(candidates) / sizeof(candidates[0]);
        candidate++) {
        bool valid = true;
        for(size_t sample = 0U; sample < count; sample++) {
            valid &= tumospectrum_builder_checksum_matches(
                candidates[candidate], values[sample], bit_count);
        }
        if(valid) {
            selected = candidates[candidate];
            matches++;
        }
    }
    *ambiguous = matches > 1U;
    return matches == 1U ? selected : ProtocolProfileChecksumNone;
}

static uint32_t tumospectrum_builder_error_percent(uint32_t actual, uint32_t expected) {
    const uint32_t delta = actual > expected ? actual - expected : expected - actual;
    return expected == 0U ? UINT32_MAX : (uint32_t)(((uint64_t)delta * 100U) / expected);
}

static void tumospectrum_builder_hash_bytes(uint64_t* hash, const void* data, size_t size) {
    const uint8_t* bytes = data;
    for(size_t index = 0U; index < size; index++) {
        *hash ^= bytes[index];
        *hash *= UINT64_C(1099511628211);
    }
}

static void tumospectrum_builder_profile_id(
    const char* name,
    const ProtocolProfile* profile,
    char output[ProtocolProfileIdSize]) {
    uint64_t hash = UINT64_C(14695981039346656037);
    tumospectrum_builder_hash_bytes(&hash, name, strlen(name));
    tumospectrum_builder_hash_bytes(&hash, &profile->frequency_hz, sizeof(profile->frequency_hz));
    tumospectrum_builder_hash_bytes(&hash, &profile->prefix_count, sizeof(profile->prefix_count));
    tumospectrum_builder_hash_bytes(
        &hash, profile->prefix, sizeof(profile->prefix[0]) * profile->prefix_count);
    tumospectrum_builder_hash_bytes(&hash, &profile->bit_count, sizeof(profile->bit_count));
    tumospectrum_builder_hash_bytes(&hash, &profile->zero_high_us, sizeof(profile->zero_high_us));
    tumospectrum_builder_hash_bytes(&hash, &profile->zero_low_us, sizeof(profile->zero_low_us));
    tumospectrum_builder_hash_bytes(&hash, &profile->one_high_us, sizeof(profile->one_high_us));
    tumospectrum_builder_hash_bytes(&hash, &profile->one_low_us, sizeof(profile->one_low_us));
    tumospectrum_builder_hash_bytes(&hash, &profile->stable_mask, sizeof(profile->stable_mask));
    tumospectrum_builder_hash_bytes(&hash, &profile->stable_value, sizeof(profile->stable_value));
    snprintf(output, ProtocolProfileIdSize, "%016llx", (unsigned long long)hash);
}

static void tumospectrum_builder_ambiguity_append(
    char* ambiguity,
    size_t ambiguity_size,
    const char* value) {
    const size_t used = strlen(ambiguity);
    if(used >= ambiguity_size - 1U) return;
    snprintf(ambiguity + used, ambiguity_size - used, "%s%s", used == 0U ? "" : ",", value);
}

TumoSpectrumProfileBuildStatus tumospectrum_profile_builder_build(
    const TumoSpectrumCaptureSet* set,
    const char* name,
    uint32_t current_api,
    ProtocolProfilePackage* package) {
    if(set == NULL || package == NULL || current_api == 0U) {
        return TumoSpectrumProfileBuildInvalidArgument;
    }
    memset(package, 0, sizeof(*package));
    if(!tumospectrum_builder_name_valid(name)) return TumoSpectrumProfileBuildNameInvalid;
    if(set->type != TumoSpectrumCaptureSubGhzRaw) {
        return TumoSpectrumProfileBuildSubGhzRawRequired;
    }
    if(set->sample_count < TUMOSPECTRUM_SET_MIN_SAMPLES ||
       set->sample_count > TUMOSPECTRUM_SET_MAX_SAMPLES) {
        return TumoSpectrumProfileBuildNeedMoreSamples;
    }
    if(!set->inferred || !set->inference.compatible) {
        return TumoSpectrumProfileBuildInferenceRequired;
    }
    if(set->inference.encoding != TumoSpectrumEncodingPulsePair &&
       set->inference.encoding != TumoSpectrumEncodingPwm &&
       set->inference.encoding != TumoSpectrumEncodingPpm) {
        return TumoSpectrumProfileBuildUnsupportedEncoding;
    }

    TumoSpectrumBuilderFrame frames[TUMOSPECTRUM_SET_MAX_SAMPLES] = {0};
    uint32_t frequency = 0U;
    for(size_t sample = 0U; sample < set->sample_count; sample++) {
        const TumoSpectrumCapture* capture = &set->samples[sample];
        if(capture->status != TumoSpectrumStatusOk ||
           capture->type != TumoSpectrumCaptureSubGhzRaw) {
            return TumoSpectrumProfileBuildSubGhzRawRequired;
        }
        if(capture->frequency_hz == 0U) return TumoSpectrumProfileBuildFrequencyMissing;
        if(sample == 0U) {
            frequency = capture->frequency_hz;
        } else if(capture->frequency_hz != frequency) {
            return TumoSpectrumProfileBuildFrequencyMismatch;
        }
        frames[sample] = tumospectrum_builder_reference_frame(capture);
        if(frames[sample].count < TUMOSPECTRUM_BUILDER_MIN_FRAME_POINTS) {
            return TumoSpectrumProfileBuildFrameMismatch;
        }
        if(sample > 0U && frames[sample].count != frames[0].count) {
            return TumoSpectrumProfileBuildFrameMismatch;
        }
    }

    TumoSpectrumBuilderCandidate candidate = {0};
    bool found = false;
    bool saw_too_many_bits = false;
    const size_t maximum_offset = frames[0].count > ProtocolProfileMaximumPrefix ?
                                      ProtocolProfileMaximumPrefix :
                                      frames[0].count;
    for(size_t offset = 0U; offset <= maximum_offset; offset++) {
        const size_t remaining = frames[0].count - offset;
        if((remaining & 1U) == 0U && remaining / 2U > ProtocolProfileMaximumBits) {
            saw_too_many_bits = true;
            continue;
        }
        if(tumospectrum_builder_candidate(set, frames, (uint8_t)offset, &candidate)) {
            found = true;
            break;
        }
    }
    if(!found) {
        return saw_too_many_bits ? TumoSpectrumProfileBuildTooManyBits :
                                   TumoSpectrumProfileBuildAmbiguousSymbols;
    }

    uint64_t values[TUMOSPECTRUM_SET_MAX_SAMPLES] = {0};
    uint64_t timing_sums[2][2] = {{0U, 0U}, {0U, 0U}};
    uint32_t timing_counts[2] = {0U, 0U};
    for(size_t sample = 0U; sample < set->sample_count; sample++) {
        const TumoSpectrumCapture* capture = &set->samples[sample];
        uint64_t value = 0U;
        for(size_t cursor = candidate.offset; cursor + 1U < frames[sample].count; cursor += 2U) {
            const int32_t first = capture->timings[frames[sample].start + cursor];
            const int32_t second = capture->timings[frames[sample].start + cursor + 1U];
            uint8_t bit = 0U;
            uint32_t high = 0U;
            uint32_t low = 0U;
            if(!tumospectrum_builder_decode_pair(first, second, &candidate, &bit, &high, &low)) {
                return TumoSpectrumProfileBuildAmbiguousSymbols;
            }
            value = (value << 1U) | bit;
            timing_sums[bit][0] += high;
            timing_sums[bit][1] += low;
            timing_counts[bit]++;
        }
        values[sample] = value;
    }
    if(timing_counts[0] == 0U || timing_counts[1] == 0U) {
        return TumoSpectrumProfileBuildAmbiguousSymbols;
    }

    ProtocolProfile profile = {
        .version = ProtocolProfileVersion,
        .minimum_api = current_api,
        .frequency_hz = frequency,
        .prefix_count = candidate.offset,
        .bit_count = candidate.bit_count,
        .zero_high_us = (uint32_t)(timing_sums[0][0] / timing_counts[0]),
        .zero_low_us = (uint32_t)(timing_sums[0][1] / timing_counts[0]),
        .one_high_us = (uint32_t)(timing_sums[1][0] / timing_counts[1]),
        .one_low_us = (uint32_t)(timing_sums[1][1] / timing_counts[1]),
        .inverted = candidate.inverted,
        .receive_only = true,
        .review_required = true,
    };
    for(uint8_t index = 0U; index < profile.prefix_count; index++) {
        int64_t sum = 0;
        bool positive = false;
        for(size_t sample = 0U; sample < set->sample_count; sample++) {
            const int32_t value = set->samples[sample].timings[frames[sample].start + index];
            if(sample == 0U) positive = value > 0;
            if(value == 0 || (value > 0) != positive) {
                return TumoSpectrumProfileBuildAmbiguousSymbols;
            }
            sum += value;
        }
        profile.prefix[index] = (int32_t)(sum / (int64_t)set->sample_count);
    }

    uint32_t maximum_error = 0U;
    for(size_t sample = 0U; sample < set->sample_count; sample++) {
        const TumoSpectrumCapture* capture = &set->samples[sample];
        size_t bit_index = 0U;
        for(size_t cursor = candidate.offset; cursor + 1U < frames[sample].count; cursor += 2U) {
            const int32_t first = capture->timings[frames[sample].start + cursor];
            const int32_t second = capture->timings[frames[sample].start + cursor + 1U];
            uint8_t bit =
                (uint8_t)((values[sample] >> (candidate.bit_count - bit_index - 1U)) & 1U);
            const uint32_t high = tumospectrum_builder_magnitude(first > 0 ? first : second);
            const uint32_t low = tumospectrum_builder_magnitude(first < 0 ? first : second);
            const uint32_t high_expected = bit == 0U ? profile.zero_high_us : profile.one_high_us;
            const uint32_t low_expected = bit == 0U ? profile.zero_low_us : profile.one_low_us;
            const uint32_t high_error = tumospectrum_builder_error_percent(high, high_expected);
            const uint32_t low_error = tumospectrum_builder_error_percent(low, low_expected);
            if(high_error > maximum_error) maximum_error = high_error;
            if(low_error > maximum_error) maximum_error = low_error;
            bit_index++;
        }
    }
    const uint32_t tolerance = maximum_error + 8U;
    profile.tolerance_percent = tolerance < 15U ? 15U : tolerance > 35U ? 35U : (uint8_t)tolerance;

    const uint64_t width_mask =
        candidate.bit_count == 64U ? UINT64_MAX : (UINT64_C(1) << candidate.bit_count) - 1U;
    profile.stable_mask = width_mask;
    profile.stable_value = values[0];
    for(size_t sample = 1U; sample < set->sample_count; sample++) {
        profile.stable_mask &= ~(profile.stable_value ^ values[sample]) & width_mask;
    }
    profile.stable_value &= profile.stable_mask;
    profile.variable_mask = width_mask ^ profile.stable_mask;

    bool checksum_ambiguous = false;
    profile.checksum = tumospectrum_builder_checksum(
        values, set->sample_count, candidate.bit_count, &checksum_ambiguous);
    uint64_t derived_mask = 0U;
    if(profile.checksum == ProtocolProfileChecksumParityEvenLast ||
       profile.checksum == ProtocolProfileChecksumParityOddLast) {
        derived_mask = 1U;
    } else if(
        profile.checksum == ProtocolProfileChecksumXor8Last ||
        profile.checksum == ProtocolProfileChecksumSum8Last) {
        derived_mask = 0xFFU;
    }
    profile.stable_mask &= ~derived_mask & width_mask;
    profile.stable_value &= profile.stable_mask;
    profile.variable_mask |= derived_mask;

    package->ambiguity[0] = '\0';
    if(profile.checksum == ProtocolProfileChecksumNone) {
        tumospectrum_builder_ambiguity_append(
            package->ambiguity,
            sizeof(package->ambiguity),
            checksum_ambiguous ? "checksum-ambiguous" : "checksum-unknown");
    }
    if(profile.variable_mask == 0U) {
        tumospectrum_builder_ambiguity_append(
            package->ambiguity, sizeof(package->ambiguity), "no-changing-fields");
    }
    if(package->ambiguity[0] == '\0') {
        snprintf(package->ambiguity, sizeof(package->ambiguity), "none");
    }

    uint32_t separation = 0U;
    const TumoSpectrumBuilderAxis* axes[] = {&candidate.high, &candidate.low};
    for(size_t index = 0U; index < 2U; index++) {
        if(axes[index]->count != 2U) continue;
        const uint32_t value =
            (axes[index]->center[1] - axes[index]->center[0]) * 20U / axes[index]->center[1];
        if(value > separation) separation = value;
    }
    if(separation > 20U) separation = 20U;
    int32_t confidence = 50 + (int32_t)separation + (int32_t)set->sample_count * 2;
    if(profile.checksum != ProtocolProfileChecksumNone) confidence += 6;
    if(strcmp(package->ambiguity, "none") != 0) confidence -= 10;
    profile.confidence = (uint8_t)(confidence < 1 ? 1 : confidence > 99 ? 99 : confidence);

    package->profile = profile;
    package->status = protocol_profile_validate(&package->profile, current_api);
    if(package->status != ProtocolProfileStatusOk) {
        return TumoSpectrumProfileBuildUnsafeProfile;
    }
    snprintf(package->name, sizeof(package->name), "%s", name);
    package->training_captures = (uint8_t)set->sample_count;
    package->training_frames = (uint8_t)set->sample_count;
    tumospectrum_builder_profile_id(name, &package->profile, package->profile_id);
    return TumoSpectrumProfileBuildOk;
}

const char* tumospectrum_profile_builder_status_name(TumoSpectrumProfileBuildStatus status) {
    switch(status) {
    case TumoSpectrumProfileBuildOk:
        return "Profile ready";
    case TumoSpectrumProfileBuildNameInvalid:
        return "Use 1-31 ASCII characters for the profile name.";
    case TumoSpectrumProfileBuildSubGhzRawRequired:
        return "Profile Builder accepts Sub-GHz RAW captures only.";
    case TumoSpectrumProfileBuildNeedMoreSamples:
        return "Select three or four distinct RAW captures.";
    case TumoSpectrumProfileBuildInferenceRequired:
        return "Run capture-set inference before building a profile.";
    case TumoSpectrumProfileBuildUnsupportedEncoding:
        return "Only pulse-pair, PWM and PPM candidates are supported.";
    case TumoSpectrumProfileBuildFrequencyMissing:
        return "A training capture has no frequency.";
    case TumoSpectrumProfileBuildFrequencyMismatch:
        return "Training captures use different frequencies.";
    case TumoSpectrumProfileBuildFrameMismatch:
        return "Training frames are not aligned. Record the same control again.";
    case TumoSpectrumProfileBuildTooManyBits:
        return "The inferred payload exceeds the 64-bit profile limit.";
    case TumoSpectrumProfileBuildAmbiguousSymbols:
        return "No unambiguous two-symbol timing model fits every capture.";
    case TumoSpectrumProfileBuildUnsafeProfile:
        return "The generated profile failed the receive-only safety gate.";
    default:
        return "Profile Builder received invalid input.";
    }
}
