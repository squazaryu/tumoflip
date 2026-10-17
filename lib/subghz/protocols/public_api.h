#pragma once

#define SUBGHZ_PRESS_IDENTITY_WORDS 5

#include "../types.h"
#include "base.h"

#define SUBGHZ_PROTOCOL_PUBLIC_API_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SubGhzDevice SubGhzDevice;
void subghz_device_registry_init(void);

void subghz_device_registry_deinit(void);

bool subghz_device_registry_is_valid(void);

const SubGhzDevice* subghz_device_registry_get_by_name(const char* name);

/**
 * Key generation from simple data.
 * @param context Pointer to a SubGhzProtocolEncoderSecPlus_v2 instance
 * @param flipper_format Pointer to a FlipperFormat instance
 * @param serial Serial number, 32 bit
 * @param btn Button number, 8 bit
 * @param cnt Container value, 28 bit
 * @param manufacture_name Name of manufacturer's key
 * @param preset Modulation, SubGhzRadioPreset
 * @return true On success
 */
bool subghz_protocol_secplus_v2_create_data(
    void* context,
    FlipperFormat* flipper_format,
    uint32_t serial,
    uint8_t btn,
    uint32_t cnt,
    SubGhzRadioPreset* preset);

/**
 * Key generation from simple data.
 * @param context Pointer to a SubGhzProtocolEncoderKeeloq instance
 * @param flipper_format Pointer to a FlipperFormat instance
 * @param serial Serial number, 28 bit
 * @param btn Button number, 4 bit
 * @param cnt Counter value, 16 bit
 * @param manufacture_name Name of manufacturer's key
 * @param preset Modulation, SubGhzRadioPreset
 * @return true On success
 */
bool subghz_protocol_keeloq_create_data(
    void* context,
    FlipperFormat* flipper_format,
    uint32_t serial,
    uint8_t btn,
    uint16_t cnt,
    const char* manufacture_name,
    SubGhzRadioPreset* preset);

/**
 * Key generation for BFT.
 * @param context Pointer to a SubGhzProtocolEncoderKeeloq instance
 * @param flipper_format Pointer to a FlipperFormat instance
 * @param serial Serial number, 28 bit
 * @param btn Button number, 4 bit
 * @param cnt Counter value, 16 bit
 * @param seed Seed value, 32 bit
 * @param manufacture_name Name of manufacturer's key
 * @param preset Modulation, SubGhzRadioPreset
 * @return true On success
 */
bool subghz_protocol_keeloq_seed_create_data(
    void* context,
    FlipperFormat* flipper_format,
    uint32_t serial,
    uint8_t btn,
    uint16_t cnt,
    uint32_t seed,
    const char* manufacture_name,
    SubGhzRadioPreset* preset);

/**
 * Key generation from simple data.
 * @param context Pointer to a SubGhzProtocolEncoderFaacSLH instance
 * @param flipper_format Pointer to a FlipperFormat instance
 * @param serial Serial number, 28 bit
 * @param btn Button number, 4 bit
 * @param cnt Counter value, 16 bit
 * @param seed Seed value, 32 bit
 * @param manufacture_name Name of manufacturer's key
 * @param preset Modulation, SubGhzRadioPreset
 * @return true On success
 */
bool subghz_protocol_faac_slh_create_data(
    void* context,
    FlipperFormat* flipper_format,
    uint32_t serial,
    uint8_t btn,
    uint32_t cnt,
    uint32_t seed,
    const char* manufacture_name,
    SubGhzRadioPreset* preset);

void faac_slh_reset_prog_mode(void);

uint32_t subghz_protocol_keeloq_common_decrypt(const uint32_t data, const uint64_t key);

uint8_t subghz_protocol_marantec_crc8(uint8_t* data, size_t len);

/**
 * Key generation from simple data.
 * @param context Pointer to a SubGhzProtocolEncoderAlutech_at_4n instance
 * @param flipper_format Pointer to a FlipperFormat instance
 * @param serial Serial number, 24 bit
 * @param btn Button number, 8 bit
 * @param cnt Counter value, 16 bit
 * @param preset Modulation, SubGhzRadioPreset
 * @return true On success
 */
bool subghz_protocol_alutech_at_4n_create_data(
    void* context,
    FlipperFormat* flipper_format,
    uint32_t serial,
    uint8_t btn,
    uint16_t cnt,
    SubGhzRadioPreset* preset);

/**
 * Key generation from simple data.
 * @param context Pointer to a SubGhzProtocolEncoderCameAtomo instance
 * @param flipper_format Pointer to a FlipperFormat instance
 * @param serial Serial number, 24 bit
 * @param cnt Counter value, 16 bit
 * @param preset Modulation, SubGhzRadioPreset
 * @return true On success
 */
bool subghz_protocol_came_atomo_create_data(
    void* context,
    FlipperFormat* flipper_format,
    uint32_t serial,
    uint16_t cnt,
    SubGhzRadioPreset* preset);

/**
 * Key generation from simple data.
 * @param context Pointer to a SubGhzProtocolEncoderPhoenix_V2 instance
 * @param flipper_format Pointer to a FlipperFormat instance
 * @param serial Serial number
 * @param cnt Counter value, 16 bit
 * @param preset Modulation, SubGhzRadioPreset
 * @return true On success
 */
bool subghz_protocol_phoenix_v2_create_data(
    void* context,
    FlipperFormat* flipper_format,
    uint32_t serial,
    uint16_t cnt,
    SubGhzRadioPreset* preset);

/**
 * New remote generation.
 * @param context Pointer to a SubGhzProtocolEncoderNiceFlorS instance
 * @param flipper_format Pointer to a FlipperFormat instance
 * @param serial Serial number
 * @param btn Button number, 4 bit
 * @param cnt Counter value, 16 bit
 * @param preset Modulation, SubGhzRadioPreset
 * @param nice_one Nice One if true, Nice Flor S if false
 * @return true On success
 */
bool subghz_protocol_nice_flor_s_create_data(
    void* context,
    FlipperFormat* flipper_format,
    uint32_t serial,
    uint8_t btn,
    uint16_t cnt,
    SubGhzRadioPreset* preset,
    bool nice_one);

/**
 * Create one Nice O-Code file frame for a known installer code.
 * The installer code is stored in the file as the `IC` field so the decoder can
 * distinguish O-Code from plain FloR-S. The receiver may still require the
 * remote to be enrolled through its normal pairing procedure.
 * @param context Pointer to a SubGhzProtocolEncoderNiceFlorS instance
 * @param flipper_format Pointer to a FlipperFormat instance
 * @param serial 28-bit serial number
 * @param btn 4-bit button number
 * @param cnt 16-bit counter
 * @param ic 16-bit installer code (0xFFFF is reserved for plain FloR-S)
 * @param preset Modulation, SubGhzRadioPreset
 * @return true On success, false if input is invalid or the rainbow table is missing
 */
/** Create the 72-bit O-Code / Nice One variant with the same known IC. */
bool subghz_protocol_nice_o_one_create_data(
    void* context,
    FlipperFormat* flipper_format,
    uint32_t serial,
    uint8_t btn,
    uint16_t cnt,
    uint16_t ic,
    SubGhzRadioPreset* preset);

bool subghz_protocol_nice_o_create_data(
    void* context,
    FlipperFormat* flipper_format,
    uint32_t serial,
    uint8_t btn,
    uint16_t cnt,
    uint16_t ic,
    SubGhzRadioPreset* preset);

/**
 * Key generation from simple data.
 * @param context Pointer to a SubGhzProtocolEncoderSomfyTelis instance
 * @param flipper_format Pointer to a FlipperFormat instance
 * @param serial Serial number, 24 bit
 * @param btn Button number, 8 bit
 * @param cnt Counter value, 16 bit
 * @param preset Modulation, SubGhzRadioPreset
 * @return true On success
 */
bool subghz_protocol_somfy_telis_create_data(
    void* context,
    FlipperFormat* flipper_format,
    uint32_t serial,
    uint8_t btn,
    uint16_t cnt,
    SubGhzRadioPreset* preset);

/**
 * Key generation from simple data.
 * @param context Pointer to a SubGhzProtocolEncoderSomfyKeytis instance
 * @param flipper_format Pointer to a FlipperFormat instance
 * @param serial Serial number, 24 bit
 * @param btn Button number, 8 bit
 * @param cnt Counter value, 16 bit
 * @param preset Modulation, SubGhzRadioPreset
 * @return true On success
 */
bool subghz_protocol_somfy_keytis_create_data(
    void* context,
    FlipperFormat* flipper_format,
    uint32_t serial,
    uint8_t btn,
    uint16_t cnt,
    SubGhzRadioPreset* preset);

/**
 * Key generation from simple data.
 * @param context Pointer to a SubGhzProtocolEncoderKingGates_stylo_4k instance
 * @param flipper_format Pointer to a FlipperFormat instance
 * @param serial Serial number, 24 bit
 * @param btn Button number, 8 bit
 * @param cnt Counter value, 16 bit
 * @param preset Modulation, SubGhzRadioPreset
 * @return true On success
 */
bool subghz_protocol_kinggates_stylo_4k_create_data(
    void* context,
    FlipperFormat* flipper_format,
    uint32_t serial,
    uint8_t btn,
    uint16_t cnt,
    SubGhzRadioPreset* preset);

/**
 * Key generation from simple data.
 * @param context Pointer to a SubGhzProtocolEncoderBenincaARC instance
 * @param flipper_format Pointer to a FlipperFormat instance
 * @param serial Serial number, 32 bit
 * @param btn Button number, 8 bit
 * @param cnt Counter value, 32 bit
 * @param preset Modulation, SubGhzRadioPreset
 * @return true On success
 */
bool subghz_protocol_beninca_arc_create_data(
    void* context,
    FlipperFormat* flipper_format,
    uint32_t serial,
    uint8_t btn,
    uint32_t cnt,
    SubGhzRadioPreset* preset);

/**
 * Key generation from simple data.
 * @param context Pointer to a SubGhzProtocolEncoderJarolift instance
 * @param flipper_format Pointer to a FlipperFormat instance
 * @param serial Serial number, 24 bit
 * @param btn Button number, 8 bit
 * @param cnt Counter value, 16 bit
 * @param preset Modulation, SubGhzRadioPreset
 * @return true On success
 */
bool subghz_protocol_jarolift_create_data(
    void* context,
    FlipperFormat* flipper_format,
    uint32_t serial,
    uint8_t btn,
    uint16_t cnt,
    SubGhzRadioPreset* preset);

/**
 * Key generation from simple data.
 * @param context Pointer to a SubGhzProtocolEncoderDitecGOL4 instance
 * @param flipper_format Pointer to a FlipperFormat instance
 * @param serial Serial number
 * @param btn Button number, 8 bit
 * @param cnt Counter value, 16 bit
 * @param preset Modulation, SubGhzRadioPreset
 * @return true On success
 */
bool subghz_protocol_ditec_gol4_create_data(
    void* context,
    FlipperFormat* flipper_format,
    uint32_t serial,
    uint8_t btn,
    uint16_t cnt,
    SubGhzRadioPreset* preset);

typedef struct SubGhzProtocolDecoderBinRAW SubGhzProtocolDecoderBinRAW;

void subghz_protocol_decoder_bin_raw_data_input_rssi(
    SubGhzProtocolDecoderBinRAW* instance,
    float rssi);

/**
 * Validation of fixed parts SubGhzProtocolDecoderSecPlus_v1.
 * @param fixed fixed parts
 * @return true On success
 */
bool subghz_protocol_secplus_v1_check_fixed(uint32_t fixed);

/** Nice O-Code helpers used by the installer-code recovery app. */
void subghz_protocol_nice_o_mask(uint8_t n, uint32_t* mask_32, uint16_t* mask_16);
uint8_t subghz_protocol_nice_o_get_parcel(uint64_t data);
void subghz_protocol_decoder_nice_flor_s_set_skip_o_code(void* context, bool skip);

/** Return true for a received O-Code frame without a known installer code. */
bool subghz_protocol_decoder_nice_flor_s_is_o_code_no_ic(void* context);

/** Only supported short frames with known IC (where required) can be encoded. */
bool subghz_protocol_decoder_nice_flor_s_can_transmit(void* context);

/** Fill five words of exact press identity for supported rotating-parcel decoders.
 * Call after get_hash_data, which extracts the decoded fields. No allocation or I/O.
 * Returns false for other protocol layouts; never cast arbitrary plugin decoders.
 */
bool subghz_protocol_decoder_get_press_identity(
    SubGhzProtocolDecoderBase* decoder,
    uint32_t* identity);
#define SUBGHZ_NO_NICE_FLOR_S_RAINBOW_TABLE 0
uint64_t subghz_protocol_nice_flor_s_decrypt_ic(
    uint64_t data,
    uint16_t ic,
    const char* file_name);

#ifdef __cplusplus
}
#endif
