#include "tumoscope_core.h"

#include <string.h>

bool tumoscope_sample_level(uint8_t sample, uint8_t channel) {
    if(channel >= TUMOSCOPE_CHANNEL_COUNT) return false;
    return (sample & (1U << channel)) != 0U;
}

void tumoscope_analyze_channel(
    const uint8_t* samples,
    size_t sample_count,
    uint32_t sample_rate,
    uint8_t channel,
    TumoScopeChannelStats* stats) {
    if(!stats) return;
    memset(stats, 0, sizeof(*stats));
    if(!samples || sample_count < 2U || !sample_rate || channel >= TUMOSCOPE_CHANNEL_COUNT) return;

    size_t first_rising = 0U;
    size_t last_rising = 0U;
    for(size_t index = 1U; index < sample_count; index++) {
        const bool previous = tumoscope_sample_level(samples[index - 1U], channel);
        const bool current = tumoscope_sample_level(samples[index], channel);
        if(previous == current) continue;
        stats->transitions++;
        if(!previous && current) {
            if(stats->rising_edges == 0U) first_rising = index;
            last_rising = index;
            stats->rising_edges++;
        }
    }

    if(stats->rising_edges >= 2U && last_rising > first_rising) {
        stats->frequency_hz = (uint32_t)(((uint64_t)(stats->rising_edges - 1U) * sample_rate +
                                          (last_rising - first_rising) / 2U) /
                                         (last_rising - first_rising));
    }
}

static void tumoscope_demo_set_level(
    uint8_t* samples,
    size_t start,
    size_t count,
    uint8_t channel,
    bool high) {
    for(size_t index = start; index < start + count; index++) {
        if(high)
            samples[index] |= 1U << channel;
        else
            samples[index] &= ~(1U << channel);
    }
}

static bool
    tumoscope_generate_edge_demo(uint8_t* samples, size_t sample_count, uint32_t sample_rate) {
    static const uint32_t frequencies[] = {5000U, 3125U, 2500U};
    memset(samples, 0, sample_count);
    for(uint8_t channel = 0U; channel < TUMOSCOPE_CHANNEL_COUNT; channel++) {
        const size_t half_period = sample_rate / (2U * frequencies[channel]);
        if(!half_period) return false;
        for(size_t index = 0U; index < sample_count; index++) {
            if(((index / half_period) & 1U) != 0U) samples[index] |= 1U << channel;
        }
    }
    return true;
}

static bool
    tumoscope_generate_uart_demo(uint8_t* samples, size_t sample_count, uint32_t sample_rate) {
    const uint32_t baud = tumoscope_demo_uart_baud(sample_rate);
    const size_t samples_per_bit = sample_rate / baud;
    if(samples_per_bit < 3U) return false;
    memset(samples, 0x07, sample_count);

    static const uint8_t bytes[] = {0x55U, 0xA5U, 0x33U};
    size_t cursor = samples_per_bit * 2U;
    size_t byte_index = 0U;
    while(cursor + samples_per_bit * 11U <= sample_count) {
        const uint8_t value = bytes[byte_index++ % (sizeof(bytes) / sizeof(bytes[0]))];
        tumoscope_demo_set_level(samples, cursor, samples_per_bit, 0U, false);
        cursor += samples_per_bit;
        for(uint8_t bit = 0U; bit < 8U; bit++) {
            tumoscope_demo_set_level(
                samples, cursor, samples_per_bit, 0U, (value & (1U << bit)) != 0U);
            cursor += samples_per_bit;
        }
        tumoscope_demo_set_level(samples, cursor, samples_per_bit * 2U, 0U, true);
        cursor += samples_per_bit * 2U;
    }
    return byte_index > 0U;
}

uint32_t tumoscope_demo_uart_baud(uint32_t sample_rate) {
    if(sample_rate <= 100000U) return 9600U;
    if(sample_rate <= 250000U) return 19200U;
    if(sample_rate <= 500000U) return 38400U;
    return 115200U;
}

static bool tumoscope_demo_append_i2c_level(
    uint8_t* samples,
    size_t sample_count,
    size_t* cursor,
    size_t hold,
    bool sda,
    bool scl) {
    if(*cursor + hold > sample_count) return false;
    const uint8_t level = (sda ? 1U : 0U) | (scl ? 2U : 0U) | 4U;
    memset(&samples[*cursor], level, hold);
    *cursor += hold;
    return true;
}

static bool
    tumoscope_generate_i2c_demo(uint8_t* samples, size_t sample_count, uint32_t sample_rate) {
    const size_t calculated_hold = (size_t)sample_rate / 50000U;
    const size_t hold = calculated_hold > 2U ? calculated_hold : 2U;
    if(sample_count < hold * 24U) return false;
    memset(samples, 0x07, sample_count);

    size_t cursor = 0U;
    uint8_t value = 0xA0U;
    while(cursor + hold * 24U <= sample_count) {
        if(!tumoscope_demo_append_i2c_level(samples, sample_count, &cursor, hold, true, true) ||
           !tumoscope_demo_append_i2c_level(samples, sample_count, &cursor, hold, false, true))
            return false;
        for(uint8_t bit = 0U; bit < 8U; bit++) {
            const bool data = (value & (0x80U >> bit)) != 0U;
            if(!tumoscope_demo_append_i2c_level(
                   samples, sample_count, &cursor, hold, data, false) ||
               !tumoscope_demo_append_i2c_level(samples, sample_count, &cursor, hold, data, true))
                return false;
        }
        if(!tumoscope_demo_append_i2c_level(samples, sample_count, &cursor, hold, false, false) ||
           !tumoscope_demo_append_i2c_level(samples, sample_count, &cursor, hold, false, true) ||
           !tumoscope_demo_append_i2c_level(samples, sample_count, &cursor, hold, true, true))
            return false;
        value++;
    }
    return true;
}

bool tumoscope_generate_demo(
    TumoScopeDemo demo,
    uint8_t* samples,
    size_t sample_count,
    uint32_t sample_rate) {
    if(!samples || sample_count < 64U || !sample_rate) return false;
    switch(demo) {
    case TumoScopeDemoEdge:
        return tumoscope_generate_edge_demo(samples, sample_count, sample_rate);
    case TumoScopeDemoUart:
        return tumoscope_generate_uart_demo(samples, sample_count, sample_rate);
    case TumoScopeDemoI2c:
        return tumoscope_generate_i2c_demo(samples, sample_count, sample_rate);
    case TumoScopeDemoNone:
    default:
        return false;
    }
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
