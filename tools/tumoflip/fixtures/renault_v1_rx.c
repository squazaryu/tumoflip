#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lib/toolbox/manchester_decoder.h>

#define furi_check(x)  assert(x)
#define furi_assert(x) assert(x)
#define UNUSED(x)      ((void)(x))
#define COUNT_OF(x)    (sizeof(x) / sizeof(*(x)))
#define SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(x)
typedef enum {
    SubGhzProtocolStatusOk,
    SubGhzProtocolStatusErrorParserOthers,
    SubGhzProtocolStatusErrorValueBitCount
} SubGhzProtocolStatus;
typedef enum {
    SubGhzProtocolTypeDynamic
} SubGhzProtocolType;
enum {
    SubGhzProtocolFlag_315 = 1,
    SubGhzProtocolFlag_433 = 2,
    SubGhzProtocolFlag_868 = 4,
    SubGhzProtocolFlag_AM = 8,
    SubGhzProtocolFlag_FM = 16,
    SubGhzProtocolFlag_Decodable = 32,
    SubGhzProtocolFlag_Load = 64,
    SubGhzProtocolFlag_Save = 128,
    SubGhzProtocolFlag_Send = 256
};
typedef struct {
    char text[512];
} FuriString;
static void furi_string_printf(FuriString* value, const char* format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(value->text, sizeof(value->text), format, args);
    va_end(args);
}
typedef struct SubGhzProtocol SubGhzProtocol;
typedef struct SubGhzProtocolDecoderBase {
    const SubGhzProtocol* protocol;
    void (*callback)(struct SubGhzProtocolDecoderBase*, void*);
    void* context;
} SubGhzProtocolDecoderBase;
typedef struct {
    uint64_t decode_data;
    uint32_t te_last, decode_count_bit, parser_step;
} SubGhzBlockDecoder;
typedef struct {
    const char* protocol_name;
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
typedef struct {
    int unused;
} SubGhzEnvironment;
typedef struct {
    char key[24];
    uint32_t value;
    uint8_t hex[16];
    uint16_t bytes;
} Field;
typedef struct {
    Field fields[24];
    unsigned count;
    const char* fail_key;
    uint16_t bits;
    uint64_t data;
} FlipperFormat;
typedef struct {
    void* (*alloc)(SubGhzEnvironment*);
    void (*free)(void*);
    void (*feed)(void*, bool, uint32_t);
    void (*reset)(void*);
    uint8_t (*get_hash_data)(void*);
    SubGhzProtocolStatus (*serialize)(void*, FlipperFormat*, SubGhzRadioPreset*);
    SubGhzProtocolStatus (*deserialize)(void*, FlipperFormat*);
    void (*get_string)(void*, FuriString*);
} SubGhzProtocolDecoder;
struct SubGhzProtocol {
    const char* name;
    SubGhzProtocolType type;
    unsigned flag;
    const SubGhzProtocolDecoder* decoder;
    const void* encoder;
};
static void subghz_protocol_decoder_common_free(void* value) {
    free(value);
}
static Field* field(FlipperFormat* ff, const char* key) {
    for(unsigned i = 0; i < ff->count; i++)
        if(!strcmp(ff->fields[i].key, key)) return &ff->fields[i];
    assert(ff->count < 24);
    Field* f = &ff->fields[ff->count++];
    memset(f, 0, sizeof(*f));
    snprintf(f->key, sizeof(f->key), "%s", key);
    return f;
}
static bool flipper_format_rewind(FlipperFormat* ff) {
    return ff != NULL;
}
static bool flipper_format_insert_or_update_uint32(
    FlipperFormat* ff,
    const char* key,
    const uint32_t* value,
    uint16_t count) {
    assert(count == 1);
    if(ff->fail_key && !strcmp(ff->fail_key, key)) return false;
    field(ff, key)->value = *value;
    return true;
}
static bool flipper_format_read_uint32(
    FlipperFormat* ff,
    const char* key,
    uint32_t* value,
    uint16_t count) {
    assert(count == 1);
    for(unsigned i = 0; i < ff->count; i++)
        if(!strcmp(ff->fields[i].key, key)) {
            *value = ff->fields[i].value;
            return true;
        }
    return false;
}
static bool flipper_format_insert_or_update_hex(
    FlipperFormat* ff,
    const char* key,
    const uint8_t* data,
    uint16_t count) {
    if(ff->fail_key && !strcmp(ff->fail_key, key)) return false;
    Field* f = field(ff, key);
    assert(count <= 16);
    memcpy(f->hex, data, count);
    f->bytes = count;
    return true;
}
static bool flipper_format_get_value_count(FlipperFormat* ff, const char* key, uint32_t* count) {
    for(unsigned i = 0; i < ff->count; i++)
        if(!strcmp(ff->fields[i].key, key)) {
            *count = ff->fields[i].bytes;
            return true;
        }
    return false;
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
    SubGhzBlockGeneric* g,
    FlipperFormat* ff,
    SubGhzRadioPreset* preset) {
    UNUSED(preset);
    ff->count = 0;
    ff->bits = g->data_count_bit;
    ff->data = g->data;
    return SubGhzProtocolStatusOk;
}
static SubGhzProtocolStatus subghz_block_generic_deserialize_check_count_bit(
    SubGhzBlockGeneric* g,
    FlipperFormat* ff,
    uint32_t min) {
    g->data_count_bit = ff->bits;
    g->data = ff->data;
    return ff->bits >= min ? SubGhzProtocolStatusOk : SubGhzProtocolStatusErrorValueBitCount;
}

/* PRODUCTION */

static unsigned hits;
static void found(SubGhzProtocolDecoderBase* base, void* context) {
    UNUSED(base);
    UNUSED(context);
    hits++;
}
static void checksum(uint8_t raw[11]) {
    raw[10] = 0;
    for(unsigned i = 0; i < 10; i++)
        raw[10] ^= raw[i];
}
static void pulse(void* decoder, bool high, uint32_t duration) {
    subghz_protocol_decoder_renault_v1_feed(decoder, high, duration);
}
static void trace(void* decoder, const uint8_t raw[11], uint16_t header, unsigned bits) {
    pulse(decoder, false, 1500);
    pulse(decoder, true, 1000);
    bool previous = true;
    for(unsigned i = 0; i < bits; i++) {
        bool wire = i < 16 ? ((header >> (15 - i)) & 1) :
                             ((raw[(i - 16) / 8] >> (7 - (i - 16) % 8)) & 1);
        bool decoded = !wire;
        if(i == 0) {
            assert(decoded);
            pulse(decoder, false, 125);
        } else if(decoded == previous) {
            pulse(decoder, previous, 125);
            pulse(decoder, !previous, 125);
        } else
            pulse(decoder, previous, 250);
        previous = decoded;
    }
    pulse(decoder, false, 10000);
}
static void remove_field(FlipperFormat* ff, const char* key) {
    for(unsigned i = 0; i < ff->count; i++)
        if(!strcmp(ff->fields[i].key, key)) {
            memmove(&ff->fields[i], &ff->fields[i + 1], (--ff->count - i) * sizeof(Field));
            return;
        }
}
int main(int argc, char** argv) {
    assert(argc == 2);
    hits = 0;
    uint8_t raw[11] = {0x12, 0x34, 0x56, 0x78, 0x2a, 0xbc, 0x12, 0x34, 0x56, 0x78, 0};
    checksum(raw);
    SubGhzProtocolDecoderRenaultV1* decoder = subghz_protocol_decoder_renault_v1_alloc(NULL);
    assert(decoder);
    decoder->base.callback = found;
    if(!strcmp(argv[1], "rx-only")) {
        assert(subghz_protocol_renault_v1.encoder == NULL);
        assert(!(subghz_protocol_renault_v1.flag & SubGhzProtocolFlag_Send));
    } else if(!strcmp(argv[1], "trace")) {
        trace(decoder, raw, 1, 104);
        assert(hits == 1);
        assert(decoder->generic.serial == 0x12345678);
        assert(decoder->generic.btn == 2 && decoder->generic.data_count_bit == 88);
        trace(decoder, raw, 1, 104);
        assert(hits == 1);
        subghz_protocol_decoder_renault_v1_reset(decoder);
        trace(decoder, raw, 1, 104);
        assert(hits == 2);
        raw[9] ^= 1;
        checksum(raw);
        trace(decoder, raw, 1, 104);
        assert(hits == 3);
    } else if(!strcmp(argv[1], "invalid")) {
        trace(decoder, raw, 2, 104);
        assert(hits == 0);
        raw[10] ^= 1;
        trace(decoder, raw, 1, 104);
        assert(hits == 0);
        raw[10] ^= 1;
        trace(decoder, raw, 1, 100);
        assert(hits == 0);
        for(unsigned i = 0; i < 500; i++)
            pulse(decoder, (i & 1) != 0, 50 + (i * 71) % 700);
        assert(hits == 0);
        trace(decoder, raw, 1, 104);
        assert(hits == 1);
    } else {
        trace(decoder, raw, 1, 104);
        assert(hits == 1);
        FlipperFormat ff = {0};
        SubGhzRadioPreset preset = {0};
        assert(
            subghz_protocol_decoder_renault_v1_serialize(decoder, &ff, &preset) ==
            SubGhzProtocolStatusOk);
        uint64_t old = decoder->generic.data;
        if(!strcmp(argv[1], "file")) {
            SubGhzProtocolDecoderRenaultV1* loaded =
                subghz_protocol_decoder_renault_v1_alloc(NULL);
            assert(
                subghz_protocol_decoder_renault_v1_deserialize(loaded, &ff) ==
                SubGhzProtocolStatusOk);
            assert(loaded->generic.data == old && !memcmp(loaded->raw, raw, 11));
            remove_field(&ff, "Raw");
            Field* key2 = field(&ff, "Key2");
            snprintf(key2->key, sizeof(key2->key), "Key_2");
            assert(
                subghz_protocol_decoder_renault_v1_deserialize(loaded, &ff) ==
                SubGhzProtocolStatusOk);
            assert(!memcmp(loaded->raw, raw, 11));
            memmove(key2->hex, key2->hex + 5, 3);
            key2->bytes = 3;
            assert(
                subghz_protocol_decoder_renault_v1_deserialize(loaded, &ff) ==
                SubGhzProtocolStatusOk);
            subghz_protocol_decoder_common_free(loaded);
        } else {
            field(&ff, "Raw")->hex[10] ^= 1;
            ff.data = 0;
            assert(
                subghz_protocol_decoder_renault_v1_deserialize(decoder, &ff) !=
                SubGhzProtocolStatusOk);
            assert(decoder->generic.data == old && !memcmp(decoder->raw, raw, 11));
            const char* keys[] = {"Raw", "Key2", "Header", "Serial", "Btn", "Cnt", "Hop"};
            for(unsigned i = 0; i < COUNT_OF(keys); i++) {
                ff.fail_key = keys[i];
                assert(
                    subghz_protocol_decoder_renault_v1_serialize(decoder, &ff, &preset) !=
                    SubGhzProtocolStatusOk);
            }
        }
    }
    subghz_protocol_decoder_common_free(decoder);
    return 0;
}
