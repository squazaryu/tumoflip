/* FAAC TM static 12-DIP framing adapted from Unleashed 24f539c382 (GPL-3.0).
 * External-only implementation; no public decoder layout or SDK change.
 */
#include "faac_tm.h"
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"
#include "common.h"

#define TAG               "SubGhzProtocolFaacTm"
#define FAAC_TM_HEADER_TE 35
#define FAAC_TM_TE_MIN    100
#define FAAC_TM_TE_MAX    1000

static const SubGhzBlockConst subghz_protocol_faac_tm_const = {
    .te_short = 400,
    .te_long = 800,
    .te_delta = 120,
    .min_count_bit_for_found = 12,
};

struct SubGhzProtocolDecoderFaacTm {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    uint32_t te;
    uint32_t te_sum;
    uint64_t last_data;
    bool have_last;
};
SUBGHZ_ASSERT_DECODER_TE_LAYOUT(SubGhzProtocolDecoderFaacTm);

struct SubGhzProtocolEncoderFaacTm {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
    uint32_t te;
};
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderFaacTm);

typedef enum {
    FaacTmDecoderStepReset,
    FaacTmDecoderStepFoundStartBit,
    FaacTmDecoderStepSaveDuration,
    FaacTmDecoderStepCheckDuration,
} FaacTmDecoderStep;

static const SubGhzProtocolDecoder subghz_protocol_faac_tm_decoder = {
    .alloc = subghz_protocol_decoder_faac_tm_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_faac_tm_feed,
    .reset = subghz_protocol_decoder_faac_tm_reset,
    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize_te,
    .deserialize = subghz_protocol_decoder_faac_tm_deserialize,
    .get_string = subghz_protocol_decoder_faac_tm_get_string,
};
static const SubGhzProtocolEncoder subghz_protocol_faac_tm_encoder = {
    .alloc = subghz_protocol_encoder_faac_tm_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_faac_tm_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_common_yield,
};
const SubGhzProtocol subghz_protocol_faac_tm = {
    .name = SUBGHZ_PROTOCOL_FAAC_TM_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_868 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_faac_tm_decoder,
    .encoder = &subghz_protocol_faac_tm_encoder,
};

void* subghz_protocol_encoder_faac_tm_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderFaacTm* instance = subghz_protocol_encoder_common_alloc(
        sizeof(SubGhzProtocolEncoderFaacTm), &subghz_protocol_faac_tm, 3, 26);
    instance->te = 400;
    return instance;
}

static bool subghz_protocol_encoder_faac_tm_get_upload(SubGhzProtocolEncoderFaacTm* instance) {
    furi_assert(instance);
    if(instance->generic.data_count_bit != subghz_protocol_faac_tm_const.min_count_bit_for_found ||
       instance->generic.data > 0xFFF || instance->te <= FAAC_TM_TE_MIN ||
       instance->te >= FAAC_TM_TE_MAX || instance->encoder.size_upload < 26)
        return false;
    size_t index = 0;
    instance->encoder.upload[index++] =
        level_duration_make(false, instance->te * FAAC_TM_HEADER_TE);
    instance->encoder.upload[index++] = level_duration_make(true, instance->te);
    for(uint8_t bit = 12; bit > 0; bit--) {
        bool value = bit_read(instance->generic.data, bit - 1);
        instance->encoder.upload[index++] =
            level_duration_make(false, instance->te * (value ? 1 : 2));
        instance->encoder.upload[index++] =
            level_duration_make(true, instance->te * (value ? 2 : 1));
    }
    instance->encoder.size_upload = index;
    return true;
}

static SubGhzProtocolStatus subghz_protocol_faac_tm_read_key(
    SubGhzBlockGeneric* generic,
    FlipperFormat* format,
    uint32_t* te) {
    *te = 400;
    SubGhzProtocolStatus result =
        subghz_block_generic_deserialize_check_count_bit(generic, format, 12);
    if(result != SubGhzProtocolStatusOk) return result;
    if(generic->data_count_bit != 12 || generic->data > 0xFFF) return SubGhzProtocolStatusError;
    if(!flipper_format_rewind(format)) return SubGhzProtocolStatusErrorParserOthers;
    uint32_t value = 0;
    if(flipper_format_read_uint32(format, "TE", &value, 1) && value > FAAC_TM_TE_MIN &&
       value < FAAC_TM_TE_MAX)
        *te = value;
    return SubGhzProtocolStatusOk;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_faac_tm_deserialize(void* context, FlipperFormat* format) {
    furi_assert(context);
    SubGhzProtocolEncoderFaacTm* instance = context;
    instance->encoder.is_running = false;
    instance->encoder.front = 0;
    instance->encoder.repeat = 3;
    SubGhzProtocolStatus result =
        subghz_protocol_faac_tm_read_key(&instance->generic, format, &instance->te);
    if(result != SubGhzProtocolStatusOk) return result;
    if(!flipper_format_rewind(format)) return SubGhzProtocolStatusErrorParserOthers;
    uint32_t repeat = 3;
    if(flipper_format_read_uint32(format, "Repeat", &repeat, 1)) instance->encoder.repeat = repeat;
    if(!subghz_protocol_encoder_faac_tm_get_upload(instance))
        return SubGhzProtocolStatusErrorEncoderGetUpload;
    instance->encoder.is_running = true;
    return SubGhzProtocolStatusOk;
}

void* subghz_protocol_decoder_faac_tm_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderFaacTm* instance = subghz_protocol_decoder_common_alloc(
        sizeof(SubGhzProtocolDecoderFaacTm), &subghz_protocol_faac_tm);
    instance->te = 400;
    subghz_protocol_decoder_faac_tm_reset(instance);
    return instance;
}

void subghz_protocol_decoder_faac_tm_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderFaacTm* instance = context;
    instance->decoder.parser_step = FaacTmDecoderStepReset;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
    instance->te_sum = 0;
    instance->last_data = 0;
    instance->have_last = false;
}

void subghz_protocol_decoder_faac_tm_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderFaacTm* instance = context;
    switch(instance->decoder.parser_step) {
    case FaacTmDecoderStepReset:
        if(!level && DURATION_DIFF(duration, 400U * FAAC_TM_HEADER_TE) < 120U * FAAC_TM_HEADER_TE)
            instance->decoder.parser_step = FaacTmDecoderStepFoundStartBit;
        break;
    case FaacTmDecoderStepFoundStartBit:
        if(!level) break;
        if(DURATION_DIFF(duration, 400U) < 120U) {
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->te_sum = duration;
            instance->decoder.parser_step = FaacTmDecoderStepSaveDuration;
        } else
            subghz_protocol_decoder_faac_tm_reset(instance);
        break;
    case FaacTmDecoderStepSaveDuration:
        if(level) {
            subghz_protocol_decoder_faac_tm_reset(instance);
            break;
        }
        if(duration >= 1600U) {
            const uint32_t te = instance->te_sum / 37U;
            const bool framed = instance->decoder.decode_count_bit == 12 && te > 0;
            const bool sync = framed && duration >= te * 28U && duration < te * 42U;
            const bool idle = framed && duration >= te * 70U;
            if((sync || idle) && instance->have_last &&
               instance->last_data == instance->decoder.decode_data) {
                instance->generic.data = instance->decoder.decode_data;
                instance->generic.data_count_bit = 12;
                instance->generic.serial = 0;
                instance->generic.btn = 0;
                instance->generic.cnt = 0;
                instance->te = te;
                if(instance->base.callback)
                    instance->base.callback(&instance->base, instance->base.context);
            }
            instance->have_last = sync;
            instance->last_data = instance->decoder.decode_data;
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->decoder.parser_step = FaacTmDecoderStepFoundStartBit;
            break;
        }
        instance->decoder.te_last = duration;
        instance->decoder.parser_step = FaacTmDecoderStepCheckDuration;
        break;
    case FaacTmDecoderStepCheckDuration:
        if(!level || instance->decoder.decode_count_bit >= 12) {
            subghz_protocol_decoder_faac_tm_reset(instance);
            break;
        }
        if(DURATION_DIFF(instance->decoder.te_last, 400U) < 120U &&
           DURATION_DIFF(duration, 800U) < 120U) {
            subghz_protocol_blocks_add_bit(&instance->decoder, 1);
        } else if(
            DURATION_DIFF(instance->decoder.te_last, 800U) < 120U &&
            DURATION_DIFF(duration, 400U) < 120U) {
            subghz_protocol_blocks_add_bit(&instance->decoder, 0);
        } else {
            subghz_protocol_decoder_faac_tm_reset(instance);
            break;
        }
        instance->te_sum += instance->decoder.te_last + duration;
        instance->decoder.parser_step = FaacTmDecoderStepSaveDuration;
        break;
    }
}

SubGhzProtocolStatus
    subghz_protocol_decoder_faac_tm_deserialize(void* context, FlipperFormat* format) {
    furi_assert(context);
    SubGhzProtocolDecoderFaacTm* instance = context;
    subghz_protocol_decoder_faac_tm_reset(instance);
    return subghz_protocol_faac_tm_read_key(&instance->generic, format, &instance->te);
}

void subghz_protocol_decoder_faac_tm_get_string(void* context, FuriString* output) {
    furi_assert(context);
    SubGhzProtocolDecoderFaacTm* instance = context;
    furi_string_cat_printf(
        output,
        "%s %dbit\r\nKey:0x%03lX Te:%luus\r\nDIP:",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (uint32_t)instance->generic.data,
        instance->te);
    for(uint16_t mask = 0x800; mask; mask >>= 1)
        furi_string_push_back(output, instance->generic.data & mask ? '1' : '0');
    furi_string_cat_str(output, "\r\n");
}
