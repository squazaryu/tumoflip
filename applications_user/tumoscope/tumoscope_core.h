#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TUMOSCOPE_CHANNEL_COUNT    3U
#define TUMOSCOPE_MAX_DECODE_BYTES 32U

typedef enum {
    TumoScopeDecoderRaw,
    TumoScopeDecoderUart,
    TumoScopeDecoderI2c,
    TumoScopeDecoderSpiMode0,
    TumoScopeDecoderOneWire,
} TumoScopeDecoder;

typedef struct {
    TumoScopeDecoder decoder;
    uint8_t bytes[TUMOSCOPE_MAX_DECODE_BYTES];
    uint8_t secondary[TUMOSCOPE_MAX_DECODE_BYTES];
    size_t byte_count;
    uint32_t frame_count;
    uint32_t error_count;
    uint32_t event_count;
} TumoScopeDecodeResult;

bool tumoscope_sample_level(uint8_t sample, uint8_t channel);

void tumoscope_decode_uart(
    const uint8_t* samples,
    size_t sample_count,
    uint32_t sample_rate,
    uint32_t baud,
    uint8_t channel,
    TumoScopeDecodeResult* result);

void tumoscope_decode_i2c(
    const uint8_t* samples,
    size_t sample_count,
    uint8_t sda_channel,
    uint8_t scl_channel,
    TumoScopeDecodeResult* result);

void tumoscope_decode_spi_mode0(
    const uint8_t* samples,
    size_t sample_count,
    uint8_t mosi_channel,
    uint8_t miso_channel,
    uint8_t clock_channel,
    TumoScopeDecodeResult* result);

void tumoscope_decode_one_wire(
    const uint8_t* samples,
    size_t sample_count,
    uint32_t sample_rate,
    uint8_t channel,
    TumoScopeDecodeResult* result);
