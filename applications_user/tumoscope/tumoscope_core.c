#include "tumoscope_core.h"

#include <string.h>

bool tumoscope_sample_level(uint8_t sample, uint8_t channel) {
    if(channel >= TUMOSCOPE_CHANNEL_COUNT) return false;
    return (sample & (1U << channel)) != 0U;
}

static void tumoscope_decode_reset(TumoScopeDecodeResult* result, TumoScopeDecoder decoder) {
    memset(result, 0, sizeof(*result));
    result->decoder = decoder;
}

void tumoscope_decode_uart(
    const uint8_t* samples,
    size_t sample_count,
    uint32_t sample_rate,
    uint32_t baud,
    uint8_t channel,
    TumoScopeDecodeResult* result) {
    tumoscope_decode_reset(result, TumoScopeDecoderUart);
    if(!samples || !sample_count || !baud || channel >= TUMOSCOPE_CHANNEL_COUNT) return;

    const uint32_t samples_per_bit = sample_rate / baud;
    if(samples_per_bit < 3U) {
        result->error_count = 1U;
        return;
    }

    size_t index = 1U;
    while(index < sample_count) {
        const bool previous = tumoscope_sample_level(samples[index - 1U], channel);
        const bool current = tumoscope_sample_level(samples[index], channel);
        if(!previous || current) {
            index++;
            continue;
        }

        const size_t stop_center = index + (samples_per_bit * 19U) / 2U;
        if(stop_center >= sample_count) break;
        const size_t start_center = index + samples_per_bit / 2U;
        if(tumoscope_sample_level(samples[start_center], channel)) {
            index++;
            continue;
        }

        uint8_t value = 0U;
        for(uint8_t bit = 0U; bit < 8U; bit++) {
            const size_t sample_index =
                index + (samples_per_bit * 3U) / 2U + (size_t)bit * samples_per_bit;
            if(tumoscope_sample_level(samples[sample_index], channel)) value |= 1U << bit;
        }

        if(!tumoscope_sample_level(samples[stop_center], channel)) {
            result->error_count++;
        } else {
            if(result->byte_count < TUMOSCOPE_MAX_DECODE_BYTES) {
                result->bytes[result->byte_count++] = value;
            }
            result->frame_count++;
        }
        index += (size_t)samples_per_bit * 10U;
    }
}

void tumoscope_decode_i2c(
    const uint8_t* samples,
    size_t sample_count,
    uint8_t sda_channel,
    uint8_t scl_channel,
    TumoScopeDecodeResult* result) {
    tumoscope_decode_reset(result, TumoScopeDecoderI2c);
    if(!samples || sample_count < 2U || sda_channel >= TUMOSCOPE_CHANNEL_COUNT ||
       scl_channel >= TUMOSCOPE_CHANNEL_COUNT || sda_channel == scl_channel) {
        return;
    }

    bool active = false;
    uint8_t bit_count = 0U;
    uint8_t value = 0U;
    for(size_t index = 1U; index < sample_count; index++) {
        const bool previous_sda = tumoscope_sample_level(samples[index - 1U], sda_channel);
        const bool current_sda = tumoscope_sample_level(samples[index], sda_channel);
        const bool previous_scl = tumoscope_sample_level(samples[index - 1U], scl_channel);
        const bool current_scl = tumoscope_sample_level(samples[index], scl_channel);

        if(previous_sda && !current_sda && current_scl) {
            active = true;
            bit_count = 0U;
            value = 0U;
            result->frame_count++;
            continue;
        }
        if(!previous_sda && current_sda && current_scl) {
            active = false;
            bit_count = 0U;
            continue;
        }
        if(!active || previous_scl || !current_scl) continue;

        if(bit_count < 8U) {
            value = (uint8_t)((value << 1U) | (current_sda ? 1U : 0U));
            bit_count++;
        } else {
            if(result->byte_count < TUMOSCOPE_MAX_DECODE_BYTES) {
                result->bytes[result->byte_count] = value;
                result->secondary[result->byte_count] = current_sda ? 1U : 0U;
                result->byte_count++;
            }
            if(current_sda) result->error_count++;
            result->event_count++;
            bit_count = 0U;
            value = 0U;
        }
    }
}

void tumoscope_decode_spi_mode0(
    const uint8_t* samples,
    size_t sample_count,
    uint8_t mosi_channel,
    uint8_t miso_channel,
    uint8_t clock_channel,
    TumoScopeDecodeResult* result) {
    tumoscope_decode_reset(result, TumoScopeDecoderSpiMode0);
    if(!samples || sample_count < 2U || mosi_channel >= TUMOSCOPE_CHANNEL_COUNT ||
       miso_channel >= TUMOSCOPE_CHANNEL_COUNT || clock_channel >= TUMOSCOPE_CHANNEL_COUNT) {
        return;
    }

    uint8_t bit_count = 0U;
    uint8_t mosi = 0U;
    uint8_t miso = 0U;
    for(size_t index = 1U; index < sample_count; index++) {
        const bool previous_clock = tumoscope_sample_level(samples[index - 1U], clock_channel);
        const bool current_clock = tumoscope_sample_level(samples[index], clock_channel);
        if(previous_clock || !current_clock) continue;

        mosi = (uint8_t)((mosi << 1U) |
                         (tumoscope_sample_level(samples[index], mosi_channel) ? 1U : 0U));
        miso = (uint8_t)((miso << 1U) |
                         (tumoscope_sample_level(samples[index], miso_channel) ? 1U : 0U));
        bit_count++;
        if(bit_count == 8U) {
            if(result->byte_count < TUMOSCOPE_MAX_DECODE_BYTES) {
                result->bytes[result->byte_count] = mosi;
                result->secondary[result->byte_count] = miso;
                result->byte_count++;
            }
            result->frame_count++;
            bit_count = 0U;
            mosi = 0U;
            miso = 0U;
        }
    }
    if(bit_count != 0U) result->error_count++;
}

void tumoscope_decode_one_wire(
    const uint8_t* samples,
    size_t sample_count,
    uint32_t sample_rate,
    uint8_t channel,
    TumoScopeDecodeResult* result) {
    tumoscope_decode_reset(result, TumoScopeDecoderOneWire);
    if(!samples || sample_count < 2U || !sample_rate || channel >= TUMOSCOPE_CHANNEL_COUNT) return;

    uint8_t bit_count = 0U;
    uint8_t value = 0U;
    size_t index = 1U;
    while(index < sample_count) {
        const bool previous = tumoscope_sample_level(samples[index - 1U], channel);
        const bool current = tumoscope_sample_level(samples[index], channel);
        if(!previous || current) {
            index++;
            continue;
        }

        const size_t low_start = index;
        while(index < sample_count && !tumoscope_sample_level(samples[index], channel))
            index++;
        if(index >= sample_count) break;
        const uint64_t low_us =
            ((uint64_t)(index - low_start) * 1000000ULL + sample_rate / 2U) / sample_rate;
        if(low_us >= 400U) {
            result->event_count++;
            bit_count = 0U;
            value = 0U;
            continue;
        }

        const bool bit = low_us < 30U;
        if(bit) value |= 1U << bit_count;
        bit_count++;
        if(bit_count == 8U) {
            if(result->byte_count < TUMOSCOPE_MAX_DECODE_BYTES) {
                result->bytes[result->byte_count++] = value;
            }
            result->frame_count++;
            bit_count = 0U;
            value = 0U;
        }
    }
    if(bit_count != 0U) result->error_count++;
}
