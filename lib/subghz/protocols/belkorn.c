/* Receive/inspect adaptation of Unleashed BelKorn through 1a5676a0a6 (GPL-3.0).
 * No encoder, origin reconstruction, future-counter generation or enrollment.
 * Complete repeated frames and file bounds are checked independently here.
 */
#include "belkorn.h"
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"
#include "common.h"

#define BELKORN_SYNC    0x801ULL
#define BELKORN_GAP_MIN 2500U
#define BELKORN_GAP_MAX 1000000U

static const SubGhzBlockConst subghz_protocol_belkorn_const = {
    .te_short = 481,
    .te_long = 962,
    .te_delta = 180,
    .min_count_bit_for_found = 118,
};

typedef enum {
    BelKornDecoderStepReset,
    BelKornDecoderStepData,
    BelKornDecoderStepMark,
    BelKornDecoderStepGap,
} BelKornDecoderStep;

struct SubGhzProtocolDecoderBelKorn {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    uint64_t packet_hi, last_hi, last_lo;
    bool have_last, reported;
};
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderBelKorn);

static const SubGhzProtocolDecoder subghz_protocol_belkorn_decoder = {
    .alloc = subghz_protocol_decoder_belkorn_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_belkorn_feed,
    .reset = subghz_protocol_decoder_belkorn_reset,
    .get_hash_data = subghz_protocol_decoder_belkorn_get_hash_data,
    .serialize = subghz_protocol_decoder_belkorn_serialize,
    .deserialize = subghz_protocol_decoder_belkorn_deserialize,
    .get_string = subghz_protocol_decoder_belkorn_get_string,
};

const SubGhzProtocol subghz_protocol_belkorn = {
    .name = SUBGHZ_PROTOCOL_BELKORN_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save,
    .decoder = &subghz_protocol_belkorn_decoder,
    .encoder = NULL,
};

static bool subghz_protocol_belkorn_frame_valid(uint64_t data, uint64_t data_2) {
    uint8_t button = data_2 & 0x3F;
    return (data >> 52) == BELKORN_SYNC && !(data_2 >> 54) && button && !(button & (button - 1));
}

static void subghz_protocol_belkorn_metadata(SubGhzBlockGeneric* generic) {
    generic->serial = (uint32_t)(generic->data >> 16);
    generic->btn = generic->data_2 & 0x3F;
    uint8_t low = (generic->data_2 >> 6) & 0xFF;
    uint8_t high = (generic->data_2 >> ((low & 1) ? 22 : 14)) & 0xFF;
    generic->cnt = (uint16_t)((uint16_t)high << 8) | low;
}

void subghz_protocol_decoder_belkorn_reset(void* context) {
    SubGhzProtocolDecoderBelKorn* instance = context;
    furi_assert(instance);
    instance->decoder = (SubGhzBlockDecoder){0};
    instance->generic.data = 0;
    instance->generic.data_2 = 0;
    instance->generic.data_count_bit = 0;
    instance->packet_hi = instance->last_hi = instance->last_lo = 0;
    instance->have_last = instance->reported = false;
}

void* subghz_protocol_decoder_belkorn_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderBelKorn* instance = subghz_protocol_decoder_common_alloc(
        sizeof(SubGhzProtocolDecoderBelKorn), &subghz_protocol_belkorn);
    subghz_protocol_decoder_belkorn_reset(instance);
    return instance;
}

void subghz_protocol_decoder_belkorn_feed(void* context, bool level, uint32_t duration) {
    SubGhzProtocolDecoderBelKorn* instance = context;
    furi_assert(instance);
    const bool gap = !level && duration >= BELKORN_GAP_MIN && duration <= BELKORN_GAP_MAX;
    const bool short_bit = DURATION_DIFF(duration, subghz_protocol_belkorn_const.te_short) <
                           subghz_protocol_belkorn_const.te_delta;
    const bool long_bit = DURATION_DIFF(duration, subghz_protocol_belkorn_const.te_long) <
                          subghz_protocol_belkorn_const.te_delta;
    switch(instance->decoder.parser_step) {
    case BelKornDecoderStepReset:
        if(gap) {
            instance->decoder.decode_count_bit = 0;
            instance->decoder.decode_data = 0;
            instance->decoder.parser_step = BelKornDecoderStepData;
        }
        return;
    case BelKornDecoderStepData:
        if(gap && instance->decoder.decode_count_bit == 0) return;
        if(instance->decoder.decode_count_bit >= 118 ||
           level != ((instance->decoder.decode_count_bit & 1) == 0) || (!short_bit && !long_bit))
            goto reset;
        subghz_protocol_blocks_add_bit(&instance->decoder, long_bit);
        if(instance->decoder.decode_count_bit == 64) {
            instance->packet_hi = instance->decoder.decode_data;
            instance->decoder.decode_data = 0;
        }
        if(instance->decoder.decode_count_bit == 118) {
            instance->decoder.parser_step = BelKornDecoderStepMark;
        }
        return;
    case BelKornDecoderStepMark:
        if(!level || (!short_bit && !long_bit)) goto reset;
        instance->decoder.parser_step = BelKornDecoderStepGap;
        return;
    case BelKornDecoderStepGap:
        if(!gap) goto reset;
        if(subghz_protocol_belkorn_frame_valid(
               instance->packet_hi, instance->decoder.decode_data)) {
            if(instance->have_last && instance->packet_hi == instance->last_hi &&
               instance->decoder.decode_data == instance->last_lo) {
                if(!instance->reported) {
                    instance->generic.data = instance->packet_hi;
                    instance->generic.data_2 = instance->decoder.decode_data;
                    instance->generic.data_count_bit = 118;
                    subghz_protocol_belkorn_metadata(&instance->generic);
                    if(instance->base.callback) {
                        instance->reported = true;
                        instance->base.callback(&instance->base, instance->base.context);
                    }
                }
            } else {
                instance->last_hi = instance->packet_hi;
                instance->last_lo = instance->decoder.decode_data;
                instance->have_last = true;
                instance->reported = false;
            }
        } else {
            instance->have_last = instance->reported = false;
        }
        instance->decoder.parser_step = BelKornDecoderStepData;
        instance->decoder.decode_count_bit = 0;
        instance->decoder.decode_data = instance->packet_hi = 0;
        return;
    default:
        break;
    }
reset:
    instance->decoder.parser_step = gap ? BelKornDecoderStepData : BelKornDecoderStepReset;
    instance->decoder.decode_count_bit = 0;
    instance->decoder.decode_data = instance->packet_hi = 0;
    instance->have_last = instance->reported = false;
}

uint8_t subghz_protocol_decoder_belkorn_get_hash_data(void* context) {
    SubGhzProtocolDecoderBelKorn* instance = context;
    furi_assert(instance);
    const uint64_t words[] = {instance->generic.data, instance->generic.data_2};
    uint8_t hash = 0;
    for(size_t word = 0; word < COUNT_OF(words); ++word)
        for(size_t byte = 0; byte < sizeof(uint64_t); ++byte)
            hash ^= words[word] >> (byte * 8);
    return hash;
}

SubGhzProtocolStatus subghz_protocol_decoder_belkorn_serialize(
    void* context,
    FlipperFormat* format,
    SubGhzRadioPreset* preset) {
    SubGhzProtocolDecoderBelKorn* instance = context;
    furi_assert(instance);
    if(instance->generic.data_count_bit != 118 ||
       !subghz_protocol_belkorn_frame_valid(instance->generic.data, instance->generic.data_2))
        return SubGhzProtocolStatusErrorParserOthers;
    SubGhzProtocolStatus result =
        subghz_block_generic_serialize(&instance->generic, format, preset);
    if(result != SubGhzProtocolStatusOk) return result;
    uint8_t tail[8];
    for(size_t i = 0; i < sizeof(tail); ++i)
        tail[sizeof(tail) - 1 - i] = instance->generic.data_2 >> (i * 8);
    return flipper_format_write_hex(format, "Data", tail, sizeof(tail)) ?
               SubGhzProtocolStatusOk :
               SubGhzProtocolStatusErrorParserOthers;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_belkorn_deserialize(void* context, FlipperFormat* format) {
    SubGhzProtocolDecoderBelKorn* instance = context;
    furi_assert(instance);
    SubGhzBlockGeneric candidate = instance->generic;
    SubGhzProtocolStatus result =
        subghz_block_generic_deserialize_check_count_bit(&candidate, format, 118);
    if(result != SubGhzProtocolStatusOk) return result;
    uint32_t count = 0;
    uint8_t tail[8] = {0}, alias[8] = {0};
    if(!flipper_format_rewind(format)) return SubGhzProtocolStatusErrorParserOthers;
    bool has_data = flipper_format_get_value_count(format, "Data", &count);
    if(has_data && (count != sizeof(tail) || !flipper_format_rewind(format) ||
                    !flipper_format_read_hex(format, "Data", tail, sizeof(tail))))
        return SubGhzProtocolStatusErrorParserOthers;
    if(!flipper_format_rewind(format)) return SubGhzProtocolStatusErrorParserOthers;
    bool has_alias = flipper_format_get_value_count(format, "Key2", &count);
    if(has_alias && (count != sizeof(alias) || !flipper_format_rewind(format) ||
                     !flipper_format_read_hex(format, "Key2", alias, sizeof(alias))))
        return SubGhzProtocolStatusErrorParserOthers;
    if((!has_data && !has_alias) || (has_data && has_alias && memcmp(tail, alias, sizeof(tail))))
        return SubGhzProtocolStatusErrorParserOthers;
    if(!has_data) memcpy(tail, alias, sizeof(tail));
    candidate.data_2 = 0;
    for(size_t i = 0; i < sizeof(tail); ++i)
        candidate.data_2 = (candidate.data_2 << 8) | tail[i];
    if(!subghz_protocol_belkorn_frame_valid(candidate.data, candidate.data_2))
        return SubGhzProtocolStatusErrorParserOthers;
    subghz_protocol_belkorn_metadata(&candidate);
    instance->generic = candidate;
    instance->decoder = (SubGhzBlockDecoder){0};
    instance->packet_hi = instance->last_hi = instance->last_lo = 0;
    instance->have_last = instance->reported = false;
    return SubGhzProtocolStatusOk;
}

void subghz_protocol_decoder_belkorn_get_string(void* context, FuriString* output) {
    SubGhzProtocolDecoderBelKorn* instance = context;
    furi_assert(instance);
    subghz_protocol_belkorn_metadata(&instance->generic);
    subghz_block_generic_global.cnt_is_available = false;
    subghz_block_generic_global.btn_is_available = false;
    furi_string_printf(
        output,
        "BelKorn 118b RX\r\nKey:%08lX%08lX\r\nTail:%08lX%08lX\r\nSn:%01lX%08lX B:%02X\r\nCnt:%04lX",
        (unsigned long)(instance->generic.data >> 32),
        (unsigned long)(uint32_t)instance->generic.data,
        (unsigned long)(instance->generic.data_2 >> 32),
        (unsigned long)(uint32_t)instance->generic.data_2,
        (unsigned long)((instance->generic.data >> 48) & 0xF),
        (unsigned long)instance->generic.serial,
        (unsigned)instance->generic.btn,
        (unsigned long)instance->generic.cnt);
}
