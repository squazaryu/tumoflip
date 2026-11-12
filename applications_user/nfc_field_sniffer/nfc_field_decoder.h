#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NFC_FIELD_FRAME_CAP     24U
#define NFC_FIELD_FRAME_BYTES   24U
#define NFC_FIELD_SAMPLE_CYCLES 26U
#define NFC_FIELD_SAMPLE_BATCH  2048U
typedef struct {
    uint32_t start_us, end_us, parity_errors;
    uint16_t bits;
    uint8_t size;
    char direction;
    bool truncated, crc_match;
    uint8_t data[NFC_FIELD_FRAME_BYTES];
} NfcFieldFrame;
typedef struct {
    uint32_t count, frames_dropped, regions_dropped, overruns;
    NfcFieldFrame frames[NFC_FIELD_FRAME_CAP];
} NfcFieldTrace;

void nfc_field_decoder_reset(bool initial_level);
bool nfc_field_decoder_feed(const uint8_t* samples, size_t count);
void nfc_field_decoder_finish(void);
void nfc_field_decoder_discontinuity(bool initial_level);
const NfcFieldTrace* nfc_field_decoder_trace(void);
size_t nfc_field_decoder_workspace_size(void);
bool nfc_field_decoder_format(char* output, size_t size);
uint16_t nfc_field_crc_a(const uint8_t* bytes, size_t size);
