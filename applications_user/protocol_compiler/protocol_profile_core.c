#include "protocol_profile_core.h"

#include <limits.h>
#include <string.h>

#define PROTOCOL_PROFILE_MIN_DURATION_US 40U
#define PROTOCOL_PROFILE_MAX_DURATION_US 1000000U

static uint32_t protocol_profile_abs_i32(int32_t value) {
    if(value == INT32_MIN) return UINT32_MAX;
    return value < 0 ? (uint32_t)(-value) : (uint32_t)value;
}

static uint64_t protocol_profile_width_mask(uint8_t bit_count) {
    return bit_count == 64U ? UINT64_MAX : ((UINT64_C(1) << bit_count) - UINT64_C(1));
}

static bool protocol_profile_duration_valid(uint32_t duration) {
    return duration >= PROTOCOL_PROFILE_MIN_DURATION_US &&
           duration <= PROTOCOL_PROFILE_MAX_DURATION_US;
}

static uint32_t protocol_profile_timing_error(int32_t actual, int32_t expected) {
    if(actual == 0 || expected == 0 || ((actual > 0) != (expected > 0))) return UINT32_MAX;
    const uint32_t actual_abs = protocol_profile_abs_i32(actual);
    const uint32_t expected_abs = protocol_profile_abs_i32(expected);
    if(!protocol_profile_duration_valid(actual_abs) ||
       !protocol_profile_duration_valid(expected_abs)) {
        return UINT32_MAX;
    }
    const uint32_t delta = actual_abs > expected_abs ? actual_abs - expected_abs :
                                                       expected_abs - actual_abs;
    return (uint32_t)(((uint64_t)delta * 100U) / expected_abs);
}

static uint8_t protocol_profile_popcount(uint64_t value) {
    uint8_t count = 0U;
    while(value != 0U) {
        count += (uint8_t)(value & 1U);
        value >>= 1U;
    }
    return count;
}

static bool protocol_profile_checksum_matches(
    ProtocolProfileChecksum checksum,
    uint64_t value,
    uint8_t bit_count) {
    if(checksum == ProtocolProfileChecksumNone) return true;
    if(checksum == ProtocolProfileChecksumParityEvenLast ||
       checksum == ProtocolProfileChecksumParityOddLast) {
        if(bit_count < 2U) return false;
        const uint8_t last = (uint8_t)(value & 1U);
        const uint8_t parity = protocol_profile_popcount(value >> 1U) & 1U;
        const uint8_t expected = checksum == ProtocolProfileChecksumParityEvenLast ? parity :
                                                                                     parity ^ 1U;
        return last == expected;
    }
    if(bit_count < 16U || (bit_count % 8U) != 0U) return false;
    const uint8_t byte_count = bit_count / 8U;
    uint8_t expected = 0U;
    for(uint8_t index = 0U; index + 1U < byte_count; index++) {
        const uint8_t shift = (uint8_t)((byte_count - index - 1U) * 8U);
        const uint8_t octet = (uint8_t)(value >> shift);
        if(checksum == ProtocolProfileChecksumXor8Last) {
            expected ^= octet;
        } else if(checksum == ProtocolProfileChecksumSum8Last) {
            expected = (uint8_t)(expected + octet);
        } else {
            return false;
        }
    }
    return (uint8_t)value == expected;
}

ProtocolProfileStatus
    protocol_profile_validate(const ProtocolProfile* profile, uint32_t current_api) {
    if(profile == NULL) return ProtocolProfileStatusInvalidArgument;
    if(profile->version != ProtocolProfileVersion) {
        return ProtocolProfileStatusUnsupportedVersion;
    }
    if(profile->minimum_api != current_api) return ProtocolProfileStatusApiMismatch;
    if(!profile->receive_only || !profile->review_required) {
        return ProtocolProfileStatusNotReceiveOnly;
    }
    if(profile->prefix_count > ProtocolProfileMaximumPrefix || profile->bit_count < 4U ||
       profile->bit_count > ProtocolProfileMaximumBits || profile->tolerance_percent < 5U ||
       profile->tolerance_percent > 50U || profile->confidence > 100U ||
       profile->frequency_hz > 1000000000U ||
       !protocol_profile_duration_valid(profile->zero_high_us) ||
       !protocol_profile_duration_valid(profile->zero_low_us) ||
       !protocol_profile_duration_valid(profile->one_high_us) ||
       !protocol_profile_duration_valid(profile->one_low_us)) {
        return ProtocolProfileStatusUnsafeProfile;
    }
    for(uint8_t index = 0U; index < profile->prefix_count; index++) {
        if(profile->prefix[index] == 0 ||
           !protocol_profile_duration_valid(protocol_profile_abs_i32(profile->prefix[index]))) {
            return ProtocolProfileStatusUnsafeProfile;
        }
    }

    const uint64_t width_mask = protocol_profile_width_mask(profile->bit_count);
    if((profile->stable_mask & ~width_mask) != 0U ||
       (profile->stable_value & ~profile->stable_mask) != 0U ||
       (profile->variable_mask & ~width_mask) != 0U ||
       (profile->uncertain_mask & ~width_mask) != 0U ||
       (profile->stable_mask & profile->variable_mask) != 0U ||
       (profile->stable_mask & profile->uncertain_mask) != 0U ||
       (profile->variable_mask & profile->uncertain_mask) != 0U ||
       (profile->stable_mask | profile->variable_mask | profile->uncertain_mask) != width_mask) {
        return ProtocolProfileStatusUnsafeProfile;
    }
    if(profile->checksum > ProtocolProfileChecksumSum8Last) {
        return ProtocolProfileStatusUnsafeProfile;
    }
    if((profile->checksum == ProtocolProfileChecksumParityEvenLast ||
        profile->checksum == ProtocolProfileChecksumParityOddLast) &&
       (profile->variable_mask & UINT64_C(1)) == 0U) {
        return ProtocolProfileStatusUnsafeProfile;
    }
    if((profile->checksum == ProtocolProfileChecksumXor8Last ||
        profile->checksum == ProtocolProfileChecksumSum8Last) &&
       (profile->bit_count < 16U || (profile->bit_count % 8U) != 0U ||
        (profile->variable_mask & UINT64_C(0xFF)) != UINT64_C(0xFF))) {
        return ProtocolProfileStatusUnsafeProfile;
    }
    return ProtocolProfileStatusOk;
}

static ProtocolProfileStatus protocol_profile_decode_at(
    const ProtocolProfile* profile,
    const int32_t* pulses,
    size_t pulse_count,
    size_t start_offset,
    ProtocolProfileDecodeResult* result) {
    size_t cursor = start_offset;
    for(uint8_t index = 0U; index < profile->prefix_count; index++) {
        if(cursor >= pulse_count ||
           protocol_profile_timing_error(pulses[cursor], profile->prefix[index]) >
               profile->tolerance_percent) {
            return ProtocolProfileStatusNoMatch;
        }
        cursor++;
    }

    uint64_t value = 0U;
    for(uint8_t index = 0U; index < profile->bit_count; index++) {
        if(cursor + 1U >= pulse_count) return ProtocolProfileStatusNoMatch;
        const int32_t first = pulses[cursor++];
        const int32_t second = pulses[cursor++];
        const int32_t high = profile->inverted ? second : first;
        const int32_t low = profile->inverted ? first : second;
        const uint32_t zero_high_error =
            protocol_profile_timing_error(high, (int32_t)profile->zero_high_us);
        const uint32_t zero_low_error =
            protocol_profile_timing_error(low, -(int32_t)profile->zero_low_us);
        const uint32_t one_high_error =
            protocol_profile_timing_error(high, (int32_t)profile->one_high_us);
        const uint32_t one_low_error =
            protocol_profile_timing_error(low, -(int32_t)profile->one_low_us);
        const uint32_t zero_error = zero_high_error > zero_low_error ? zero_high_error :
                                                                       zero_low_error;
        const uint32_t one_error = one_high_error > one_low_error ? one_high_error : one_low_error;
        const uint32_t best_error = zero_error < one_error ? zero_error : one_error;
        if(best_error > profile->tolerance_percent || zero_error == one_error) {
            return ProtocolProfileStatusNoMatch;
        }
        value = (value << 1U) | (zero_error < one_error ? 0U : 1U);
    }

    result->value = value;
    result->bit_count = profile->bit_count;
    result->start_offset = (uint16_t)start_offset;
    result->stable_match = (value & profile->stable_mask) == profile->stable_value;
    result->checksum_match =
        protocol_profile_checksum_matches(profile->checksum, value, profile->bit_count);
    if(!result->stable_match) return ProtocolProfileStatusStableMismatch;
    if(!result->checksum_match) return ProtocolProfileStatusChecksumMismatch;
    return ProtocolProfileStatusOk;
}

ProtocolProfileStatus protocol_profile_decode(
    const ProtocolProfile* profile,
    uint32_t current_api,
    const int32_t* pulses,
    size_t pulse_count,
    ProtocolProfileDecodeResult* result) {
    if(pulses == NULL || result == NULL || pulse_count == 0U ||
       pulse_count > ProtocolProfileMaximumCapturePulses) {
        return ProtocolProfileStatusInvalidArgument;
    }
    const ProtocolProfileStatus validation = protocol_profile_validate(profile, current_api);
    if(validation != ProtocolProfileStatusOk) return validation;
    memset(result, 0, sizeof(*result));
    const size_t required = profile->prefix_count + (size_t)profile->bit_count * 2U;
    if(required > pulse_count) return ProtocolProfileStatusNoMatch;

    ProtocolProfileStatus best_failure = ProtocolProfileStatusNoMatch;
    for(size_t offset = 0U; offset + required <= pulse_count; offset++) {
        const ProtocolProfileStatus status =
            protocol_profile_decode_at(profile, pulses, pulse_count, offset, result);
        if(status == ProtocolProfileStatusOk) return status;
        if(status == ProtocolProfileStatusChecksumMismatch ||
           (status == ProtocolProfileStatusStableMismatch &&
            best_failure == ProtocolProfileStatusNoMatch)) {
            best_failure = status;
        }
    }
    return best_failure;
}

const char* protocol_profile_status_name(ProtocolProfileStatus status) {
    switch(status) {
    case ProtocolProfileStatusOk:
        return "OK";
    case ProtocolProfileStatusInvalidArgument:
        return "Invalid input";
    case ProtocolProfileStatusUnsupportedVersion:
        return "Unsupported profile";
    case ProtocolProfileStatusApiMismatch:
        return "API mismatch";
    case ProtocolProfileStatusUnsafeProfile:
        return "Unsafe profile";
    case ProtocolProfileStatusNotReceiveOnly:
        return "RX-only gate failed";
    case ProtocolProfileStatusNoMatch:
        return "No match";
    case ProtocolProfileStatusStableMismatch:
        return "Stable field mismatch";
    case ProtocolProfileStatusChecksumMismatch:
        return "Checksum mismatch";
    default:
        return "Unknown";
    }
}

const char* protocol_profile_checksum_name(ProtocolProfileChecksum checksum) {
    switch(checksum) {
    case ProtocolProfileChecksumNone:
        return "Unknown";
    case ProtocolProfileChecksumParityEvenLast:
        return "Even parity";
    case ProtocolProfileChecksumParityOddLast:
        return "Odd parity";
    case ProtocolProfileChecksumXor8Last:
        return "XOR-8";
    case ProtocolProfileChecksumSum8Last:
        return "SUM-8";
    default:
        return "Invalid";
    }
}
