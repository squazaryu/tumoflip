#include "superrollo.h"

#include "core/log.h"
#include "keeloq_common.h"

#include "../blocks/const.h"
#include "../blocks/custom_btn_i.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"
#include "../subghz_keystore.h"

#define TAG "SubGhzProtocolSuperrollo"

#define SUPERROLLO_MANUFACTURER_NAME "Superrollo"
#define SUPERROLLO_FRAME_BITS        67U
#define SUPERROLLO_UPLOAD_CAPACITY   160U
#define SUPERROLLO_DEFAULT_REPEAT    4U

static const SubGhzBlockConst subghz_protocol_superrollo_const = {
    .te_short = 450,
    .te_long = 900,
    .te_delta = 200,
    .min_count_bit_for_found = SUPERROLLO_FRAME_BITS,
};

struct SubGhzProtocolDecoderSuperrollo {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    uint16_t header_count;
    SubGhzKeystore* keystore;
};

struct SubGhzProtocolEncoderSuperrollo {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
    SubGhzKeystore* keystore;
};

typedef enum {
    SuperrolloDecoderStepReset = 0,
    SuperrolloDecoderStepCheckPreambula,
    SuperrolloDecoderStepCheckSyncLow,
    SuperrolloDecoderStepSaveDuration,
    SuperrolloDecoderStepCheckDuration,
} SuperrolloDecoderStep;

typedef struct {
    uint8_t crc0;
    uint8_t crc1;
} SuperrolloCrc;

const SubGhzProtocolDecoder subghz_protocol_superrollo_decoder = {
    .alloc = subghz_protocol_decoder_superrollo_alloc,
    .free = subghz_protocol_decoder_superrollo_free,
    .feed = subghz_protocol_decoder_superrollo_feed,
    .reset = subghz_protocol_decoder_superrollo_reset,
    .get_hash_data = subghz_protocol_decoder_superrollo_get_hash_data,
    .serialize = subghz_protocol_decoder_superrollo_serialize,
    .deserialize = subghz_protocol_decoder_superrollo_deserialize,
    .get_string = subghz_protocol_decoder_superrollo_get_string,
};

const SubGhzProtocolEncoder subghz_protocol_superrollo_encoder = {
    .alloc = subghz_protocol_encoder_superrollo_alloc,
    .free = subghz_protocol_encoder_superrollo_free,
    .deserialize = subghz_protocol_encoder_superrollo_deserialize,
    .stop = subghz_protocol_encoder_superrollo_stop,
    .yield = subghz_protocol_encoder_superrollo_yield,
};

const SubGhzProtocol subghz_protocol_superrollo = {
    .name = SUBGHZ_PROTOCOL_SUPERROLLO_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_superrollo_decoder,
    .encoder = &subghz_protocol_superrollo_encoder,
};

static bool subghz_protocol_superrollo_get_manufacturer_key(
    SubGhzKeystore* keystore,
    uint64_t* manufacturer_key) {
    if(!keystore || !manufacturer_key) return false;

    for
        M_EACH(manufacture_code, *subghz_keystore_get_data(keystore), SubGhzKeyArray_t) {
            if((manufacture_code->type == KEELOQ_LEARNING_NORMAL) &&
               (furi_string_cmp_str(manufacture_code->name, SUPERROLLO_MANUFACTURER_NAME) == 0)) {
                *manufacturer_key = manufacture_code->key;
                return true;
            }
        }

    FURI_LOG_E(TAG, "Superrollo key missing");
    return false;
}

static SuperrolloCrc subghz_protocol_superrollo_calculate_crc(uint64_t word0, bool vlow) {
    SuperrolloCrc crc = {0};

    for(size_t i = 0; i < 65; i++) {
        const uint8_t input = i < 64 ? bit_read(word0, i) : vlow;
        const uint8_t next_crc1 = crc.crc0 ^ input;
        const uint8_t next_crc0 = next_crc1 ^ crc.crc1;
        crc.crc0 = next_crc0 & 1U;
        crc.crc1 = next_crc1 & 1U;
    }

    return crc;
}

static bool subghz_protocol_superrollo_validate_frame(const SubGhzBlockGeneric* generic) {
    if(generic->data_count_bit != SUPERROLLO_FRAME_BITS) return false;

    const uint64_t word0 = subghz_protocol_blocks_reverse_key(generic->data, 64);
    const uint8_t word1 =
        (uint8_t)subghz_protocol_blocks_reverse_key(generic->data_2 & 0x7U, 3);
    const SuperrolloCrc crc = subghz_protocol_superrollo_calculate_crc(word0, word1 & 1U);
    const uint8_t expected = (uint8_t)((crc.crc0 << 1U) | (crc.crc1 << 2U));

    return (word1 & 0x6U) == expected;
}

static void subghz_protocol_superrollo_build_frame(
    SubGhzBlockGeneric* generic,
    uint32_t encrypted_hop,
    bool vlow) {
    uint64_t word0 = encrypted_hop;
    uint8_t word1 = vlow ? 1U : 0U;

    word0 |= ((uint64_t)generic->serial & 0x0FFFFFFFULL) << 32U;
    word0 |= ((uint64_t)generic->btn & 0xFU) << 60U;

    const SuperrolloCrc crc = subghz_protocol_superrollo_calculate_crc(word0, vlow);
    word1 |= (uint8_t)((crc.crc0 << 1U) | (crc.crc1 << 2U));

    generic->data = subghz_protocol_blocks_reverse_key(word0, 64);
    generic->data_2 = subghz_protocol_blocks_reverse_key(word1, 3);
    generic->data_count_bit = SUPERROLLO_FRAME_BITS;
}

static bool subghz_protocol_superrollo_encode_frame(
    SubGhzBlockGeneric* generic,
    SubGhzKeystore* keystore) {
    uint64_t manufacturer_key = 0;
    if(!subghz_protocol_superrollo_get_manufacturer_key(keystore, &manufacturer_key)) {
        return false;
    }

    const uint64_t derived_key = subghz_protocol_keeloq_common_normal_learning(
        generic->serial, manufacturer_key);
    const uint32_t discrimination = generic->serial & 0x0FFFU;
    const uint32_t plaintext = ((uint32_t)(generic->btn & 0xFU) << 28U) |
                               (discrimination << 16U) | (generic->cnt & 0xFFFFU);
    const uint32_t encrypted_hop =
        subghz_protocol_keeloq_common_encrypt(plaintext, derived_key);

    subghz_protocol_superrollo_build_frame(generic, encrypted_hop, true);
    return true;
}

static uint8_t subghz_protocol_superrollo_get_button_code(uint8_t original_button) {
    switch(subghz_custom_btn_get()) {
    case SUBGHZ_CUSTOM_BTN_UP:
        return 0x3;
    case SUBGHZ_CUSTOM_BTN_DOWN:
        return 0x5;
    case SUBGHZ_CUSTOM_BTN_LEFT:
        return 0x7;
    case SUBGHZ_CUSTOM_BTN_OK:
    default:
        return original_button;
    }
}

static bool subghz_protocol_superrollo_analyze_frame(
    SubGhzBlockGeneric* generic,
    SubGhzKeystore* keystore,
    bool* decrypted) {
    if(decrypted) *decrypted = false;
    if(!subghz_protocol_superrollo_validate_frame(generic)) return false;

    const uint64_t word0 = subghz_protocol_blocks_reverse_key(generic->data, 64);
    const uint32_t encrypted_hop = (uint32_t)word0;
    generic->serial = (word0 >> 32U) & 0x0FFFFFFFU;
    generic->btn = (word0 >> 60U) & 0xFU;
    generic->cnt = 0;
    generic->seed = 0;

    uint64_t manufacturer_key = 0;
    if(!subghz_protocol_superrollo_get_manufacturer_key(keystore, &manufacturer_key)) {
        return true;
    }

    const uint64_t derived_key = subghz_protocol_keeloq_common_normal_learning(
        generic->serial, manufacturer_key);
    const uint32_t plaintext =
        subghz_protocol_keeloq_common_decrypt(encrypted_hop, derived_key);
    if(((plaintext >> 16U) & 0x0FFFU) != (generic->serial & 0x0FFFU)) {
        return false;
    }

    generic->cnt = plaintext & 0xFFFFU;
    generic->seed = (plaintext >> 24U) & 0xFU;
    if(decrypted) *decrypted = true;
    subghz_custom_btn_set_original(generic->btn);
    subghz_custom_btn_set_max(3);
    return true;
}

void* subghz_protocol_encoder_superrollo_alloc(SubGhzEnvironment* environment) {
    SubGhzProtocolEncoderSuperrollo* instance = malloc(sizeof(SubGhzProtocolEncoderSuperrollo));
    furi_check(instance);
    memset(instance, 0, sizeof(SubGhzProtocolEncoderSuperrollo));

    instance->base.protocol = &subghz_protocol_superrollo;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->keystore = subghz_environment_get_keystore(environment);
    instance->encoder.repeat = SUPERROLLO_DEFAULT_REPEAT;
    instance->encoder.size_upload = SUPERROLLO_UPLOAD_CAPACITY;
    instance->encoder.upload =
        malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    furi_check(instance->encoder.upload);

    return instance;
}

void subghz_protocol_encoder_superrollo_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderSuperrollo* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

void subghz_protocol_encoder_superrollo_stop(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderSuperrollo* instance = context;
    instance->encoder.is_running = false;
}

LevelDuration subghz_protocol_encoder_superrollo_yield(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderSuperrollo* instance = context;

    if((instance->encoder.repeat == 0) || !instance->encoder.is_running) {
        instance->encoder.is_running = false;
        return level_duration_reset();
    }

    const LevelDuration result = instance->encoder.upload[instance->encoder.front];
    if(++instance->encoder.front == instance->encoder.size_upload) {
        if(!subghz_block_generic_global.endless_tx) instance->encoder.repeat--;
        instance->encoder.front = 0;
    }

    return result;
}

bool subghz_protocol_superrollo_create_data(
    void* context,
    FlipperFormat* flipper_format,
    uint32_t serial,
    uint8_t btn,
    uint16_t cnt,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    furi_assert(flipper_format);
    SubGhzProtocolEncoderSuperrollo* instance = context;

    if((btn != 0x3) && (btn != 0x5) && (btn != 0x7)) return false;

    instance->generic.serial = (serial >> 4U) & 0x0FFFFFFFU;
    instance->generic.cnt = cnt;
    instance->generic.btn = btn;
    instance->generic.seed = 1;

    if(!subghz_protocol_superrollo_encode_frame(&instance->generic, instance->keystore)) {
        return false;
    }

    SubGhzProtocolStatus status =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    uint8_t data2[sizeof(uint64_t)] = {0};
    for(size_t i = 0; i < sizeof(uint64_t); i++) {
        data2[sizeof(uint64_t) - i - 1] =
            (instance->generic.data_2 >> (i * 8U)) & 0xFFU;
    }

    if(!flipper_format_rewind(flipper_format)) {
        status = SubGhzProtocolStatusErrorParserOthers;
    } else if(
        (status == SubGhzProtocolStatusOk) &&
        !flipper_format_insert_or_update_hex(flipper_format, "Data", data2, sizeof(data2))) {
        status = SubGhzProtocolStatusErrorParserOthers;
    }

    return status == SubGhzProtocolStatusOk;
}

static bool subghz_protocol_encoder_superrollo_get_upload(
    SubGhzProtocolEncoderSuperrollo* instance,
    uint8_t original_button) {
    furi_assert(instance);

    uint64_t manufacturer_key = 0;
    if(!subghz_protocol_superrollo_get_manufacturer_key(
           instance->keystore, &manufacturer_key)) {
        return false;
    }
    UNUSED(manufacturer_key);

    uint8_t button = subghz_protocol_superrollo_get_button_code(original_button);
    if(subghz_block_generic_global_button_override_get(&button)) {
        FURI_LOG_D(TAG, "Button changed to 0x%X", button);
    }
    if((button != 0x3) && (button != 0x5) && (button != 0x7)) {
        FURI_LOG_E(TAG, "Unsupported button 0x%X", button);
        return false;
    }

    instance->generic.btn = button;
    instance->generic.cnt = (instance->generic.cnt + 1U) & 0xFFFFU;
    if(!subghz_protocol_superrollo_encode_frame(&instance->generic, instance->keystore)) {
        return false;
    }

    size_t index = 0;
    for(uint8_t i = 0; i < 9; i++) {
        instance->encoder.upload[index++] = level_duration_make(
            true, (uint32_t)subghz_protocol_superrollo_const.te_short);
        instance->encoder.upload[index++] = level_duration_make(
            false, (uint32_t)subghz_protocol_superrollo_const.te_long);
    }

    instance->encoder.upload[index++] = level_duration_make(
        true, (uint32_t)subghz_protocol_superrollo_const.te_short * 10U);
    instance->encoder.upload[index++] = level_duration_make(
        false, (uint32_t)subghz_protocol_superrollo_const.te_short * 10U);

    for(uint8_t i = 0; i < SUPERROLLO_FRAME_BITS; i++) {
        const bool bit = i < 64 ? bit_read(instance->generic.data, 63U - i) :
                                  bit_read(instance->generic.data_2, 66U - i);
        instance->encoder.upload[index++] = level_duration_make(
            true,
            bit ? (uint32_t)subghz_protocol_superrollo_const.te_short :
                  (uint32_t)subghz_protocol_superrollo_const.te_long);
        instance->encoder.upload[index++] = level_duration_make(
            false,
            bit ? (uint32_t)subghz_protocol_superrollo_const.te_long :
                  (uint32_t)subghz_protocol_superrollo_const.te_short);
    }

    furi_check(index <= instance->encoder.size_upload);
    instance->encoder.upload[index - 1U].duration +=
        (uint32_t)subghz_protocol_superrollo_const.te_short * 18U;
    instance->encoder.size_upload = index;
    instance->encoder.front = 0;
    return true;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_superrollo_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    furi_assert(flipper_format);
    SubGhzProtocolEncoderSuperrollo* instance = context;
    SubGhzProtocolStatus status = SubGhzProtocolStatusError;

    do {
        if(subghz_block_generic_deserialize(&instance->generic, flipper_format) !=
           SubGhzProtocolStatusOk) {
            break;
        }

        flipper_format_read_uint32(
            flipper_format, "Repeat", (uint32_t*)&instance->encoder.repeat, 1);
        if(!flipper_format_rewind(flipper_format)) break;

        uint8_t data2[sizeof(uint64_t)] = {0};
        if(!flipper_format_read_hex(flipper_format, "Data", data2, sizeof(data2))) break;
        instance->generic.data_2 = 0;
        for(size_t i = 0; i < sizeof(uint64_t); i++) {
            instance->generic.data_2 = (instance->generic.data_2 << 8U) | data2[i];
        }

        bool decrypted = false;
        if(!subghz_protocol_superrollo_analyze_frame(
               &instance->generic, instance->keystore, &decrypted) ||
           !decrypted) {
            FURI_LOG_E(TAG, "Invalid frame or unavailable manufacturer key");
            break;
        }

        subghz_custom_btn_set_original(instance->generic.btn);
        subghz_custom_btn_set_max(3);
        if(!subghz_protocol_encoder_superrollo_get_upload(instance, instance->generic.btn)) break;

        uint8_t key_data[sizeof(uint64_t)] = {0};
        for(size_t i = 0; i < sizeof(uint64_t); i++) {
            key_data[sizeof(uint64_t) - i - 1] =
                (instance->generic.data >> (i * 8U)) & 0xFFU;
            data2[sizeof(uint64_t) - i - 1] =
                (instance->generic.data_2 >> (i * 8U)) & 0xFFU;
        }

        if(!flipper_format_rewind(flipper_format) ||
           !flipper_format_update_hex(flipper_format, "Key", key_data, sizeof(key_data)) ||
           !flipper_format_rewind(flipper_format) ||
           !flipper_format_update_hex(flipper_format, "Data", data2, sizeof(data2))) {
            FURI_LOG_E(TAG, "Unable to persist the advanced rolling code");
            break;
        }

        instance->encoder.is_running = true;
        status = SubGhzProtocolStatusOk;
    } while(false);

    return status;
}

static void subghz_protocol_decoder_superrollo_reset_parser(
    SubGhzProtocolDecoderSuperrollo* instance,
    bool clear_frame) {
    instance->decoder.parser_step = SuperrolloDecoderStepReset;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
    instance->decoder.te_last = 0;
    instance->header_count = 0;
    if(clear_frame) {
        instance->generic.data = 0;
        instance->generic.data_2 = 0;
        instance->generic.data_count_bit = 0;
    }
}

void* subghz_protocol_decoder_superrollo_alloc(SubGhzEnvironment* environment) {
    SubGhzProtocolDecoderSuperrollo* instance = malloc(sizeof(SubGhzProtocolDecoderSuperrollo));
    furi_check(instance);
    memset(instance, 0, sizeof(SubGhzProtocolDecoderSuperrollo));
    instance->base.protocol = &subghz_protocol_superrollo;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->keystore = subghz_environment_get_keystore(environment);
    return instance;
}

void subghz_protocol_decoder_superrollo_free(void* context) {
    furi_assert(context);
    free(context);
}

void subghz_protocol_decoder_superrollo_reset(void* context) {
    furi_assert(context);
    subghz_protocol_decoder_superrollo_reset_parser(context, true);
}

static void subghz_protocol_decoder_superrollo_add_bit(
    SubGhzProtocolDecoderSuperrollo* instance,
    bool bit) {
    if(instance->decoder.decode_count_bit == 64U) {
        instance->generic.data = instance->decoder.decode_data;
        instance->decoder.decode_data = 0;
    }
    subghz_protocol_blocks_add_bit(&instance->decoder, bit);
}

static void subghz_protocol_decoder_superrollo_finish_frame(
    SubGhzProtocolDecoderSuperrollo* instance) {
    if(instance->decoder.decode_count_bit == SUPERROLLO_FRAME_BITS) {
        instance->generic.data_2 = instance->decoder.decode_data;
        instance->generic.data_count_bit = SUPERROLLO_FRAME_BITS;
        if(subghz_protocol_superrollo_validate_frame(&instance->generic) &&
           instance->base.callback) {
            instance->base.callback(&instance->base, instance->base.context);
        }
    }
    subghz_protocol_decoder_superrollo_reset_parser(instance, false);
}

void subghz_protocol_decoder_superrollo_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderSuperrollo* instance = context;
    const uint32_t te_short = subghz_protocol_superrollo_const.te_short;
    const uint32_t te_long = subghz_protocol_superrollo_const.te_long;
    const uint32_t te_delta = subghz_protocol_superrollo_const.te_delta;

    switch(instance->decoder.parser_step) {
    case SuperrolloDecoderStepReset:
        if(level && (DURATION_DIFF(duration, te_short) < te_delta)) {
            instance->header_count++;
            instance->decoder.parser_step = SuperrolloDecoderStepCheckPreambula;
        } else if(
            level && (instance->header_count >= 4U) &&
            (DURATION_DIFF(duration, te_short * 10U) < te_delta * 10U)) {
            instance->decoder.parser_step = SuperrolloDecoderStepCheckSyncLow;
        } else {
            instance->header_count = 0;
        }
        break;

    case SuperrolloDecoderStepCheckPreambula:
        if(!level && (DURATION_DIFF(duration, te_long) < te_delta)) {
            instance->decoder.parser_step = SuperrolloDecoderStepReset;
        } else {
            subghz_protocol_decoder_superrollo_reset_parser(instance, false);
        }
        break;

    case SuperrolloDecoderStepCheckSyncLow:
        if(!level && (DURATION_DIFF(duration, te_short * 10U) < te_delta * 10U)) {
            instance->decoder.parser_step = SuperrolloDecoderStepSaveDuration;
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->generic.data = 0;
            instance->generic.data_2 = 0;
            instance->generic.data_count_bit = 0;
            instance->header_count = 0;
        } else {
            subghz_protocol_decoder_superrollo_reset_parser(instance, false);
        }
        break;

    case SuperrolloDecoderStepSaveDuration:
        if(level) {
            instance->decoder.te_last = duration;
            instance->decoder.parser_step = SuperrolloDecoderStepCheckDuration;
        } else if(duration >= te_short * 18U) {
            subghz_protocol_decoder_superrollo_finish_frame(instance);
        } else {
            subghz_protocol_decoder_superrollo_reset_parser(instance, false);
        }
        break;

    case SuperrolloDecoderStepCheckDuration:
        if(level) {
            subghz_protocol_decoder_superrollo_reset_parser(instance, false);
            break;
        }

        if((DURATION_DIFF(instance->decoder.te_last, te_short) < te_delta) &&
           (DURATION_DIFF(duration, te_long) < te_delta)) {
            if(instance->decoder.decode_count_bit < SUPERROLLO_FRAME_BITS) {
                subghz_protocol_decoder_superrollo_add_bit(instance, true);
                instance->decoder.parser_step = SuperrolloDecoderStepSaveDuration;
            } else {
                subghz_protocol_decoder_superrollo_reset_parser(instance, false);
            }
        } else if(
            (DURATION_DIFF(instance->decoder.te_last, te_long) < te_delta) &&
            (DURATION_DIFF(duration, te_short) < te_delta)) {
            if(instance->decoder.decode_count_bit < SUPERROLLO_FRAME_BITS) {
                subghz_protocol_decoder_superrollo_add_bit(instance, false);
                instance->decoder.parser_step = SuperrolloDecoderStepSaveDuration;
            } else {
                subghz_protocol_decoder_superrollo_reset_parser(instance, false);
            }
        } else if(duration >= te_short * 18U) {
            if(instance->decoder.decode_count_bit == SUPERROLLO_FRAME_BITS - 1U) {
                if(DURATION_DIFF(instance->decoder.te_last, te_short) < te_delta) {
                    subghz_protocol_decoder_superrollo_add_bit(instance, true);
                } else if(DURATION_DIFF(instance->decoder.te_last, te_long) < te_delta) {
                    subghz_protocol_decoder_superrollo_add_bit(instance, false);
                }
            }
            subghz_protocol_decoder_superrollo_finish_frame(instance);
        } else {
            subghz_protocol_decoder_superrollo_reset_parser(instance, false);
        }
        break;
    }
}

static const char* subghz_protocol_superrollo_get_button_name(uint8_t btn) {
    switch(btn) {
    case 0x3:
        return "Up";
    case 0x5:
        return "Down";
    case 0x7:
        return "Stop";
    default:
        return "Unknown";
    }
}

uint8_t subghz_protocol_decoder_superrollo_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderSuperrollo* instance = context;
    uint8_t hash = (uint8_t)(instance->generic.data_2 & 0x7U);
    const uint8_t* bytes = (const uint8_t*)&instance->generic.data;
    for(size_t i = 0; i < sizeof(instance->generic.data); i++) hash ^= bytes[i];
    return hash;
}

SubGhzProtocolStatus subghz_protocol_decoder_superrollo_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderSuperrollo* instance = context;
    if(!subghz_protocol_superrollo_validate_frame(&instance->generic)) {
        return SubGhzProtocolStatusError;
    }

    SubGhzProtocolStatus status =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    uint8_t data2[sizeof(uint64_t)] = {0};
    for(size_t i = 0; i < sizeof(uint64_t); i++) {
        data2[sizeof(uint64_t) - i - 1] =
            (instance->generic.data_2 >> (i * 8U)) & 0xFFU;
    }

    if(!flipper_format_rewind(flipper_format)) {
        status = SubGhzProtocolStatusErrorParserOthers;
    } else if(
        (status == SubGhzProtocolStatusOk) &&
        !flipper_format_insert_or_update_hex(flipper_format, "Data", data2, sizeof(data2))) {
        status = SubGhzProtocolStatusErrorParserOthers;
    }
    return status;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_superrollo_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderSuperrollo* instance = context;
    SubGhzProtocolStatus status = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, SUPERROLLO_FRAME_BITS);
    if(status != SubGhzProtocolStatusOk) return status;
    if(!flipper_format_rewind(flipper_format)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    uint8_t data2[sizeof(uint64_t)] = {0};
    if(!flipper_format_read_hex(flipper_format, "Data", data2, sizeof(data2))) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    instance->generic.data_2 = 0;
    for(size_t i = 0; i < sizeof(uint64_t); i++) {
        instance->generic.data_2 = (instance->generic.data_2 << 8U) | data2[i];
    }

    return subghz_protocol_superrollo_validate_frame(&instance->generic) ?
               SubGhzProtocolStatusOk :
               SubGhzProtocolStatusError;
}

void subghz_protocol_decoder_superrollo_get_string(void* context, FuriString* output) {
    furi_assert(context);
    SubGhzProtocolDecoderSuperrollo* instance = context;
    bool decrypted = false;
    const bool valid = subghz_protocol_superrollo_analyze_frame(
        &instance->generic, instance->keystore, &decrypted);

    subghz_block_generic_global.cnt_is_available = valid && decrypted;
    subghz_block_generic_global.cnt_length_bit = 16;
    subghz_block_generic_global.current_cnt = instance->generic.cnt;
    subghz_block_generic_global.btn_is_available = valid;
    subghz_block_generic_global.current_btn = instance->generic.btn;
    subghz_block_generic_global.btn_length_bit = 4;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%0llX\r\n"
        "Sn:%08lX Btn:%01X - %s\r\n"
        "Cnt:%04lX Group:%01lX%s\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        instance->generic.data,
        instance->generic.serial << 4U,
        instance->generic.btn,
        subghz_protocol_superrollo_get_button_name(instance->generic.btn),
        instance->generic.cnt,
        instance->generic.seed,
        !valid ? " (invalid)" : (decrypted ? "" : " (key missing)"));
}
