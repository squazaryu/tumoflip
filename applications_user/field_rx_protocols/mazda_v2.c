/* Receive-only GPL-3.0 adaptation of D4C1-Labs/Flipper-ARF
 * 9b18419de2774a9693f91a3631653d1538f0f91a. See PROVENANCE.md.
 * Encoder implementations are intentionally not imported. */
#include "mazda_v2.h"

#include <lib/subghz/protocols/common.h>

#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>

#include <furi_hal.h>
#include <string.h>

#define MAZDA_V2_TE_SHORT         500U
#define MAZDA_V2_TE_LONG          1000U
#define MAZDA_V2_TE_GAP           3000U
#define MAZDA_V2_DUR_FILTER       0xF9U
#define MAZDA_V2_DUR_SHORT        0x2EDU
#define MAZDA_V2_DUR_LONG         0x5DBU
#define MAZDA_V2_SAMPLE_CAP       512U
#define MAZDA_V2_SAMPLE_PARSE_MIN 0x1FCU
#define MAZDA_V2_PREAMBLE_PAIRS   180U
#define MAZDA_V2_SYNC             0xECU
#define MAZDA_V2_PAYLOAD_BITS     71U
#define MAZDA_V2_DEBOUNCE_TICKS   0x95U
#define MAZDA_V2_UPLOAD_CAPACITY  0x280U
#define MAZDA_V2_BTN_LOCK         0x04U
#define MAZDA_V2_BTN_UNLOCK       0x08U
#define MAZDA_V2_BTN_TRUNK        0x10U
#define MAZDA_V2_BTN_LOCK_ALT     0x06U
#define MAZDA_V2_BTN_UNLOCK_ALT   0x0AU
#define MAZDA_V2_BTN_TRUNK_ALT    0x0EU
#define MAZDA_V2_BTN_MAX          3U

static const SubGhzBlockConst subghz_protocol_mazda_v2_const = {
    .te_short = MAZDA_V2_TE_SHORT,
    .te_long = MAZDA_V2_TE_LONG,
    .te_delta = 250,
    .min_count_bit_for_found = 63,
};

static const uint8_t mazda_v2_pat_a[8] = {1, 1, 1, 0, 1, 1, 0, 0};
static const uint8_t mazda_v2_pat_b[8] = {0, 0, 0, 1, 0, 0, 1, 1};
struct SubGhzProtocolDecoderMazdaV2 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t skip_flag;
    uint8_t samples[MAZDA_V2_SAMPLE_CAP];
    uint16_t sample_count;
    uint16_t short_count;
    uint64_t last_key;
    uint32_t last_tick;
};

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderMazdaV2);

const SubGhzProtocolDecoder subghz_protocol_mazda_v2_decoder = {
    .alloc = subghz_protocol_decoder_mazda_v2_alloc,
    .free = subghz_protocol_decoder_mazda_v2_free,
    .feed = subghz_protocol_decoder_mazda_v2_feed,
    .reset = subghz_protocol_decoder_mazda_v2_reset,
    .get_hash_data = subghz_protocol_decoder_mazda_v2_get_hash_data,
    .serialize = subghz_protocol_decoder_mazda_v2_serialize,
    .deserialize = subghz_protocol_decoder_mazda_v2_deserialize,
    .get_string = subghz_protocol_decoder_mazda_v2_get_string,
};

const SubGhzProtocol subghz_protocol_mazda_v2 = {
    .name = SUBGHZ_PROTOCOL_MAZDA_V2_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_FM | SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load |
            SubGhzProtocolFlag_Save,
    .decoder = &subghz_protocol_mazda_v2_decoder,
    .encoder = NULL,
};

static uint8_t mazda_v2_btn7(uint8_t button) {
    return (uint8_t)(button & 0x7FU);
}

static bool mazda_v2_button_is_valid(uint8_t button) {
    switch(mazda_v2_btn7(button)) {
    case MAZDA_V2_BTN_LOCK:
    case MAZDA_V2_BTN_UNLOCK:
    case MAZDA_V2_BTN_TRUNK:
    case MAZDA_V2_BTN_LOCK_ALT:
    case MAZDA_V2_BTN_UNLOCK_ALT:
    case MAZDA_V2_BTN_TRUNK_ALT:
        return true;
    default:
        return false;
    }
}

static const char* mazda_v2_get_button_name(uint8_t button) {
    switch(mazda_v2_btn7(button)) {
    case MAZDA_V2_BTN_LOCK:
    case MAZDA_V2_BTN_LOCK_ALT:
        return "Lock";
    case MAZDA_V2_BTN_UNLOCK:
    case MAZDA_V2_BTN_UNLOCK_ALT:
        return "Unlock";
    case MAZDA_V2_BTN_TRUNK:
    case MAZDA_V2_BTN_TRUNK_ALT:
        return "Trunk";
    default:
        return "Unknown";
    }
}

static void
    mazda_v2_extract(uint64_t data, uint8_t hop[6], uint8_t* idx, uint8_t* hdr, uint8_t* btn) {
    hop[0] = (uint8_t)((data >> 40) & 0xFFU);
    hop[1] = (uint8_t)((data >> 32) & 0xFFU);
    hop[2] = (uint8_t)((data >> 27) & 0x1FU);
    hop[3] = (uint8_t)((data >> 16) & 0x07U);
    hop[4] = (uint8_t)((data >> 8) & 0xFFU);
    hop[5] = (uint8_t)((data >> 1) & 0x7FU);
    *idx = (uint8_t)((data >> 19) & 0xFFU);
    *hdr = (uint8_t)((data >> 48) & 0xFFU);
    *btn = (uint8_t)((data >> 56) & 0xFFU);
}

static void mazda_v2_decode_key(SubGhzBlockGeneric* generic) {
    uint8_t hop[6];
    uint8_t idx;
    uint8_t hdr;
    uint8_t btn;
    mazda_v2_extract(generic->data, hop, &idx, &hdr, &btn);
    generic->btn = btn;
    generic->cnt = idx;
    generic->serial = idx;
    /* [ARF_PORT] Upstream caches the header byte in SubGhzBlockGeneric.extra
     * (serialized as "Extra"). ARF's generic block has no such field, and
     * mazda_v2_decode_key() always runs before any read, so the value is simply
     * re-derived via mazda_v2_header_from_data(). The header is still persisted
     * by this protocol itself as the "Hdr" field in serialize(). */
    generic->data_2 = generic->data & 0xFFFFFFFFFFFFULL;
    generic->data_count_bit = subghz_protocol_mazda_v2_const.min_count_bit_for_found;
}

/* [ARF_PORT] Replacement for the dropped SubGhzBlockGeneric.extra cache. */
static uint8_t mazda_v2_header_from_data(uint64_t data) {
    uint8_t hop[6];
    uint8_t idx;
    uint8_t hdr;
    uint8_t btn;
    mazda_v2_extract(data, hop, &idx, &hdr, &btn);
    return hdr;
}

static SubGhzProtocolStatus mazda_v2_write_display(
    FlipperFormat* flipper_format,
    const char* protocol_name,
    uint8_t button) {
    FuriString* display = furi_string_alloc();
    furi_string_printf(display, "%s - %s", protocol_name, mazda_v2_get_button_name(button));
    const bool ok =
        flipper_format_write_string_cstr(flipper_format, "Disp", furi_string_get_cstr(display));
    furi_string_free(display);
    return ok ? SubGhzProtocolStatusOk : SubGhzProtocolStatusErrorParserOthers;
}

static void mazda_v2_try_parse(SubGhzProtocolDecoderMazdaV2* instance) {
    const uint16_t sample_count = instance->sample_count;
    if(sample_count <= 0x46U) {
        return;
    }

    const int remain = (int)sample_count - 0x47;
    const uint8_t* samples = instance->samples;

    for(int start = 0; start <= remain; start++) {
        uint8_t invert = 1;
        uint8_t pol = 1;
        bool fail = false;
        for(uint8_t i = 0; i < 8; i++) {
            const uint8_t bit = samples[start + i];
            const uint8_t pa = mazda_v2_pat_a[i];
            const uint8_t pb = mazda_v2_pat_b[i];
            if(bit == pa) {
                if(bit == pb) {
                    if((pol == 0) && (invert == 0)) {
                        fail = true;
                        break;
                    }
                } else if(pol != 0) {
                    invert = 0;
                }
            } else if(bit == pb) {
                if(invert != 0) {
                    pol = 0;
                } else {
                    fail = true;
                    break;
                }
            } else {
                fail = true;
                break;
            }
        }
        if(fail) {
            continue;
        }
        if((pol == 0) && (invert == 0)) {
            continue;
        }
        pol ^= 1U;

        unsigned preamble = 0;
        int pos = start;
        while(preamble < (unsigned)start) {
            pos--;
            if(samples[pos] == pol) {
                preamble++;
            } else {
                break;
            }
        }
        if((int)preamble <= 0x63) {
            continue;
        }

        uint64_t value = 0;
        for(uint8_t i = 0; i < 63; i++) {
            uint8_t bit = samples[start + 8 + i];
            if(invert) {
                bit ^= 1U;
            }
            value = (value << 1) | bit;
        }
        value <<= 1;
        if((value == 0) || (value == 0xFFFFFFFF00000002ULL)) {
            continue;
        }

        const uint8_t top = (uint8_t)(value >> 56);
        if(!mazda_v2_button_is_valid(top)) {
            continue;
        }

        instance->generic.data = value;
        instance->generic.data_count_bit = subghz_protocol_mazda_v2_const.min_count_bit_for_found;
        instance->decoder.decode_data = value;
        instance->decoder.decode_count_bit =
            subghz_protocol_mazda_v2_const.min_count_bit_for_found;
        mazda_v2_decode_key(&instance->generic);

        const uint32_t now = furi_get_tick();
        const bool same = (instance->last_key == value);
        if(!(same && ((now - instance->last_tick) <= MAZDA_V2_DEBOUNCE_TICKS))) {
            if(instance->base.callback) {
                instance->base.callback(&instance->base, instance->base.context);
            }
        }
        instance->last_key = value;
        instance->last_tick = now;

        const int consumed = start + 0x47;
        const int leftover = (int)sample_count - consumed;
        if(leftover > 0) {
            memmove(instance->samples, instance->samples + consumed, (size_t)leftover);
            instance->sample_count = (uint16_t)leftover;
        } else {
            instance->sample_count = 0;
        }
        return;
    }
}

static void mazda_v2_after_store(SubGhzProtocolDecoderMazdaV2* instance) {
    instance->skip_flag = 0;
    instance->short_count++;
    if((instance->short_count > 9U) && (instance->sample_count > 0x6EU)) {
        instance->short_count = 0;
        mazda_v2_try_parse(instance);
    }
    if(instance->sample_count >= MAZDA_V2_SAMPLE_PARSE_MIN) {
        mazda_v2_try_parse(instance);
        if(instance->sample_count >= MAZDA_V2_SAMPLE_PARSE_MIN) {
            instance->decoder.parser_step = 0;
            instance->sample_count = 0;
            instance->skip_flag = 0;
        }
    }
}

static void mazda_v2_store_sample(SubGhzProtocolDecoderMazdaV2* instance, bool level) {
    if(instance->sample_count < MAZDA_V2_SAMPLE_CAP) {
        instance->samples[instance->sample_count] = level ? 1 : 0;
        instance->sample_count++;
    }
    mazda_v2_after_store(instance);
}

void* subghz_protocol_decoder_mazda_v2_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderMazdaV2* instance = calloc(1, sizeof(SubGhzProtocolDecoderMazdaV2));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_mazda_v2;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_mazda_v2_free(void* context) {
    furi_check(context);
    free(context);
}

uint8_t subghz_protocol_decoder_mazda_v2_get_hash_data(void* context) {
    SubGhzProtocolDecoderMazdaV2* instance = context;
    uint64_t value = instance->generic.data ^ instance->generic.data_2;
    value ^= value >> 32;
    value ^= value >> 16;
    value ^= value >> 8;
    return (uint8_t)value;
}

void subghz_protocol_decoder_mazda_v2_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderMazdaV2* instance = context;
    instance->decoder.parser_step = 0;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
    instance->skip_flag = 0;
    instance->sample_count = 0;
    instance->short_count = 0;
    instance->last_key = 0;
    instance->last_tick = 0;
}

void subghz_protocol_decoder_mazda_v2_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderMazdaV2* instance = context;

    if(duration <= MAZDA_V2_DUR_FILTER) {
        return;
    }

    if(instance->decoder.parser_step == 0) {
        if(duration > MAZDA_V2_DUR_SHORT) {
            return;
        }
        instance->decoder.parser_step = 1;
        instance->sample_count = 0;
        instance->short_count = 0;
        instance->skip_flag = 0;
        return;
    }

    if(instance->decoder.parser_step != 1) {
        instance->decoder.parser_step = 0;
        return;
    }

    if(duration <= MAZDA_V2_DUR_SHORT) {
        if(instance->skip_flag != 0) {
            mazda_v2_store_sample(instance, level);
            return;
        }
        instance->skip_flag = 1;
        instance->short_count++;
        if((instance->short_count > 9U) && (instance->sample_count > 0x6EU)) {
            instance->short_count = 0;
            mazda_v2_try_parse(instance);
        }
        if(instance->sample_count >= MAZDA_V2_SAMPLE_PARSE_MIN) {
            mazda_v2_try_parse(instance);
            if(instance->sample_count >= MAZDA_V2_SAMPLE_PARSE_MIN) {
                instance->decoder.parser_step = 0;
                instance->sample_count = 0;
                instance->skip_flag = 0;
            }
        }
        return;
    }

    if(duration <= MAZDA_V2_DUR_LONG) {
        mazda_v2_store_sample(instance, level);
        return;
    }

    if(instance->sample_count > 0x46U) {
        mazda_v2_try_parse(instance);
    }
    instance->decoder.parser_step = 0;
    instance->sample_count = 0;
    instance->skip_flag = 0;
}

SubGhzProtocolStatus subghz_protocol_decoder_mazda_v2_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderMazdaV2* instance = context;

    mazda_v2_decode_key(&instance->generic);
    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret == SubGhzProtocolStatusOk) {
        uint32_t hdr = mazda_v2_header_from_data(instance->generic.data);
        flipper_format_write_uint32(flipper_format, "Hdr", &hdr, 1);
        ret = mazda_v2_write_display(
            flipper_format, instance->generic.protocol_name, instance->generic.btn);
    }
    return ret;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_mazda_v2_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderMazdaV2* instance = context;

    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_mazda_v2_const.min_count_bit_for_found);
    if(ret == SubGhzProtocolStatusOk) {
        mazda_v2_decode_key(&instance->generic);
        instance->decoder.decode_data = instance->generic.data;
        instance->decoder.decode_count_bit = instance->generic.data_count_bit;
    }
    return ret;
}

void subghz_protocol_decoder_mazda_v2_get_string(void* context, FuriString* output) {
    furi_check(context);
    SubGhzProtocolDecoderMazdaV2* instance = context;

    mazda_v2_decode_key(&instance->generic);

    subghz_block_generic_global.btn_is_available = true;
    subghz_block_generic_global.cnt_is_available = true;
    subghz_block_generic_global.btn_length_bit = 8;
    subghz_block_generic_global.cnt_length_bit = 8;
    subghz_block_generic_global.current_btn = instance->generic.btn;
    subghz_block_generic_global.current_cnt = instance->generic.cnt;
    if((subghz_custom_btn_get_original() == 0)) {
        subghz_custom_btn_set_original((uint8_t)(instance->generic.btn & 0x7FU));
    }
    subghz_custom_btn_set_max(MAZDA_V2_BTN_MAX);

    furi_string_cat_printf(
        output,
        "%s %ubit\r\n"
        "Key:%016llX\r\n"
        "Btn:%02X [%s]  Hdr:%02X\r\n"
        "Cnt:%02lX \r\n"
        "Hop:%012llX",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (unsigned long long)instance->generic.data,
        instance->generic.btn,
        mazda_v2_get_button_name(instance->generic.btn),
        mazda_v2_header_from_data(instance->generic.data),
        (unsigned long)(instance->generic.cnt & 0xFFU),
        (unsigned long long)(instance->generic.data_2 & 0xFFFFFFFFFFFFULL));
}
