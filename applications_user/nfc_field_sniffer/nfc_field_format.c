#include "nfc_field_decoder.h"
#include <stdio.h>
#include <stdarg.h>

uint16_t nfc_field_crc_a(const uint8_t* bytes, size_t size) {
    uint16_t crc = 0x6363;
    for(size_t i = 0; i < size; i++) {
        uint8_t value = bytes[i] ^ (uint8_t)crc;
        value ^= (uint8_t)(value << 4);
        crc = (uint16_t)((crc >> 8) ^ ((uint16_t)value << 8) ^ ((uint16_t)value << 3) ^ (value >> 4));
    }
    return crc;
}
static bool nfc_field_append(char* output, size_t size, size_t* used, const char* format, ...) {
    if(*used >= size) return false;
    va_list arguments;
    va_start(arguments, format);
    int n = vsnprintf(output + *used, size - *used, format, arguments);
    va_end(arguments);
    if(n < 0 || (size_t)n >= size - *used) return false;
    *used += n;
    return true;
}
bool nfc_field_decoder_format(char* output, size_t size) {
    if(!output || !size) return false;
    output[0] = '\0';
    const NfcFieldTrace* trace = nfc_field_decoder_trace();
    size_t used = 0;
    bool ok = nfc_field_append(output, size, &used,
        "Passive NFC A / 106k\nFrames: %lu Dropped: %lu\nOverruns: %lu Regions lost: %lu\nR=reader C=card\nCRC unverified unless matched.\n\n",
        (unsigned long)trace->count, (unsigned long)trace->frames_dropped,
        (unsigned long)trace->overruns, (unsigned long)trace->regions_dropped);
    for(unsigned i = 0; ok && i < trace->count; i++) {
        const NfcFieldFrame* frame = &trace->frames[i];
        ok = nfc_field_append(output, size, &used, "%c %luus %ub:", frame->direction,
                               (unsigned long)frame->start_us, frame->bits);
        for(unsigned j = 0; ok && j < frame->size; j++)
            ok = nfc_field_append(output, size, &used, " %02X%s", frame->data[j],
                                   frame->parity_errors & (1UL << j) ? "!" : "");
        if(ok) ok = nfc_field_append(output, size, &used, "%s%s\n", frame->crc_match ? " CRC-A match" : "",
                                      frame->truncated ? " TRUNCATED" : "");
    }
    return ok;
}
