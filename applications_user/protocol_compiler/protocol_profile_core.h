#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    ProtocolProfileVersion = 1,
    ProtocolProfileMaximumPrefix = 24,
    ProtocolProfileMaximumBits = 64,
    ProtocolProfileMaximumCapturePulses = 512,
};

typedef enum {
    ProtocolProfileChecksumNone = 0,
    ProtocolProfileChecksumParityEvenLast,
    ProtocolProfileChecksumParityOddLast,
    ProtocolProfileChecksumXor8Last,
    ProtocolProfileChecksumSum8Last,
} ProtocolProfileChecksum;

typedef enum {
    ProtocolProfileStatusOk = 0,
    ProtocolProfileStatusInvalidArgument,
    ProtocolProfileStatusUnsupportedVersion,
    ProtocolProfileStatusApiMismatch,
    ProtocolProfileStatusUnsafeProfile,
    ProtocolProfileStatusUnsupportedCapture,
    ProtocolProfileStatusNotReceiveOnly,
    ProtocolProfileStatusNoMatch,
    ProtocolProfileStatusStableMismatch,
    ProtocolProfileStatusChecksumMismatch,
} ProtocolProfileStatus;

typedef struct {
    uint32_t version;
    uint32_t minimum_api;
    uint32_t frequency_hz;
    uint8_t tolerance_percent;
    uint8_t prefix_count;
    int32_t prefix[ProtocolProfileMaximumPrefix];
    uint8_t bit_count;
    uint32_t zero_high_us;
    uint32_t zero_low_us;
    uint32_t one_high_us;
    uint32_t one_low_us;
    uint64_t stable_mask;
    uint64_t stable_value;
    uint64_t variable_mask;
    uint64_t uncertain_mask;
    ProtocolProfileChecksum checksum;
    uint8_t confidence;
    bool inverted;
    bool receive_only;
    bool review_required;
} ProtocolProfile;

typedef struct {
    uint64_t value;
    uint8_t bit_count;
    uint16_t start_offset;
    bool stable_match;
    bool checksum_match;
} ProtocolProfileDecodeResult;

ProtocolProfileStatus
    protocol_profile_validate(const ProtocolProfile* profile, uint32_t current_api);

ProtocolProfileStatus protocol_profile_decode(
    const ProtocolProfile* profile,
    uint32_t current_api,
    const int32_t* pulses,
    size_t pulse_count,
    ProtocolProfileDecodeResult* result);

const char* protocol_profile_status_name(ProtocolProfileStatus status);
const char* protocol_profile_checksum_name(ProtocolProfileChecksum checksum);
