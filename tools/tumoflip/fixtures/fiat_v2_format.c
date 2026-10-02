#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define furi_check(x) assert(x)
#define FF_SERIAL     "Serial"
#define FF_BTN        "Btn"
#define FF_CNT        "Cnt"
typedef enum {
    SubGhzProtocolStatusOk,
    SubGhzProtocolStatusErrorParserOthers,
    SubGhzProtocolStatusErrorValueBitCount
} SubGhzProtocolStatus;
typedef struct {
    int unused;
} SubGhzProtocolDecoderBase;
typedef struct {
    uint64_t decode_data;
    uint16_t decode_count_bit;
} SubGhzBlockDecoder;
typedef struct {
    uint64_t data;
    uint32_t serial, cnt;
    uint16_t data_count_bit;
    uint8_t btn;
} SubGhzBlockGeneric;
typedef struct {
    uint32_t te_short, te_long, te_delta, min_count_bit_for_found;
} SubGhzBlockConst;
typedef struct {
    int unused;
} SubGhzRadioPreset;
typedef struct SubGhzProtocolDecoderFiatV2 SubGhzProtocolDecoderFiatV2;
typedef struct {
    char key[24];
    uint32_t value;
    uint8_t hex[16];
    uint16_t bytes;
} Field;
typedef struct {
    Field fields[20];
    unsigned count;
    const char* fail_key;
    uint16_t bits;
    uint64_t data;
} FlipperFormat;

static Field* field(FlipperFormat* ff, const char* key, bool append) {
    if(!append)
        for(unsigned i = 0; i < ff->count; i++)
            if(!strcmp(ff->fields[i].key, key)) return &ff->fields[i];
    assert(ff->count < 20);
    Field* value = &ff->fields[ff->count++];
    memset(value, 0, sizeof(*value));
    snprintf(value->key, sizeof(value->key), "%s", key);
    return value;
}
static bool failed(FlipperFormat* ff, const char* key) {
    return ff->fail_key && !strcmp(ff->fail_key, key);
}
static bool flipper_format_rewind(FlipperFormat* ff) {
    return ff != NULL;
}
static bool flipper_format_write_uint32(
    FlipperFormat* ff,
    const char* key,
    const uint32_t* data,
    uint16_t count) {
    assert(count == 1);
    if(failed(ff, key)) return false;
    field(ff, key, true)->value = *data;
    return true;
}
static bool flipper_format_insert_or_update_uint32(
    FlipperFormat* ff,
    const char* key,
    const uint32_t* data,
    uint16_t count) {
    assert(count == 1);
    if(failed(ff, key)) return false;
    field(ff, key, false)->value = *data;
    return true;
}
static void pp_flipper_update_or_insert_u32(FlipperFormat* ff, const char* key, uint32_t value) {
    (void)flipper_format_insert_or_update_uint32(ff, key, &value, 1);
}
static bool flipper_format_insert_or_update_hex(
    FlipperFormat* ff,
    const char* key,
    const uint8_t* data,
    uint16_t count) {
    if(failed(ff, key)) return false;
    Field* value = field(ff, key, false);
    assert(count <= sizeof(value->hex));
    memcpy(value->hex, data, count);
    value->bytes = count;
    return true;
}
static bool
    flipper_format_read_hex(FlipperFormat* ff, const char* key, uint8_t* data, uint16_t count) {
    for(unsigned i = 0; i < ff->count; i++)
        if(!strcmp(ff->fields[i].key, key) && ff->fields[i].bytes == count) {
            memcpy(data, ff->fields[i].hex, count);
            return true;
        }
    return false;
}
static SubGhzProtocolStatus subghz_block_generic_serialize(
    SubGhzBlockGeneric* generic,
    FlipperFormat* ff,
    SubGhzRadioPreset* preset) {
    (void)preset;
    ff->count = 0;
    ff->bits = generic->data_count_bit;
    ff->data = generic->data;
    return SubGhzProtocolStatusOk;
}
static SubGhzProtocolStatus subghz_block_generic_deserialize_check_count_bit(
    SubGhzBlockGeneric* generic,
    FlipperFormat* ff,
    uint32_t minimum) {
    generic->data_count_bit = ff->bits;
    generic->data = ff->data;
    return ff->bits >= minimum ? SubGhzProtocolStatusOk : SubGhzProtocolStatusErrorValueBitCount;
}

/* PRODUCTION */

int main(int argc, char** argv) {
    assert(argc == 2);
    const uint8_t raw[14] = {
        0, 1, 0x12, 0x34, 0x56, 0x78, 0xd0, 0x80, 0x33, 0x44, 0xab, 0xcd, 0xef, 0x01};
    SubGhzProtocolDecoderFiatV2 input = {0}, output = {0};
    memcpy(input.raw_data, raw, sizeof(raw));
    fiat_v2_decode_fields(&input);
    FlipperFormat ff = {0};
    SubGhzRadioPreset preset = {0};
    if(!strcmp(argv[1], "roundtrip")) {
        assert(
            subghz_protocol_decoder_fiat_v2_serialize(&input, &ff, &preset) ==
            SubGhzProtocolStatusOk);
        assert(ff.count == 5);
        assert(field(&ff, "Hop", false)->value == input.hop);
        assert(field(&ff, "Btn", false)->value == input.button);
        assert(
            subghz_protocol_decoder_fiat_v2_deserialize(&output, &ff) == SubGhzProtocolStatusOk);
        assert(!memcmp(input.raw_data, output.raw_data, sizeof(raw)));
        assert(output.generic.serial == input.generic.serial && output.hop == input.hop);
        assert(output.button == input.button && output.generic.cnt == input.generic.cnt);
    } else if(!strcmp(argv[1], "invalid")) {
        assert(
            subghz_protocol_decoder_fiat_v2_serialize(&input, &ff, &preset) ==
            SubGhzProtocolStatusOk);
        output = input;
        ff.data = 0;
        field(&ff, "Raw", false)->hex[0] = 0xff;
        assert(
            subghz_protocol_decoder_fiat_v2_deserialize(&output, &ff) != SubGhzProtocolStatusOk);
        assert(output.generic.data == input.generic.data);
        assert(!memcmp(output.raw_data, input.raw_data, sizeof(raw)));
    } else {
        ff.fail_key = argv[1];
        assert(
            subghz_protocol_decoder_fiat_v2_serialize(&input, &ff, &preset) !=
            SubGhzProtocolStatusOk);
    }
    return 0;
}
