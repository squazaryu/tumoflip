/* Proteco MOVER framing from Unleashed 57d471868b (GPL-3.0).
 * External Core module. Preserve captured bits; no unverified button remapping.
 */
#include "proteco.h"
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"
#include "common.h"

#define TAG              "SubGhzProtocolProteco"
#define PROTECO_GUARD_X2 41
#define PROTECO_TE_MIN   100
#define PROTECO_TE_MAX   1000

static const SubGhzBlockConst subghz_protocol_proteco_const = {
    .te_short = 440,
    .te_long = 880,
    .te_delta = 150,
    .min_count_bit_for_found = 24,
};

struct SubGhzProtocolDecoderProteco {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    uint32_t te;
    uint32_t te_sum;
    uint32_t guard;
    uint64_t last_data;
    bool have_last;
};
SUBGHZ_ASSERT_DECODER_TE_LAYOUT(SubGhzProtocolDecoderProteco);

struct SubGhzProtocolEncoderProteco {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
    uint32_t te;
};
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderProteco);

typedef enum {
    ProtecoDecoderStepReset,
    ProtecoDecoderStepFoundStartBit,
    ProtecoDecoderStepSaveDuration,
    ProtecoDecoderStepCheckDuration,
} ProtecoDecoderStep;

static const SubGhzProtocolDecoder subghz_protocol_proteco_decoder = {
    .alloc = subghz_protocol_decoder_proteco_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_proteco_feed,
    .reset = subghz_protocol_decoder_proteco_reset,
    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize_te,
    .deserialize = subghz_protocol_decoder_proteco_deserialize,
    .get_string = subghz_protocol_decoder_proteco_get_string,
};
static const SubGhzProtocolEncoder subghz_protocol_proteco_encoder = {
    .alloc = subghz_protocol_encoder_proteco_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_proteco_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_common_yield,
};
const SubGhzProtocol subghz_protocol_proteco = {
    .name = SUBGHZ_PROTOCOL_PROTECO_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_proteco_decoder,
    .encoder = &subghz_protocol_proteco_encoder,
};

void* subghz_protocol_encoder_proteco_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderProteco* instance = subghz_protocol_encoder_common_alloc(
        sizeof(SubGhzProtocolEncoderProteco), &subghz_protocol_proteco, 3, 50);
    instance->te = 440;
    return instance;
}

static bool subghz_protocol_encoder_proteco_get_upload(SubGhzProtocolEncoderProteco* instance) {
    furi_assert(instance);
    if(instance->generic.data_count_bit != subghz_protocol_proteco_const.min_count_bit_for_found ||
       instance->generic.data > 0xFFFFFF || instance->te <= PROTECO_TE_MIN ||
       instance->te >= PROTECO_TE_MAX || instance->encoder.size_upload < 50)
        return false;
    size_t index = 0;
    instance->encoder.upload[index++] =
        level_duration_make(false, instance->te * PROTECO_GUARD_X2 / 2);
    instance->encoder.upload[index++] = level_duration_make(true, instance->te);
    for(uint8_t bit = 24; bit > 0; bit--) {
        bool value = bit_read(instance->generic.data, bit - 1);
        instance->encoder.upload[index++] =
            level_duration_make(false, instance->te * (value ? 2 : 1));
        instance->encoder.upload[index++] =
            level_duration_make(true, instance->te * (value ? 1 : 2));
    }
    instance->encoder.size_upload = index;
    return true;
}

static SubGhzProtocolStatus subghz_protocol_proteco_read_key(
    SubGhzBlockGeneric* generic,
    FlipperFormat* format,
    uint32_t* te) {
    *te = 440;
    SubGhzProtocolStatus result =
        subghz_block_generic_deserialize_check_count_bit(generic, format, 24);
    if(result != SubGhzProtocolStatusOk) return result;
    if(generic->data_count_bit != 24 || generic->data > 0xFFFFFF) return SubGhzProtocolStatusError;
    if(!flipper_format_rewind(format)) return SubGhzProtocolStatusErrorParserOthers;
    uint32_t value = 0;
    if(flipper_format_read_uint32(format, "TE", &value, 1) && value > PROTECO_TE_MIN &&
       value < PROTECO_TE_MAX)
        *te = value;
    return SubGhzProtocolStatusOk;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_proteco_deserialize(void* context, FlipperFormat* format) {
    furi_assert(context);
    SubGhzProtocolEncoderProteco* instance = context;
    instance->encoder.is_running = false;
    instance->encoder.front = 0;
    instance->encoder.repeat = 3;
    SubGhzProtocolStatus result =
        subghz_protocol_proteco_read_key(&instance->generic, format, &instance->te);
    if(result != SubGhzProtocolStatusOk) return result;
    if(!flipper_format_rewind(format)) return SubGhzProtocolStatusErrorParserOthers;
    uint32_t repeat = 3;
    if(flipper_format_read_uint32(format, "Repeat", &repeat, 1)) instance->encoder.repeat = repeat;
    if(!subghz_protocol_encoder_proteco_get_upload(instance))
        return SubGhzProtocolStatusErrorEncoderGetUpload;
    instance->encoder.is_running = true;
    return SubGhzProtocolStatusOk;
}

void* subghz_protocol_decoder_proteco_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderProteco* instance = subghz_protocol_decoder_common_alloc(
        sizeof(SubGhzProtocolDecoderProteco), &subghz_protocol_proteco);
    instance->te = 440;
    subghz_protocol_decoder_proteco_reset(instance);
    return instance;
}

void subghz_protocol_decoder_proteco_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderProteco* instance = context;
    instance->decoder.parser_step = ProtecoDecoderStepReset;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
    instance->te_sum = 0;
    instance->guard = 0;
    instance->last_data = 0;
    instance->have_last = false;
}

void subghz_protocol_decoder_proteco_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderProteco* instance = context;
    switch(instance->decoder.parser_step) {
    case ProtecoDecoderStepReset:
        if(!level && duration >= 1760U) {
            instance->guard = duration;
            instance->decoder.parser_step = ProtecoDecoderStepFoundStartBit;
        }
        break;
    case ProtecoDecoderStepFoundStartBit:
        if(!level) break;
        if(DURATION_DIFF(duration, 440U) < 150U) {
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->te_sum = duration;
            instance->decoder.parser_step = ProtecoDecoderStepSaveDuration;
        } else
            subghz_protocol_decoder_proteco_reset(instance);
        break;
    case ProtecoDecoderStepSaveDuration:
        if(level) {
            subghz_protocol_decoder_proteco_reset(instance);
            break;
        }
        if(duration >= 1760U) {
            const uint32_t te = instance->te_sum / 73U;
            const bool framed = instance->decoder.decode_count_bit == 24 && te > 0 &&
                                instance->guard >= te * 37U / 2 && instance->guard <= te * 45U / 2;
            const bool sync = duration >= te * 37U / 2 && duration <= te * 45U / 2;
            const bool idle = duration >= te * 70U;
            if(framed && (sync || idle) && instance->have_last &&
               instance->last_data == instance->decoder.decode_data) {
                instance->generic.data = instance->decoder.decode_data;
                instance->generic.data_count_bit = 24;
                instance->generic.serial = instance->generic.data >> 2;
                instance->generic.btn = instance->generic.data & 3;
                instance->generic.cnt = 0;
                instance->te = te;
                if(instance->base.callback)
                    instance->base.callback(&instance->base, instance->base.context);
            }
            instance->have_last = framed && sync;
            instance->last_data = instance->decoder.decode_data;
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->guard = duration;
            instance->decoder.parser_step = ProtecoDecoderStepFoundStartBit;
            break;
        }
        instance->decoder.te_last = duration;
        instance->decoder.parser_step = ProtecoDecoderStepCheckDuration;
        break;
    case ProtecoDecoderStepCheckDuration:
        if(!level || instance->decoder.decode_count_bit >= 24) {
            subghz_protocol_decoder_proteco_reset(instance);
            break;
        }
        if(DURATION_DIFF(instance->decoder.te_last, 880U) < 150U &&
           DURATION_DIFF(duration, 440U) < 150U) {
            subghz_protocol_blocks_add_bit(&instance->decoder, 1);
        } else if(
            DURATION_DIFF(instance->decoder.te_last, 440U) < 150U &&
            DURATION_DIFF(duration, 880U) < 150U) {
            subghz_protocol_blocks_add_bit(&instance->decoder, 0);
        } else {
            subghz_protocol_decoder_proteco_reset(instance);
            break;
        }
        instance->te_sum += instance->decoder.te_last + duration;
        instance->decoder.parser_step = ProtecoDecoderStepSaveDuration;
        break;
    }
}

SubGhzProtocolStatus
    subghz_protocol_decoder_proteco_deserialize(void* context, FlipperFormat* format) {
    furi_assert(context);
    SubGhzProtocolDecoderProteco* instance = context;
    subghz_protocol_decoder_proteco_reset(instance);
    return subghz_protocol_proteco_read_key(&instance->generic, format, &instance->te);
}

void subghz_protocol_decoder_proteco_get_string(void* context, FuriString* output) {
    furi_assert(context);
    SubGhzProtocolDecoderProteco* instance = context;
    furi_string_cat_printf(
        output,
        "%s %dbit\r\nKey:0x%06lX\r\nTe:%luus Btn:%lu\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (uint32_t)instance->generic.data,
        instance->te,
        (uint32_t)(instance->generic.data & 3U));
}
