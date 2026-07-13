#include "../../applications_user/protocol_compiler/protocol_profile_core.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static ProtocolProfile demo_profile(void) {
    const ProtocolProfile profile = {
        .version = ProtocolProfileVersion,
        .minimum_api = 88U,
        .frequency_hz = 433920000U,
        .tolerance_percent = 15U,
        .prefix_count = 8U,
        .prefix = {402, -402, 402, -402, 402, -402, 402, -402},
        .bit_count = 13U,
        .zero_high_us = 395U,
        .zero_low_us = 1190U,
        .one_high_us = 1216U,
        .one_low_us = 408U,
        .stable_mask = UINT64_C(0x1E00),
        .stable_value = UINT64_C(0x0A00),
        .variable_mask = UINT64_C(0x01FF),
        .uncertain_mask = 0U,
        .checksum = ProtocolProfileChecksumParityEvenLast,
        .confidence = 81U,
        .receive_only = true,
        .review_required = true,
    };
    return profile;
}

static uint8_t parity_even(uint16_t value) {
    uint8_t parity = 0U;
    for(uint8_t index = 0U; index < 11U; index++) {
        parity ^= (uint8_t)((value >> index) & 1U);
    }
    return parity;
}

static size_t encode_capture(uint16_t value, bool corrupt_checksum, int32_t* pulses) {
    size_t count = 0U;
    pulses[count++] = 120;
    pulses[count++] = -8000;
    for(uint8_t index = 0U; index < 4U; index++) {
        pulses[count++] = 406;
        pulses[count++] = -406;
    }
    pulses[count++] = 406;
    pulses[count++] = -1212;
    for(uint8_t index = 0U; index < 11U; index++) {
        const uint8_t shift = (uint8_t)(10U - index);
        const bool bit = ((value >> shift) & 1U) != 0U;
        pulses[count++] = bit ? 1212 : 406;
        pulses[count++] = bit ? -406 : -1212;
    }
    const bool parity = parity_even(value) != 0U;
    const bool checksum_bit = corrupt_checksum ? !parity : parity;
    pulses[count++] = checksum_bit ? 1212 : 406;
    pulses[count++] = checksum_bit ? -406 : -1212;
    pulses[count++] = -8000;
    return count;
}

int main(void) {
    ProtocolProfile profile = demo_profile();
    assert(protocol_profile_validate(&profile, 88U) == ProtocolProfileStatusOk);
    assert(protocol_profile_validate(&profile, 87U) == ProtocolProfileStatusApiMismatch);
    assert(protocol_profile_validate(&profile, 89U) == ProtocolProfileStatusApiMismatch);

    int32_t pulses[64] = {0};
    size_t count = encode_capture(0x53CU, false, pulses);
    ProtocolProfileDecodeResult result;
    assert(
        protocol_profile_decode(&profile, 88U, pulses, count, &result) ==
        ProtocolProfileStatusOk);
    assert(result.start_offset == 2U);
    assert(result.stable_match);
    assert(result.checksum_match);
    assert(result.value == ((uint64_t)0x53CU << 1U | parity_even(0x53CU)));

    count = encode_capture(0x13CU, false, pulses);
    assert(
        protocol_profile_decode(&profile, 88U, pulses, count, &result) ==
        ProtocolProfileStatusStableMismatch);

    count = encode_capture(0x53CU, true, pulses);
    assert(
        protocol_profile_decode(&profile, 88U, pulses, count, &result) ==
        ProtocolProfileStatusChecksumMismatch);

    ProtocolProfile unsafe = profile;
    unsafe.receive_only = false;
    assert(
        protocol_profile_validate(&unsafe, 88U) == ProtocolProfileStatusNotReceiveOnly);
    unsafe = profile;
    unsafe.variable_mask |= unsafe.stable_mask;
    assert(
        protocol_profile_validate(&unsafe, 88U) == ProtocolProfileStatusUnsafeProfile);
    unsafe = profile;
    unsafe.prefix[0] = 0;
    assert(
        protocol_profile_validate(&unsafe, 88U) == ProtocolProfileStatusUnsafeProfile);
    unsafe = profile;
    unsafe.stable_mask |= UINT64_C(1);
    unsafe.stable_value &= ~UINT64_C(1);
    unsafe.variable_mask &= ~UINT64_C(1);
    assert(
        protocol_profile_validate(&unsafe, 88U) == ProtocolProfileStatusUnsafeProfile);
    unsafe = profile;
    unsafe.frequency_hz = 1000000001U;
    assert(
        protocol_profile_validate(&unsafe, 88U) == ProtocolProfileStatusUnsafeProfile);

    assert(
        protocol_profile_decode(
            &profile,
            88U,
            pulses,
            ProtocolProfileMaximumCapturePulses + 1U,
            &result) == ProtocolProfileStatusInvalidArgument);
    assert(strcmp(protocol_profile_checksum_name(profile.checksum), "Even parity") == 0);
    assert(strcmp(protocol_profile_status_name(ProtocolProfileStatusNoMatch), "No match") == 0);

    puts("protocol_profile_core_host_test: PASS");
    return 0;
}
