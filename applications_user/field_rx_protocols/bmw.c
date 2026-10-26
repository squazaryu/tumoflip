/* Receive-only GPL-3.0 adaptation of D4C1-Labs/Flipper-ARF
 * 9b18419de2774a9693f91a3631653d1538f0f91a. See PROVENANCE.md.
 * Encoder implementations are intentionally not imported. */
#include "bmw.h"

#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

#include <lib/subghz/blocks/custom_btn_i.h>

#define BMW_TE_US             62
#define BMW_TE1_MIN_US        40
#define BMW_TE1_MAX_US        90
#define BMW_TE2_MIN_US        100
#define BMW_TE2_MAX_US        160
#define BMW_GAP_US            400
#define BMW_PREAMBLE_MIN      32
#define BMW_PREAMBLE_MAX      280
#define BMW_PREAMBLE_BITS     212
#define BMW_SYNC              0x599EU
#define BMW_SYNC_INV          0xA661U
#define BMW_HEADER_HUNT_MAX   40
#define BMW_PAYLOAD_BITS      128
#define BMW_FIXED_VALUE       0x89U
#define BMW_CNT_BITS          17
#define BMW_CNT_MASK          0x1FFFFU
#define BMW_HOP_MASK          0x7FFFFFFFFFFFULL
#define BMW_BTN_TEMPLATE      0x01U
#define BMW_BTN_TEMPLATE_MASK 0xE7U
#define BMW_BTN_XX_MASK       0x3U
#define BMW_BTN_UNLOCK        0x1U
#define BMW_BTN_LOCK          0x2U
#define BMW_BTN_TRUNK         0x3U
#define BMW_BTN_MAX           2
#define BMW_REPEAT_GAP_US     200000
#define BMW_REPEAT_DEFAULT    3
#define BMW_UPLOAD_CAPACITY   2048

static const SubGhzBlockConst subghz_protocol_bmw_const = {
    .te_short = BMW_TE_US,
    .te_long = BMW_TE_US * 2,
    .te_delta = 25,
    .min_count_bit_for_found = BMW_PAYLOAD_BITS,
};

typedef enum {
    BMWDecoderStepReset = 0,
    BMWDecoderStepAgc,
    BMWDecoderStepWaitPreambleHigh,
    BMWDecoderStepPreambleLow,
    BMWDecoderStepPreambleHigh,
    BMWDecoderStepHeader,
    BMWDecoderStepData,
} BMWDecoderStep;

struct SubGhzProtocolDecoderBMW {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint64_t decode_head;
    uint16_t preamble_count;
    uint16_t header_bits;
    uint16_t sync_reg;
    int8_t pair_chip;
    bool invert;
};

const SubGhzProtocolDecoder subghz_protocol_bmw_decoder = {
    .alloc = subghz_protocol_decoder_bmw_alloc,
    .free = subghz_protocol_decoder_bmw_free,
    .feed = subghz_protocol_decoder_bmw_feed,
    .reset = subghz_protocol_decoder_bmw_reset,
    .get_hash_data = subghz_protocol_decoder_bmw_get_hash_data,
    .serialize = subghz_protocol_decoder_bmw_serialize,
    .deserialize = subghz_protocol_decoder_bmw_deserialize,
    .get_string = subghz_protocol_decoder_bmw_get_string,
};

const SubGhzProtocol subghz_protocol_bmw = {
    .name = SUBGHZ_PROTOCOL_BMW_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save,
    .decoder = &subghz_protocol_bmw_decoder,
    .encoder = NULL,
};

static bool bmw_is_1t(uint32_t duration) {
    return (duration >= BMW_TE1_MIN_US) && (duration <= BMW_TE1_MAX_US);
}

static bool bmw_is_2t(uint32_t duration) {
    return (duration >= BMW_TE2_MIN_US) && (duration <= BMW_TE2_MAX_US);
}

static uint8_t bmw_chips(uint32_t duration) {
    if(bmw_is_1t(duration)) return 1;
    if(bmw_is_2t(duration)) return 2;
    return 0;
}

static void bmw_normalize_stored_payload(SubGhzBlockGeneric* instance) {
    if((uint16_t)(instance->data >> 48) == BMW_SYNC_INV) {
        instance->data = ~instance->data;
        instance->data_2 = ~instance->data_2;
    }
}

static uint8_t bmw_wire_xx(uint8_t btn_byte) {
    return (uint8_t)((btn_byte >> 3) & BMW_BTN_XX_MASK);
}

static const char* bmw_btn_name(uint8_t btn) {
    switch(btn) {
    case BMW_BTN_UNLOCK:
        return "Unlock";
    case BMW_BTN_LOCK:
        return "Lock";
    case BMW_BTN_TRUNK:
        return "Trunk";
    default:
        return "?";
    }
}

static uint32_t bmw_wire_cnt(uint64_t lo) {
    return (uint32_t)((((lo >> 48) & 0xFFFFU) << 1) | ((lo >> 47) & 1U));
}

static void bmw_parse_fields(SubGhzBlockGeneric* instance) {
    const uint64_t hi = instance->data;
    const uint64_t lo = instance->data_2;
    const uint8_t btn_byte = (uint8_t)((hi >> 8) & 0xFFU);

    instance->serial = (uint32_t)(hi >> 16);
    instance->btn = bmw_wire_xx(btn_byte);
    instance->cnt = bmw_wire_cnt(lo) & BMW_CNT_MASK;
    /* [ARF_PORT] Upstream also caches three values in SubGhzBlockGeneric fields
     * that ARF's generic block does not have:
     *   .validation_field = hi & 0xFF  -> write-only upstream, and
     *                                     bmw_fields_valid() recomputes it,
     *   .te               = BMW_TE_US  -> write-only (a compile-time constant),
     *   .data_3           = lo & BMW_HOP_MASK -> always equal to
     *                                     data_2 & BMW_HOP_MASK.
     * All three are therefore dropped; the hop is read straight from data_2. */
}

static bool bmw_fields_valid(const SubGhzBlockGeneric* instance) {
    const uint64_t hi = instance->data;
    const uint16_t sync = (uint16_t)(hi >> 48);
    const uint8_t btn_byte = (uint8_t)((hi >> 8) & 0xFFU);
    const uint8_t fixed = (uint8_t)(hi & 0xFFULL);

    if(sync != BMW_SYNC) return false;
    if((btn_byte & BMW_BTN_TEMPLATE_MASK) != BMW_BTN_TEMPLATE) return false;
    if(bmw_wire_xx(btn_byte) == 0) return false;
    if(fixed != BMW_FIXED_VALUE) return false;
    return true;
}

/* [ARF_PORT] The upstream firmware builds this mapping at runtime through
 * subghz_custom_btn_configure_map(). ARF keeps button maps static inside the
 * protocol (see vag.c) plus a label row in
 * applications/main/subghz/helpers/subghz_button_labels.c, so the same map is
 * expressed here. Codes taken verbatim from the upstream "BMW" entry. */

void* subghz_protocol_decoder_bmw_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderBMW* instance = calloc(1, sizeof(SubGhzProtocolDecoderBMW));
    instance->base.protocol = &subghz_protocol_bmw;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->pair_chip = -1;
    return instance;
}

void subghz_protocol_decoder_bmw_free(void* context) {
    furi_check(context);
    free(context);
}

static void bmw_reset_soft(SubGhzProtocolDecoderBMW* instance) {
    instance->decoder.parser_step = BMWDecoderStepReset;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
    instance->decode_head = 0;
    instance->preamble_count = 0;
    instance->header_bits = 0;
    instance->sync_reg = 0;
    instance->pair_chip = -1;
    instance->invert = false;
}

void subghz_protocol_decoder_bmw_reset(void* context) {
    furi_check(context);
    bmw_reset_soft(context);
}

static void bmw_try_callback(SubGhzProtocolDecoderBMW* instance) {
    if(instance->decoder.decode_count_bit != subghz_protocol_bmw_const.min_count_bit_for_found)
        return;
    instance->generic.data = instance->decode_head;
    instance->generic.data_2 = instance->decoder.decode_data;
    instance->generic.data_count_bit = BMW_PAYLOAD_BITS;
    if(!bmw_fields_valid(&instance->generic)) {
        bmw_reset_soft(instance);
        return;
    }
    bmw_parse_fields(&instance->generic);
    if((subghz_custom_btn_get_original() == 0)) {
        subghz_custom_btn_set_original(instance->generic.btn);
        subghz_custom_btn_set_max(BMW_BTN_MAX);
    }
    if(instance->base.callback) {
        instance->base.callback(&instance->base, instance->base.context);
    }
    bmw_reset_soft(instance);
}

static void bmw_seed_payload_sync(SubGhzProtocolDecoderBMW* instance) {
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
    instance->decode_head = 0;
    for(int8_t i = 15; i >= 0; i--) {
        subghz_protocol_blocks_add_to_128_bit(
            &instance->decoder, (BMW_SYNC >> i) & 1, &instance->decode_head);
    }
}

static void bmw_add_logical_bit(SubGhzProtocolDecoderBMW* instance, bool bit) {
    switch(instance->decoder.parser_step) {
    case BMWDecoderStepHeader:
        instance->sync_reg = (uint16_t)((instance->sync_reg << 1) | (bit ? 1U : 0U));
        instance->header_bits++;
        if(instance->sync_reg == BMW_SYNC) {
            instance->invert = false;
            bmw_seed_payload_sync(instance);
            instance->decoder.parser_step = BMWDecoderStepData;
        } else if(instance->sync_reg == BMW_SYNC_INV) {
            instance->invert = true;
            bmw_seed_payload_sync(instance);
            instance->decoder.parser_step = BMWDecoderStepData;
        } else if(instance->header_bits >= BMW_HEADER_HUNT_MAX) {
            bmw_reset_soft(instance);
        }
        break;
    case BMWDecoderStepData: {
        bool value = instance->invert ? !bit : bit;
        if(instance->decoder.decode_count_bit >=
           subghz_protocol_bmw_const.min_count_bit_for_found) {
            bmw_try_callback(instance);
            return;
        }
        subghz_protocol_blocks_add_to_128_bit(
            &instance->decoder, value ? 1 : 0, &instance->decode_head);
        if(instance->decoder.decode_count_bit ==
           subghz_protocol_bmw_const.min_count_bit_for_found) {
            bmw_try_callback(instance);
        }
        break;
    }
    default:
        bmw_reset_soft(instance);
        break;
    }
}

static void bmw_add_chip(SubGhzProtocolDecoderBMW* instance, bool chip) {
    if(instance->pair_chip < 0) {
        instance->pair_chip = chip ? 1 : 0;
        return;
    }
    const uint8_t first = (uint8_t)instance->pair_chip;
    const uint8_t second = chip ? 1 : 0;
    instance->pair_chip = -1;
    if(first == 0 && second == 1) {
        bmw_add_logical_bit(instance, true);
    } else if(first == 1 && second == 0) {
        bmw_add_logical_bit(instance, false);
    } else {
        bmw_reset_soft(instance);
    }
}

static void bmw_enter_header(SubGhzProtocolDecoderBMW* instance, bool level) {
    instance->decoder.parser_step = BMWDecoderStepHeader;
    instance->header_bits = 0;
    instance->sync_reg = 0;
    instance->invert = false;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
    instance->decode_head = 0;
    instance->pair_chip = level ? 1 : 0;
}

static void bmw_finish_gap(SubGhzProtocolDecoderBMW* instance, bool level) {
    if(instance->decoder.parser_step == BMWDecoderStepHeader ||
       instance->decoder.parser_step == BMWDecoderStepData) {
        if(instance->pair_chip == 0 && level) {
            bmw_add_logical_bit(instance, true);
        } else if(instance->pair_chip == 1 && !level) {
            bmw_add_logical_bit(instance, false);
        }
        instance->pair_chip = -1;
        bmw_try_callback(instance);
    }
    bmw_reset_soft(instance);
}

void subghz_protocol_decoder_bmw_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderBMW* instance = context;

    if(duration >= BMW_GAP_US) {
        bmw_finish_gap(instance, level);
        return;
    }

    const uint8_t n = bmw_chips(duration);
    if(n == 0) {
        bmw_reset_soft(instance);
        return;
    }

    switch(instance->decoder.parser_step) {
    case BMWDecoderStepReset:
        if(level && n == 2) {
            instance->decoder.parser_step = BMWDecoderStepAgc;
        } else if(level && n == 1) {
            instance->preamble_count = 0;
            instance->decoder.parser_step = BMWDecoderStepPreambleLow;
        } else if(!level && n == 1) {
            instance->preamble_count = 0;
            instance->decoder.parser_step = BMWDecoderStepPreambleHigh;
        }
        break;

    case BMWDecoderStepAgc:
        if(!level && (n == 1 || n == 2)) {
            instance->decoder.parser_step = BMWDecoderStepWaitPreambleHigh;
        } else if(level && n == 1) {
            instance->preamble_count = 0;
            instance->decoder.parser_step = BMWDecoderStepPreambleLow;
        } else {
            bmw_reset_soft(instance);
        }
        break;

    case BMWDecoderStepWaitPreambleHigh:
        if(level && n == 1) {
            instance->preamble_count = 0;
            instance->decoder.parser_step = BMWDecoderStepPreambleLow;
        } else if(level && n == 2) {
            instance->decoder.parser_step = BMWDecoderStepAgc;
        } else if(!level && (n == 1 || n == 2)) {
        } else {
            bmw_reset_soft(instance);
        }
        break;

    case BMWDecoderStepPreambleLow:
        if(!level && n == 1) {
            instance->preamble_count++;
            if(instance->preamble_count > BMW_PREAMBLE_MAX) {
                bmw_reset_soft(instance);
                break;
            }
            instance->decoder.parser_step = BMWDecoderStepPreambleHigh;
        } else if(!level && n == 2 && instance->preamble_count >= BMW_PREAMBLE_MIN) {
            bmw_enter_header(instance, false);
        } else {
            bmw_reset_soft(instance);
        }
        break;

    case BMWDecoderStepPreambleHigh:
        if(level && n == 1) {
            instance->decoder.parser_step = BMWDecoderStepPreambleLow;
        } else if(level && n == 2 && instance->preamble_count >= BMW_PREAMBLE_MIN) {
            bmw_enter_header(instance, true);
        } else {
            bmw_reset_soft(instance);
        }
        break;

    case BMWDecoderStepHeader:
    case BMWDecoderStepData:
        for(uint8_t i = 0; i < n; i++) {
            if(instance->decoder.parser_step != BMWDecoderStepHeader &&
               instance->decoder.parser_step != BMWDecoderStepData) {
                break;
            }
            bmw_add_chip(instance, level);
        }
        break;

    default:
        bmw_reset_soft(instance);
        break;
    }

    instance->decoder.te_last = duration;
}

uint8_t subghz_protocol_decoder_bmw_get_hash_data(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderBMW* instance = context;
    uint64_t mix = instance->generic.data ^ instance->generic.data_2;
    return (uint8_t)(mix ^ (mix >> 8) ^ (mix >> 16) ^ (mix >> 24) ^ (mix >> 32) ^ (mix >> 40));
}

SubGhzProtocolStatus subghz_protocol_decoder_bmw_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderBMW* instance = context;
    return subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
}

SubGhzProtocolStatus
    subghz_protocol_decoder_bmw_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderBMW* instance = context;
    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, BMW_PAYLOAD_BITS);
    if(ret == SubGhzProtocolStatusOk) {
        bmw_normalize_stored_payload(&instance->generic);
        bmw_parse_fields(&instance->generic);
    }
    return ret;
}

void subghz_protocol_decoder_bmw_get_string(void* context, FuriString* output) {
    furi_check(context);
    SubGhzProtocolDecoderBMW* instance = context;
    bmw_parse_fields(&instance->generic);

    subghz_block_generic_global.btn_is_available = true;
    subghz_block_generic_global.cnt_is_available = true;
    subghz_block_generic_global.btn_length_bit = 2;
    subghz_block_generic_global.cnt_length_bit = BMW_CNT_BITS;
    subghz_block_generic_global.current_btn = instance->generic.btn;
    subghz_block_generic_global.current_cnt = instance->generic.cnt;
    if((subghz_custom_btn_get_original() == 0)) {
        subghz_custom_btn_set_original(instance->generic.btn);
    }
    subghz_custom_btn_set_max(BMW_BTN_MAX);

    furi_string_cat_printf(
        output,
        "%s 128bit\r\n"
        "Sn:%08lX\r\n"
        "Btn:%s Cnt:%u\r\n"
        "Hop:%012llX",
        instance->generic.protocol_name,
        (unsigned long)instance->generic.serial,
        bmw_btn_name(instance->generic.btn),
        (unsigned int)instance->generic.cnt,
        (unsigned long long)(instance->generic.data_2 & BMW_HOP_MASK));
}
