#pragma once
#include "base.h"

#define SUBGHZ_PROTOCOL_FAAC_TM_NAME "Faac TM"

typedef struct SubGhzProtocolDecoderFaacTm SubGhzProtocolDecoderFaacTm;
typedef struct SubGhzProtocolEncoderFaacTm SubGhzProtocolEncoderFaacTm;
extern const SubGhzProtocol subghz_protocol_faac_tm;

void* subghz_protocol_encoder_faac_tm_alloc(SubGhzEnvironment* environment);
SubGhzProtocolStatus
    subghz_protocol_encoder_faac_tm_deserialize(void* context, FlipperFormat* format);
void* subghz_protocol_decoder_faac_tm_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_faac_tm_reset(void* context);
void subghz_protocol_decoder_faac_tm_feed(void* context, bool level, uint32_t duration);
SubGhzProtocolStatus
    subghz_protocol_decoder_faac_tm_deserialize(void* context, FlipperFormat* format);
void subghz_protocol_decoder_faac_tm_get_string(void* context, FuriString* output);
