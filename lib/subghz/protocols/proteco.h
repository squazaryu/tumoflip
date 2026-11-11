#pragma once
#include "base.h"

#define SUBGHZ_PROTOCOL_PROTECO_NAME "Proteco"

typedef struct SubGhzProtocolDecoderProteco SubGhzProtocolDecoderProteco;
typedef struct SubGhzProtocolEncoderProteco SubGhzProtocolEncoderProteco;
extern const SubGhzProtocol subghz_protocol_proteco;

void* subghz_protocol_encoder_proteco_alloc(SubGhzEnvironment* environment);
SubGhzProtocolStatus
    subghz_protocol_encoder_proteco_deserialize(void* context, FlipperFormat* format);
void* subghz_protocol_decoder_proteco_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_proteco_reset(void* context);
void subghz_protocol_decoder_proteco_feed(void* context, bool level, uint32_t duration);
SubGhzProtocolStatus
    subghz_protocol_decoder_proteco_deserialize(void* context, FlipperFormat* format);
void subghz_protocol_decoder_proteco_get_string(void* context, FuriString* output);
