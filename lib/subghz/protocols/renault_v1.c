/* Receive-only adaptation of ProtoPirate Renault V1 (GPL-3.0).
 * Source: all-the-plugins 3e6a9ca199cdb4664c1998952deedc8533c420d5.
 * Preserves the observed 16-bit header and 88-bit payload. No transmitter.
 */
#include "renault_v1.h"
#include "common.h"
#include <lib/toolbox/manchester_decoder.h>
#include <stdlib.h>
#include <string.h>

#define RENAULT_V1_BITS       88U
#define RENAULT_V1_RAW_BYTES  11U
#define RENAULT_V1_FRAME_BITS 104U

typedef enum {
    RenaultV1Reset,
    RenaultV1Sync,
    RenaultV1Data
} RenaultV1Step;

struct SubGhzProtocolDecoderRenaultV1 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    ManchesterState manchester;
    uint16_t te_high, te_low, header;
    uint64_t pending_data;
    uint8_t raw[RENAULT_V1_RAW_BYTES];
    uint32_t hop;
    bool frame_valid, last_valid;
};

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderRenaultV1);

static void renault_v1_free(void* context) {
    free(context);
}

static const SubGhzProtocolDecoder renault_v1_decoder = {
    .alloc = subghz_protocol_decoder_renault_v1_alloc,
    .free = renault_v1_free,
    .feed = subghz_protocol_decoder_renault_v1_feed,
    .reset = subghz_protocol_decoder_renault_v1_reset,
    .get_hash_data = subghz_protocol_decoder_renault_v1_get_hash_data,
    .serialize = subghz_protocol_decoder_renault_v1_serialize,
    .deserialize = subghz_protocol_decoder_renault_v1_deserialize,
    .get_string = subghz_protocol_decoder_renault_v1_get_string,
};

const SubGhzProtocol subghz_protocol_renault_v1 = {
    .name = RENAULT_V1_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_868 |
            SubGhzProtocolFlag_AM | SubGhzProtocolFlag_FM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save,
    .decoder = &renault_v1_decoder,
    .encoder = NULL,
};

static bool renault_v1_valid(const uint8_t raw[RENAULT_V1_RAW_BYTES]) {
    uint8_t checksum = 0;
    for(size_t i = 0; i < RENAULT_V1_RAW_BYTES - 1U; i++)
        checksum ^= raw[i];
    return checksum == raw[RENAULT_V1_RAW_BYTES - 1U];
}

static void renault_v1_apply(SubGhzProtocolDecoderRenaultV1* instance, const uint8_t raw[11]) {
    memcpy(instance->raw, raw, sizeof(instance->raw));
    instance->generic.data = 0;
    for(size_t i = 0; i < 8; i++)
        instance->generic.data = (instance->generic.data << 8) | raw[i];
    instance->generic.data_count_bit = RENAULT_V1_BITS;
    instance->generic.serial = (uint32_t)(instance->generic.data >> 32);
    instance->generic.btn = raw[4] >> 4;
    instance->generic.cnt = ((uint16_t)(raw[4] & 0x0f) << 6) | (raw[5] >> 2);
    instance->hop = ((uint32_t)(raw[5] & 3) << 30) | ((uint32_t)raw[6] << 22) |
                    ((uint32_t)raw[7] << 14) | ((uint32_t)raw[8] << 6) | (raw[9] >> 2);
    instance->frame_valid = true;
}

void* subghz_protocol_decoder_renault_v1_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderRenaultV1* instance = calloc(1, sizeof(*instance));
    if(!instance) return NULL;
    instance->base.protocol = &subghz_protocol_renault_v1;
    instance->generic.protocol_name = RENAULT_V1_PROTOCOL_NAME;
    return instance;
}

void subghz_protocol_decoder_renault_v1_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderRenaultV1* instance = context;
    memset(&instance->decoder, 0, sizeof(instance->decoder));
    instance->manchester = ManchesterStateStart1;
    instance->header = 0;
    instance->pending_data = 0;
    instance->last_valid = false;
}

static bool renault_v1_header_low(uint32_t duration) {
    return duration >= 1150U && duration <= 2200U;
}

static uint32_t renault_v1_threshold(uint16_t te) {
    uint32_t triple = (uint32_t)te * 3;
    return triple < 300U ? 150U : (triple >= 422U ? 210U : triple / 2);
}

static uint16_t renault_v1_adapt(uint16_t te, uint32_t duration) {
    uint32_t mixed = (uint32_t)te * 7 + duration;
    return mixed < 560U ? 70U : (mixed >= 1488U ? 185U : (uint16_t)(mixed / 8));
}

void subghz_protocol_decoder_renault_v1_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderRenaultV1* instance = context;
    if(instance->decoder.parser_step == RenaultV1Data && duration > 520U) {
        instance->decoder.parser_step = RenaultV1Reset;
    }
    switch(instance->decoder.parser_step) {
    case RenaultV1Reset:
        if(!level && renault_v1_header_low(duration)) {
            instance->decoder.parser_step = RenaultV1Sync;
        }
        break;
    case RenaultV1Sync:
        if(level && duration >= 800U && duration <= 1150U) {
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->header = 0;
            instance->pending_data = 0;
            instance->te_high = 120U;
            instance->te_low = 150U;
            instance->manchester = ManchesterStateStart1;
            instance->decoder.parser_step = RenaultV1Data;
        } else {
            instance->decoder.parser_step =
                !level && renault_v1_header_low(duration) ? RenaultV1Sync : RenaultV1Reset;
        }
        break;
    case RenaultV1Data: {
        if(duration <= 49U) break;
        uint16_t* te = level ? &instance->te_high : &instance->te_low;
        ManchesterEvent event;
        if(duration > renault_v1_threshold(*te)) {
            event = level ? ManchesterEventLongHigh : ManchesterEventLongLow;
        } else {
            event = level ? ManchesterEventShortHigh : ManchesterEventShortLow;
            *te = renault_v1_adapt(*te, duration);
        }
        bool bit;
        if(!manchester_advance(instance->manchester, event, &instance->manchester, &bit)) {
            // Valid half-symbol transitions end at Start0/Start1; Mid1 here is
            // the decoder's invalid-transition reset, so abandon this frame.
            if(instance->manchester == ManchesterStateMid1)
                instance->decoder.parser_step = RenaultV1Reset;
            break;
        }
        instance->decoder.decode_data = (instance->decoder.decode_data << 1) | (bit ? 1U : 0U);
        instance->decoder.decode_count_bit++;
        if(instance->decoder.decode_count_bit == 16U) {
            instance->header = (uint16_t)~instance->decoder.decode_data;
            if(instance->header != 1U) instance->decoder.parser_step = RenaultV1Reset;
            instance->decoder.decode_data = 0;
        } else if(instance->decoder.decode_count_bit == 80U) {
            instance->pending_data = ~instance->decoder.decode_data;
            instance->decoder.decode_data = 0;
        } else if(instance->decoder.decode_count_bit == RENAULT_V1_FRAME_BITS) {
            uint8_t raw[RENAULT_V1_RAW_BYTES];
            for(size_t i = 0; i < 8; i++)
                raw[i] = instance->pending_data >> (56U - i * 8);
            uint32_t tail = (uint32_t)~instance->decoder.decode_data & 0xffffffU;
            raw[8] = tail >> 16;
            raw[9] = tail >> 8;
            raw[10] = tail;
            if(renault_v1_valid(raw) &&
               (!instance->last_valid || memcmp(raw, instance->raw, sizeof(raw)))) {
                renault_v1_apply(instance, raw);
                instance->last_valid = true;
                if(instance->base.callback)
                    instance->base.callback(&instance->base, instance->base.context);
            }
            instance->decoder.parser_step = RenaultV1Reset;
        }
        break;
    }
    default:
        instance->decoder.parser_step = RenaultV1Reset;
        break;
    }
}

uint8_t subghz_protocol_decoder_renault_v1_get_hash_data(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderRenaultV1* instance = context;
    uint8_t hash = 0;
    for(size_t i = 0; i < sizeof(instance->raw); i++)
        hash = (uint8_t)((hash * 33U) ^ instance->raw[i]);
    return hash;
}

static bool renault_v1_write_u32(FlipperFormat* format, const char* key, uint32_t value) {
    return flipper_format_rewind(format) &&
           flipper_format_insert_or_update_uint32(format, key, &value, 1U);
}

SubGhzProtocolStatus subghz_protocol_decoder_renault_v1_serialize(
    void* context,
    FlipperFormat* format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderRenaultV1* instance = context;
    if(!instance->frame_valid) return SubGhzProtocolStatusErrorParserOthers;
    SubGhzProtocolStatus status =
        subghz_block_generic_serialize(&instance->generic, format, preset);
    if(status != SubGhzProtocolStatusOk) return status;
    uint8_t key2[8] = {0};
    memcpy(key2 + 5, instance->raw + 8, 3);
    if(!renault_v1_write_u32(format, "Header", 1U) ||
       !renault_v1_write_u32(format, "Serial", instance->generic.serial) ||
       !renault_v1_write_u32(format, "Btn", instance->generic.btn) ||
       !renault_v1_write_u32(format, "Cnt", instance->generic.cnt) ||
       !renault_v1_write_u32(format, "Hop", instance->hop) || !flipper_format_rewind(format) ||
       !flipper_format_insert_or_update_hex(format, "Key2", key2, sizeof(key2)) ||
       !flipper_format_rewind(format) ||
       !flipper_format_insert_or_update_hex(format, "Raw", instance->raw, sizeof(instance->raw))) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    return SubGhzProtocolStatusOk;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_renault_v1_deserialize(void* context, FlipperFormat* format) {
    furi_check(context);
    SubGhzProtocolDecoderRenaultV1* instance = context;
    SubGhzBlockGeneric candidate = instance->generic;
    SubGhzProtocolStatus status =
        subghz_block_generic_deserialize_check_count_bit(&candidate, format, RENAULT_V1_BITS);
    if(status != SubGhzProtocolStatusOk) return status;
    if(candidate.data_count_bit != RENAULT_V1_BITS) return SubGhzProtocolStatusErrorValueBitCount;
    uint32_t header = 1U, count = 0;
    if(!flipper_format_rewind(format)) return SubGhzProtocolStatusErrorParserOthers;
    if(flipper_format_read_uint32(format, "Header", &header, 1U) && header != 1U)
        return SubGhzProtocolStatusErrorParserOthers;
    uint8_t raw[RENAULT_V1_RAW_BYTES];
    if(!flipper_format_rewind(format)) return SubGhzProtocolStatusErrorParserOthers;
    if(flipper_format_get_value_count(format, "Raw", &count)) {
        if(count != sizeof(raw) || !flipper_format_rewind(format) ||
           !flipper_format_read_hex(format, "Raw", raw, sizeof(raw)))
            return SubGhzProtocolStatusErrorParserOthers;
    } else {
        const char* fields[] = {"Key2", "Key_2"};
        bool found = false;
        uint8_t key2[8] = {0};
        for(size_t i = 0; i < COUNT_OF(fields); i++) {
            if(!flipper_format_rewind(format)) return SubGhzProtocolStatusErrorParserOthers;
            if(!flipper_format_get_value_count(format, fields[i], &count)) continue;
            if(count != 3U && count != 8U) return SubGhzProtocolStatusErrorParserOthers;
            if(!flipper_format_rewind(format) ||
               !flipper_format_read_hex(format, fields[i], key2 + 8U - count, count))
                return SubGhzProtocolStatusErrorParserOthers;
            found = true;
            break;
        }
        if(!found || key2[0] || key2[1] || key2[2] || key2[3] || key2[4])
            return SubGhzProtocolStatusErrorParserOthers;
        for(size_t i = 0; i < 8; i++)
            raw[i] = candidate.data >> (56U - i * 8);
        memcpy(raw + 8, key2 + 5, 3);
    }
    if(!renault_v1_valid(raw)) return SubGhzProtocolStatusErrorParserOthers;
    instance->generic = candidate;
    renault_v1_apply(instance, raw);
    instance->last_valid = false;
    return SubGhzProtocolStatusOk;
}

void subghz_protocol_decoder_renault_v1_get_string(void* context, FuriString* output) {
    furi_check(context);
    SubGhzProtocolDecoderRenaultV1* instance = context;
    static const char* const buttons[] = {
        "Sync", "Lock", "Unlock", "?", "Trunk", "?", "?", "?", "Panic"};
    const char* button =
        instance->generic.btn < COUNT_OF(buttons) ? buttons[instance->generic.btn] : "?";
    furi_string_printf(
        output,
        "%s 88bit\r\nSn:%08lX\r\nBtn:%02X %s\r\nCnt:%03lX\r\nHop:%08lX\r\nChecksum: OK",
        instance->generic.protocol_name,
        (unsigned long)instance->generic.serial,
        instance->generic.btn,
        button,
        (unsigned long)instance->generic.cnt,
        (unsigned long)instance->hop);
}
