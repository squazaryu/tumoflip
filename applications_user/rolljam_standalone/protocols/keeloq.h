#pragma once

#include <furi.h>
#include <lib/subghz/protocols/base.h>
#include <lib/subghz/types.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <flipper_format/flipper_format.h>

#define SUBGHZ_PROTOCOL_KEELOQ_NAME "KeeLoq"
#define KEELOQ_MF_UNKNOWN           "KL Unknown"

typedef struct SubGhzProtocolDecoderKeeloq SubGhzProtocolDecoderKeeloq;
typedef struct SubGhzProtocolEncoderKeeloq SubGhzProtocolEncoderKeeloq;

extern const SubGhzProtocolDecoder subghz_protocol_keeloq_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_keeloq_encoder;
extern const SubGhzProtocol subghz_protocol_keeloq;

void* subghz_protocol_decoder_keeloq_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_keeloq_reset(void* context);
void subghz_protocol_decoder_keeloq_feed(void* context, bool level, uint32_t duration);
SubGhzProtocolStatus subghz_protocol_decoder_keeloq_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);
SubGhzProtocolStatus
    subghz_protocol_decoder_keeloq_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_decoder_keeloq_get_string(void* context, FuriString* output);
