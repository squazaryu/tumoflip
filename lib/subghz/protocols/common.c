#include "common.h"
#include "public_api.h"
#include "beninca_arc.h"
#include "came_atomo.h"
#include "nice_flor_s.h"

bool subghz_protocol_decoder_get_press_identity(
    SubGhzProtocolDecoderBase* decoder,
    uint32_t* identity) {
    furi_assert(decoder);
    furi_assert(identity);
    const SubGhzProtocol* protocol = decoder->protocol;
    if(protocol != &subghz_protocol_came_atomo && protocol != &subghz_protocol_beninca_arc &&
       protocol != &subghz_protocol_nice_flor_s)
        return false;
    const SubGhzBlockGeneric* generic = &((SubGhzProtocolDecoderCommon*)decoder)->generic;
    memset(identity, 0, SUBGHZ_PRESS_IDENTITY_WORDS * sizeof(uint32_t));
    if(protocol == &subghz_protocol_nice_flor_s ||
       (protocol == &subghz_protocol_beninca_arc && generic->seed != 0xAA55)) {
        identity[0] = (uint32_t)generic->data;
        identity[1] = (uint32_t)(generic->data >> 32);
        identity[4] = generic->data_count_bit;
        if(protocol == &subghz_protocol_nice_flor_s && generic->data_count_bit != 112) {
            // Keep the button and encrypted body, ignoring only the parcel nibble.
            identity[1] &= 0xF0FFFu;
        } else {
            identity[2] = (uint32_t)generic->data_2;
            identity[3] = (uint32_t)(generic->data_2 >> 32);
        }
    } else {
        identity[0] = generic->serial;
        identity[1] = generic->cnt;
        identity[2] = generic->btn;
    }
    return true;
}

void subghz_protocol_encoder_common_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderCommon* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

void subghz_protocol_encoder_common_stop(void* context) {
    SubGhzProtocolEncoderCommon* instance = context;
    instance->encoder.is_running = false;
}

LevelDuration subghz_protocol_encoder_common_yield(void* context) {
    SubGhzProtocolEncoderCommon* instance = context;

    if(instance->encoder.repeat == 0 || !instance->encoder.is_running) {
        instance->encoder.is_running = false;
        return level_duration_reset();
    }

    LevelDuration ret = instance->encoder.upload[instance->encoder.front];

    if(++instance->encoder.front == instance->encoder.size_upload) {
        if(!subghz_block_generic_global.endless_tx) instance->encoder.repeat--;
        instance->encoder.front = 0;
    }

    return ret;
}

void subghz_protocol_decoder_common_free(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderCommon* instance = context;
    free(instance);
}

void subghz_protocol_decoder_common_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderCommon* instance = context;
    instance->decoder.parser_step = 0;
}

uint8_t subghz_protocol_decoder_common_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderCommon* instance = context;
    return subghz_protocol_blocks_get_hash_data(
        &instance->decoder, (instance->decoder.decode_count_bit / 8) + 1);
}

uint8_t subghz_protocol_decoder_common_get_hash_data_serial(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderCommon* instance = context;
    /* The mini counter inside each parcel changes while the button is held; the serial
     * and press counter do not, and still separate one remote from another. */
    uint32_t id = instance->generic.serial ^ instance->generic.cnt ^
                  ((uint32_t)instance->generic.btn << 24);
    return (uint8_t)((id * 2654435761u) >> 24);
}

SubGhzProtocolStatus subghz_protocol_decoder_common_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderCommon* instance = context;
    return subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
}

SubGhzProtocolStatus subghz_protocol_common_append_data_2(
    SubGhzProtocolStatus status,
    SubGhzBlockGeneric* generic,
    FlipperFormat* flipper_format) {
    uint8_t key_data[sizeof(uint64_t)] = {0};
    for(size_t i = 0; i < sizeof(uint64_t); i++) {
        key_data[sizeof(uint64_t) - i - 1] = (generic->data_2 >> (i * 8)) & 0xFF;
    }

    if(!flipper_format_rewind(flipper_format)) {
        FURI_LOG_E(generic->protocol_name, "Rewind error");
        status = SubGhzProtocolStatusErrorParserOthers;
    }

    if((status == SubGhzProtocolStatusOk) &&
       !flipper_format_insert_or_update_hex(flipper_format, "Data", key_data, sizeof(uint64_t))) {
        FURI_LOG_E(generic->protocol_name, "Unable to add Data");
        status = SubGhzProtocolStatusErrorParserOthers;
    }
    return status;
}

SubGhzProtocolStatus subghz_protocol_decoder_common_serialize_data_2(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderCommon* instance = context;
    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    return subghz_protocol_common_append_data_2(ret, &instance->generic, flipper_format);
}

SubGhzProtocolStatus subghz_protocol_decoder_common_serialize_te(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderCommonTe* instance = context;
    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->common.generic, flipper_format, preset);
    if((ret == SubGhzProtocolStatusOk) &&
       !flipper_format_write_uint32(flipper_format, "TE", &instance->te, 1)) {
        FURI_LOG_E(instance->common.generic.protocol_name, "Unable to add TE");
        ret = SubGhzProtocolStatusErrorParserTe;
    }
    return ret;
}

void* subghz_protocol_decoder_common_alloc(size_t instance_size, const SubGhzProtocol* protocol) {
    SubGhzProtocolDecoderCommon* instance = malloc(instance_size);
    instance->base.protocol = protocol;
    instance->generic.protocol_name = protocol->name;
    return instance;
}

void* subghz_protocol_encoder_common_alloc(
    size_t instance_size,
    const SubGhzProtocol* protocol,
    size_t repeat,
    size_t size_upload) {
    SubGhzProtocolEncoderCommonGeneric* instance = malloc(instance_size);
    instance->common.base.protocol = protocol;
    instance->generic.protocol_name = protocol->name;
    instance->common.encoder.repeat = repeat;
    instance->common.encoder.size_upload = size_upload;
    instance->common.encoder.upload = malloc(size_upload * sizeof(LevelDuration));
    instance->common.encoder.is_running = false;
    return instance;
}

SubGhzProtocolStatus subghz_protocol_encoder_common_deserialize(
    void* context,
    FlipperFormat* flipper_format,
    uint16_t min_count_bit,
    SubGhzProtocolEncoderGetUpload get_upload) {
    furi_assert(context);
    SubGhzProtocolEncoderCommonGeneric* instance = context;
    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, min_count_bit);
    if(ret != SubGhzProtocolStatusOk) {
        return ret;
    }
    // Optional value
    flipper_format_read_uint32(
        flipper_format, "Repeat", (uint32_t*)&instance->common.encoder.repeat, 1);

    if(!get_upload(instance)) {
        return SubGhzProtocolStatusErrorEncoderGetUpload;
    }
    instance->common.encoder.is_running = true;
    return ret;
}

SubGhzProtocolStatus subghz_protocol_decoder_common_deserialize_te(
    void* context,
    FlipperFormat* flipper_format,
    uint16_t min_count_bit) {
    furi_assert(context);
    SubGhzProtocolDecoderCommonTe* instance = context;
    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        &instance->common.generic, flipper_format, min_count_bit);
    if(ret != SubGhzProtocolStatusOk) {
        return ret;
    }
    if(!flipper_format_rewind(flipper_format)) {
        FURI_LOG_E(instance->common.generic.protocol_name, "Rewind error");
        return SubGhzProtocolStatusErrorParserOthers;
    }
    if(!flipper_format_read_uint32(flipper_format, "TE", (uint32_t*)&instance->te, 1)) {
        FURI_LOG_E(instance->common.generic.protocol_name, "Missing TE");
        return SubGhzProtocolStatusErrorParserTe;
    }
    return ret;
}
