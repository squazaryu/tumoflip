#include "../../applications_user/signal_workbench/tumospectrum_profile_builder.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t parity_even(uint16_t value) {
    uint8_t parity = 0U;
    for(uint8_t index = 0U; index < 11U; index++)
        parity ^= (uint8_t)((value >> index) & 1U);
    return parity;
}

static size_t encode_capture(TumoSpectrumCapture* capture, uint16_t value, int32_t jitter) {
    const int32_t short_us = 400 + jitter;
    const int32_t long_us = 1200 + jitter * 2;
    size_t count = 0U;
    memset(capture, 0, sizeof(*capture));
    capture->status = TumoSpectrumStatusOk;
    capture->type = TumoSpectrumCaptureSubGhzRaw;
    capture->frequency_hz = 433920000U;
    for(uint8_t index = 0U; index < 4U; index++) {
        capture->timings[count++] = short_us;
        capture->timings[count++] = -short_us;
    }
    capture->timings[count++] = short_us;
    capture->timings[count++] = -long_us;
    for(int8_t index = 10; index >= 0; index--) {
        const bool bit = ((value >> index) & 1U) != 0U;
        capture->timings[count++] = bit ? long_us : short_us;
        capture->timings[count++] = bit ? -short_us : -long_us;
    }
    const bool parity = parity_even(value) != 0U;
    capture->timings[count++] = parity ? long_us : short_us;
    capture->timings[count++] = parity ? -short_us : -long_us;
    capture->timings[count++] = -8000;
    capture->timing_count = count;
    return count;
}

int main(void) {
    const uint16_t values[] = {0x500U, 0x5FFU, 0x555U, 0x5AAU};
    const int32_t jitters[] = {-12, 8, -5, 14};
    TumoSpectrumCaptureSet set = {
        .type = TumoSpectrumCaptureSubGhzRaw,
        .sample_count = 4U,
        .inferred = true,
        .inference =
            {
                .compatible = true,
                .encoding = TumoSpectrumEncodingPulsePair,
            },
    };
    for(size_t index = 0U; index < 4U; index++) {
        encode_capture(&set.samples[index], values[index], jitters[index]);
    }

    ProtocolProfilePackage package = {0};
    assert(
        tumospectrum_profile_builder_build(&set, "Field Demo", 88U, &package) ==
        TumoSpectrumProfileBuildOk);
    assert(package.status == ProtocolProfileStatusOk);
    assert(package.profile.prefix_count == 8U);
    assert(package.profile.bit_count == 13U);
    assert(package.profile.frequency_hz == 433920000U);
    assert(package.profile.receive_only);
    assert(package.profile.review_required);

    TumoSpectrumCapture validation = {0};
    encode_capture(&validation, 0x53CU, 6);
    ProtocolProfileDecodeResult decoded = {0};
    assert(
        protocol_profile_decode(
            &package.profile, 88U, validation.timings, validation.timing_count, &decoded) ==
        ProtocolProfileStatusOk);
    assert(decoded.value == 0xA78U);
    assert(decoded.bit_count == 13U);

    set.inference.encoding = TumoSpectrumEncodingManchester;
    assert(
        tumospectrum_profile_builder_build(&set, "Rejected", 88U, &package) ==
        TumoSpectrumProfileBuildUnsupportedEncoding);
    puts("tumospectrum_profile_builder_host_test: PASS");
    return 0;
}
