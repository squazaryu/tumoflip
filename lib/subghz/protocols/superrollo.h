#pragma once
#include "base.h"

#define SUBGHZ_PROTOCOL_SUPERROLLO_NAME "Superrollo"

typedef struct SubGhzProtocolDecoderSuperrollo SubGhzProtocolDecoderSuperrollo;
typedef struct SubGhzProtocolEncoderSuperrollo SubGhzProtocolEncoderSuperrollo;

extern const SubGhzProtocolDecoder subghz_protocol_superrollo_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_superrollo_encoder;
extern const SubGhzProtocol subghz_protocol_superrollo;

bool subghz_protocol_superrollo_create_data(
    void* context,
    FlipperFormat* flipper_format,
    uint32_t serial,
    uint8_t btn,
    uint16_t cnt,
    SubGhzRadioPreset* preset);

/**
 * Allocate SubGhzProtocolEncoderSuperrollo.
 * @param environment Pointer to a SubGhzEnvironment instance
 * @return SubGhzProtocolEncoderSuperrollo* pointer to a SubGhzProtocolEncoderSuperrollo instance
 */
void* subghz_protocol_encoder_superrollo_alloc(SubGhzEnvironment* environment);

/**
 * Free SubGhzProtocolEncoderSuperrollo.
 * @param context Pointer to a SubGhzProtocolEncoderSuperrollo instance
 */
void subghz_protocol_encoder_superrollo_free(void* context);

/**
 * Deserialize and generating an upload to send.
 * @param context Pointer to a SubGhzProtocolEncoderSuperrollo instance
 * @param flipper_format Pointer to a FlipperFormat instance
 * @return true On success
 */
SubGhzProtocolStatus
    subghz_protocol_encoder_superrollo_deserialize(void* context, FlipperFormat* flipper_format);

/**
 * Forced transmission stop.
 * @param context Pointer to a SubGhzProtocolEncoderSuperrollo instance
 */
void subghz_protocol_encoder_superrollo_stop(void* context);

/**
 * Getting the level and duration of the upload to be loaded into DMA.
 * @param context Pointer to a SubGhzProtocolEncoderSuperrollo instance
 * @return LevelDuration
 */
LevelDuration subghz_protocol_encoder_superrollo_yield(void* context);

/**
 * Allocate SubGhzProtocolDecoderSuperrollo.
 * @param environment Pointer to a SubGhzEnvironment instance
 * @return SubGhzProtocolDecoderSuperrollo* pointer to a SubGhzProtocolDecoderSuperrollo instance
 */
void* subghz_protocol_decoder_superrollo_alloc(SubGhzEnvironment* environment);

/**
 * Free SubGhzProtocolDecoderSuperrollo.
 * @param context Pointer to a SubGhzProtocolDecoderSuperrollo instance
 */
void subghz_protocol_decoder_superrollo_free(void* context);

/**
 * Reset decoder SubGhzProtocolDecoderSuperrollo.
 * @param context Pointer to a SubGhzProtocolDecoderSuperrollo instance
 */
void subghz_protocol_decoder_superrollo_reset(void* context);

/**
 * Parse a raw sequence of levels and durations received from the air.
 * @param context Pointer to a SubGhzProtocolDecoderSuperrollo instance
 * @param level Signal level true-high false-low
 * @param duration Duration of this level in, us
 */
void subghz_protocol_decoder_superrollo_feed(void* context, bool level, uint32_t duration);

/**
 * Getting the hash sum of the last randomly received parcel.
 * @param context Pointer to a SubGhzProtocolDecoderSuperrollo instance
 * @return hash Hash sum
 */
uint8_t subghz_protocol_decoder_superrollo_get_hash_data(void* context);

/**
 * Serialize data SubGhzProtocolDecoderSuperrollo.
 * @param context Pointer to a SubGhzProtocolDecoderSuperrollo instance
 * @param flipper_format Pointer to a FlipperFormat instance
 * @param preset The modulation on which the signal was received, SubGhzRadioPreset
 * @return status
 */
SubGhzProtocolStatus subghz_protocol_decoder_superrollo_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

/**
 * Deserialize data SubGhzProtocolDecoderSuperrollo.
 * @param context Pointer to a SubGhzProtocolDecoderSuperrollo instance
 * @param flipper_format Pointer to a FlipperFormat instance
 * @return status
 */
SubGhzProtocolStatus
    subghz_protocol_decoder_superrollo_deserialize(void* context, FlipperFormat* flipper_format);

/**
 * Getting a textual representation of the received data.
 * @param context Pointer to a SubGhzProtocolDecoderSuperrollo instance
 * @param output Resulting text
 */
void subghz_protocol_decoder_superrollo_get_string(void* context, FuriString* output);
