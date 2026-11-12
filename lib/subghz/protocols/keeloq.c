#include "keeloq.h"
#include "keeloq_common.h"

#include "../subghz_keystore.h"
#include <m-array.h>

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"
#include "common.h"

#include "../blocks/custom_btn_i.h"
#include "../subghz_keystore_i.h"

#define TAG "SubGhzProtocolKeeloq"

// Received Monarch discriminator, preserved as metadata rather than regenerated.
#define KEELOQ_MONARCH_DISCRIMINATOR_A 0x100u
#define KEELOQ_MONARCH_DISCRIMINATOR_B 0x280u

// KEY discriminator, constant. Only one remote seen so far - add values here if another
// one turns up with a different constant.
#define KEELOQ_KEY_DISCRIMINATOR 0x2FFu

// Some HCS encoders use TE 200us instead of 400us. Select the rate from
// preamble + sync, without changing the frame format or counter semantics.
#define KEELOQ_TE_FAST           200u
#define KEELOQ_TE_FAST_GAP_DELTA 450u

// This adaptation adds recognition only. Keep the existing Monarch variant's
// behavior, but reject newly recognized variants before any TX/counter mutation.
static bool subghz_protocol_keeloq_receive_only(const char* name, uint32_t discriminator) {
    return strcmp(name, "KEY") == 0 ||
           (strcmp(name, "Monarch") == 0 && discriminator == KEELOQ_MONARCH_DISCRIMINATOR_B);
}

//variable used to bypass CounterMode settings if user just change Counter or Button
static bool bypass = false;

static const SubGhzBlockConst subghz_protocol_keeloq_const = {
    .te_short = 400,
    .te_long = 800,
    .te_delta = 180,
    .min_count_bit_for_found = 64,
};

static inline uint32_t subghz_protocol_keeloq_te_delta(uint32_t te) {
    return te * subghz_protocol_keeloq_const.te_delta / subghz_protocol_keeloq_const.te_short;
}

static inline bool subghz_protocol_keeloq_te_preamble(uint32_t duration) {
    // The two allowed short-pulse bands overlap; their union is contiguous.
    return duration > KEELOQ_TE_FAST - subghz_protocol_keeloq_te_delta(KEELOQ_TE_FAST) &&
           duration <
               subghz_protocol_keeloq_const.te_short + subghz_protocol_keeloq_const.te_delta;
}

static inline uint32_t subghz_protocol_keeloq_te_sanitize(uint32_t te) {
    return te == KEELOQ_TE_FAST ? KEELOQ_TE_FAST : subghz_protocol_keeloq_const.te_short;
}

static inline uint32_t subghz_protocol_keeloq_te_nominal(uint32_t te) {
    return te < (KEELOQ_TE_FAST + subghz_protocol_keeloq_const.te_short) / 2 ?
               KEELOQ_TE_FAST :
               subghz_protocol_keeloq_const.te_short;
}

struct SubGhzProtocolDecoderKeeloq {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint16_t header_count;
    uint16_t te; // Measured fast TE, or the legacy nominal 400us.
    SubGhzKeystore* keystore;
    const char* manufacture_name;

    FuriString* manufacture_from_file;
};
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderKeeloq);

struct SubGhzProtocolEncoderKeeloq {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    SubGhzKeystore* keystore;
    const char* manufacture_name;

    FuriString* manufacture_from_file;
    uint16_t te; // Only nominal 200/400us, validated on every file load.
};
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderKeeloq);

typedef enum {
    KeeloqDecoderStepReset = 0,
    KeeloqDecoderStepCheckPreambula,
    KeeloqDecoderStepSaveDuration,
    KeeloqDecoderStepCheckDuration,
} KeeloqDecoderStep;

static uint8_t keeloq_counter_mode = 0;

// One file-metadata path for RX inspection and TX loading. Keep it outlined:
// duplicating this logic per caller wastes resident flash without changing I/O.
#if defined(__GNUC__) && !defined(__clang__)
__attribute__((noinline, noclone))
#endif
static bool
    subghz_protocol_keeloq_read_metadata(
        SubGhzBlockGeneric* generic,
        FlipperFormat* format,
        SubGhzKeystore* keystore,
        FuriString* manufacture_from_file,
        const char** manufacture_name,
        uint16_t* te) {
    uint8_t seed[sizeof(uint32_t)];
    for(size_t i = 0; i < sizeof(seed); i++) {
        seed[sizeof(seed) - i - 1] = (generic->seed >> (i * 8)) & 0xFF;
    }
    flipper_format_read_hex(format, "Seed", seed, sizeof(seed));
    generic->seed = (uint32_t)seed[0] << 24 | (uint32_t)seed[1] << 16 | (uint32_t)seed[2] << 8 |
                    seed[3];
    if(!flipper_format_rewind(format)) return false;

    if(flipper_format_read_string(format, "Manufacture", manufacture_from_file)) {
        *manufacture_name = furi_string_get_cstr(manufacture_from_file);
        keystore->mfname = *manufacture_name;
        // Preserve the existing old-file name migration.
        if(strcmp(*manufacture_name, "Sommer(fsk476)") == 0) {
            *manufacture_name = "Sommer";
            keystore->mfname = *manufacture_name;
            if(!flipper_format_rewind(format) ||
               !flipper_format_update_string_cstr(format, "Manufacture", *manufacture_name)) {
                return false;
            }
        }
    }
    if(!flipper_format_rewind(format)) return false;

    uint32_t value = subghz_protocol_keeloq_const.te_short;
    if(!flipper_format_read_uint32(format, "TE", &value, 1)) {
        value = subghz_protocol_keeloq_const.te_short;
    }
    *te = subghz_protocol_keeloq_te_sanitize(value);
    if(!flipper_format_rewind(format)) return false;

    value = 0;
    keeloq_counter_mode =
        flipper_format_read_uint32(format, "CounterMode", &value, 1) ? (uint8_t)value : 0;
    return flipper_format_rewind(format);
}

const SubGhzProtocolDecoder subghz_protocol_keeloq_decoder = {
    .alloc = subghz_protocol_decoder_keeloq_alloc,
    .free = subghz_protocol_decoder_keeloq_free,

    .feed = subghz_protocol_decoder_keeloq_feed,
    .reset = subghz_protocol_decoder_keeloq_reset,

    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_keeloq_serialize,
    .deserialize = subghz_protocol_decoder_keeloq_deserialize,
    .get_string = subghz_protocol_decoder_keeloq_get_string,
};

const SubGhzProtocolEncoder subghz_protocol_keeloq_encoder = {
    .alloc = subghz_protocol_encoder_keeloq_alloc,
    .free = subghz_protocol_encoder_keeloq_free,

    .deserialize = subghz_protocol_encoder_keeloq_deserialize,
    .stop = subghz_protocol_encoder_keeloq_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

const SubGhzProtocol subghz_protocol_keeloq = {
    .name = SUBGHZ_PROTOCOL_KEELOQ_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_868 | SubGhzProtocolFlag_315 |
            SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load |
            SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,

    .decoder = &subghz_protocol_keeloq_decoder,
    .encoder = &subghz_protocol_keeloq_encoder,
};

/** 
 * Analysis of received data
 * @param instance Pointer to a SubGhzBlockGeneric* instance
 * @param keystore Pointer to a SubGhzKeystore* instance
 * @param manufacture_name
 */
static uint32_t subghz_protocol_keeloq_check_remote_controller(
    SubGhzBlockGeneric* instance,
    SubGhzKeystore* keystore,
    const char** manufacture_name);

/**
 * Defines the button value for the current btn_id
 * Basic set | 0x1 | 0x2 | 0x4 | 0x8 | 0xA or Special Learning Code |
 * @param last_btn_code Candidate for the last button
 * @return Button code
 */
static uint8_t subghz_protocol_keeloq_get_btn_code(uint8_t last_btn_code);

void* subghz_protocol_encoder_keeloq_alloc(SubGhzEnvironment* environment) {
    SubGhzProtocolEncoderKeeloq* instance = subghz_protocol_encoder_common_alloc(
        sizeof(SubGhzProtocolEncoderKeeloq), &subghz_protocol_keeloq, 3, 1100);
    instance->keystore = subghz_environment_get_keystore(environment);
    instance->manufacture_from_file = furi_string_alloc();
    instance->te = subghz_protocol_keeloq_const.te_short;
    return instance;
}

void subghz_protocol_encoder_keeloq_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderKeeloq* instance = context;
    furi_string_free(instance->manufacture_from_file);
    free(instance->encoder.upload);
    free(instance);
}

/** 
 * Key generation from simple data
 * @param instance Pointer to a SubGhzProtocolEncoderKeeloq* instance
 * @param btn Button number, 4 bit
 * @param counter_up increasing the counter if the value is true
 */
static bool subghz_protocol_keeloq_uses_12bit_serial(const char* name) {
    static const char* const names[] = {
        "DTM_Neo",
        "FAAC_RC,XT",
        "Clemsa_Mutancode",
        "Came_Space",
        "Genius_Bravo",
        "GSN",
        "Rosh",
        "Rossi",
        "Pecinin",
        "Steelmate",
        "Cardin_S449",
        "Stilmatic",
        "Wisniowski",
        "Wisniowski2",
        "Wisniowski1Rv",
        "ATA_PTX4",
        "Fadini",
        "Seav",
    };
    for(size_t i = 0; i < COUNT_OF(names); i++) {
        if(strcmp(name, names[i]) == 0) return true;
    }
    return false;
}

static bool subghz_protocol_keeloq_gen_data(
    SubGhzProtocolEncoderKeeloq* instance,
    uint8_t btn,
    bool counter_up,
    bool skip_btn_check) {
    // No mf name set? -> set to ""
    if(instance->manufacture_name == 0x0) {
        instance->manufacture_name = "";
    }
    // add gendata part
    ProgMode prog_mode = subghz_custom_btn_get_prog_mode();
    if(!skip_btn_check && (keeloq_counter_mode != 7)) {
        // Save original button
        if(subghz_custom_btn_get_original() == 0) {
            subghz_custom_btn_set_original(btn);
        }

        // Prog mode checks and extra fixage of MF Names
        if(prog_mode == PROG_MODE_KEELOQ_BFT) {
            instance->manufacture_name = "BFT";
        } else if(prog_mode == PROG_MODE_KEELOQ_APRIMATIC) {
            instance->manufacture_name = "Aprimatic";
        } else if(prog_mode == PROG_MODE_KEELOQ_DEA_MIO) {
            instance->manufacture_name = "Dea_Mio";
        }
        // Custom button (programming mode button) for BFT, Aprimatic, Dea_Mio
        uint8_t klq_last_custom_btn = 0xA;
        if((strcmp(instance->manufacture_name, "BFT") == 0) ||
           (strcmp(instance->manufacture_name, "Aprimatic") == 0) ||
           (strcmp(instance->manufacture_name, "Dea_Mio") == 0) ||
           (strcmp(instance->manufacture_name, "NICE_MHOUSE") == 0)) {
            klq_last_custom_btn = 0xF;
        } else if(
            (strcmp(instance->manufacture_name, "FAAC_RC,XT") == 0) ||
            (strcmp(instance->manufacture_name, "Monarch") == 0) ||
            (strcmp(instance->manufacture_name, "NICE_Smilo") == 0) ||
            (strcmp(instance->manufacture_name, "Genius_Bravo") == 0)) {
            klq_last_custom_btn = 0xB;
        } else if(
            (strcmp(instance->manufacture_name, "Novoferm") == 0) ||
            (strcmp(instance->manufacture_name, "Stilmatic") == 0)) {
            klq_last_custom_btn = 0x9;
        } else if(
            (strcmp(instance->manufacture_name, "EcoStar") == 0) ||
            (strcmp(instance->manufacture_name, "Sommer") == 0)) {
            klq_last_custom_btn = 0x6;
        } else if((strcmp(instance->manufacture_name, "AN-Motors") == 0)) {
            klq_last_custom_btn = 0xC;
        } else if((strcmp(instance->manufacture_name, "Cardin_S449") == 0)) {
            klq_last_custom_btn = 0xD;
        }

        btn = subghz_protocol_keeloq_get_btn_code(klq_last_custom_btn);
    }
    // end gendata part
    // override button if we change it with signal settings button editor
    if(subghz_block_generic_global_button_override_get(&btn)) {
        FURI_LOG_D(TAG, "Button sucessfully changed to 0x%X", btn);
    }

    uint32_t fix = 0;

    if(strcmp(instance->manufacture_name, "Pecinin") == 0) {
        // No button code in fix
        fix = instance->generic.serial;
    } else {
        fix = (uint32_t)btn << 28 | instance->generic.serial;
    }

    uint32_t hop = 0;
    uint64_t man = 0;
    uint64_t code_found_reverse;
    int res = 0;

    // programming mode on / off conditions
    if(strcmp(instance->manufacture_name, "BFT") == 0) {
        // BFT programming mode on / off conditions
        if(btn == 0xF) {
            prog_mode = PROG_MODE_KEELOQ_BFT;
        } else if(prog_mode == PROG_MODE_KEELOQ_BFT) {
            prog_mode = PROG_MODE_OFF;
        }
    } else if(strcmp(instance->manufacture_name, "Aprimatic") == 0) {
        // Aprimatic programming mode on / off conditions
        if(btn == 0xF) {
            prog_mode = PROG_MODE_KEELOQ_APRIMATIC;
        } else if(prog_mode == PROG_MODE_KEELOQ_APRIMATIC) {
            prog_mode = PROG_MODE_OFF;
        }
    } else if(strcmp(instance->manufacture_name, "Dea_Mio") == 0) {
        // Dea_Mio programming mode on / off conditions
        if(btn == 0xF) {
            prog_mode = PROG_MODE_KEELOQ_DEA_MIO;
        } else if(prog_mode == PROG_MODE_KEELOQ_DEA_MIO) {
            prog_mode = PROG_MODE_OFF;
        }
    }
    subghz_custom_btn_set_prog_mode(prog_mode);

    // If we using BFT programming mode we will trasmit its seed in hop part like original remote
    if(prog_mode == PROG_MODE_KEELOQ_BFT) {
        hop = instance->generic.seed;
    } else if(prog_mode == PROG_MODE_KEELOQ_APRIMATIC) {
        // If we using Aprimatic programming mode we will trasmit some strange looking hop value, why? cuz manufacturer did it this way :)
        hop = 0x1A2B3C4D;
    } else if(prog_mode == PROG_MODE_KEELOQ_DEA_MIO) {
        // If we using DEA Mio programming mode we will trasmit only FIX value with button code 0xF, hop is zero
        hop = 0x00000000;
    }
    if(counter_up && prog_mode == PROG_MODE_OFF) {
        // Counter increment conditions

        if(keeloq_counter_mode == 0 || bypass) {
            // Check for OFEX (overflow experimental) mode
            if(furi_hal_subghz_get_rolling_counter_mult() != -0x7FFFFFFF || bypass) {
                bypass = false;
                // standart counter mode. PULL data from subghz_block_generic_global variables
                if(!subghz_block_generic_global_counter_override_get(&instance->generic.cnt)) {
                    // if counter_override_get return FALSE then counter was not changed and we increase counter by standart mult value
                    if((instance->generic.cnt + furi_hal_subghz_get_rolling_counter_mult()) >
                       0xFFFF) {
                        instance->generic.cnt = 0;
                    } else {
                        instance->generic.cnt += furi_hal_subghz_get_rolling_counter_mult();
                    }
                }
            } else {
                if((instance->generic.cnt + 0x1) > 0xFFFF) {
                    instance->generic.cnt = 0;
                } else if(instance->generic.cnt >= 0x1 && instance->generic.cnt != 0xFFFE) {
                    instance->generic.cnt = 0xFFFE;
                } else {
                    instance->generic.cnt++;
                }
            }
        } else if(keeloq_counter_mode == 1) {
            // Mode 1
            // 0000 / 0001 / FFFE / FFFF
            if((instance->generic.cnt + 0x1) > 0xFFFF) {
                instance->generic.cnt = 0;
            } else if(instance->generic.cnt >= 0x1 && instance->generic.cnt != 0xFFFE) {
                instance->generic.cnt = 0xFFFE;
            } else {
                instance->generic.cnt++;
            }
        } else if(keeloq_counter_mode == 2) {
            // Mode 2
            // + 0x3333 each time
            if((instance->generic.cnt + 0x3333) > 0xFFFF) {
                instance->generic.cnt = 0;
            } else {
                instance->generic.cnt += 0x3333;
            }
        } else if(keeloq_counter_mode == 3) {
            // Mode 3
            // 0x8006 / 0x8007 / 0x0006 / 0x0007
            if(instance->generic.cnt != 0x8006 && instance->generic.cnt != 0x8007 &&
               instance->generic.cnt != 0x0006) {
                instance->generic.cnt = 0x8006;
            } else if(instance->generic.cnt == 0x8007) {
                instance->generic.cnt = 0x0006;
            } else {
                instance->generic.cnt++;
            }

        } else if(keeloq_counter_mode == 4) {
            // Mode 4
            // 0x807B / 0x807C / 0x007B / 0x007C
            if(instance->generic.cnt != 0x807B && instance->generic.cnt != 0x807C &&
               instance->generic.cnt != 0x007B) {
                instance->generic.cnt = 0x807B;
            } else if(instance->generic.cnt == 0x807C) {
                instance->generic.cnt = 0x007B;
            } else {
                instance->generic.cnt++;
            }
        } else if(keeloq_counter_mode == 5) {
            // Mode 5
            // 0000 / FFFF
            if((instance->generic.cnt + 0x1) > 0xFFFF) {
                instance->generic.cnt = 0;
            } else {
                instance->generic.cnt = 0xFFFF;
            }
        } else if(keeloq_counter_mode == 6) {
            // Mode 6 - Freeze counter
        } else {
            // Mode 7 - Make 12 signals in row with mode 2
            // + 0x3333 each time
            if((instance->generic.cnt + 0x3333) > 0xFFFF) {
                instance->generic.cnt = 0;
            } else {
                instance->generic.cnt += 0x3333;
            }
        }
    }
    if(prog_mode == PROG_MODE_OFF) {
        // Protocols that do not use encryption
        if(strcmp(instance->manufacture_name, "Unknown") == 0) {
            // Simple Replay of received code
            code_found_reverse = subghz_protocol_blocks_reverse_key(
                instance->generic.data, instance->generic.data_count_bit);
            hop = code_found_reverse & 0x00000000ffffffff;
        } else if(strcmp(instance->manufacture_name, "AN-Motors") == 0) {
            // An-Motors encode
            hop = (instance->generic.cnt & 0xFF) << 24 | (instance->generic.cnt & 0xFF) << 16 |
                  (btn & 0xF) << 12 | 0x404;
        } else if(strcmp(instance->manufacture_name, "HCS101") == 0) {
            // HCS101 Encode
            hop = instance->generic.cnt << 16 | (btn & 0xF) << 12 | 0x000;
        } else {
            // Protocols that use encryption
            uint32_t decrypt = (uint32_t)btn << 28 |
                               (instance->generic.serial & 0x3FF)
                                   << 16 | //ToDo in some protocols the discriminator is 0
                               instance->generic.cnt;

            if(strcmp(instance->manufacture_name, "Aprimatic") == 0) {
                // Aprimatic uses 12bit serial number + 2bit APR1 "parity" bit in front of it replacing first 2 bits of serial
                // Thats in theory! We need to check if this is true for all Aprimatic remotes but we got only 3 recordings to test
                // For now lets assume that this is true for all Aprimatic remotes, if not we will need to add some more code here
                uint32_t apri_serial = instance->generic.serial;
                uint8_t apr1 = 0;
                for(uint16_t i = 1; i != 0b10000000000; i <<= 1) {
                    if(apri_serial & i) apr1++;
                }
                apri_serial &= 0b00001111111111;
                if(apr1 % 2 == 0) {
                    apri_serial |= 0b110000000000;
                }
                decrypt = btn << 28 | (apri_serial & 0xFFF) << 16 | instance->generic.cnt;
            } else if(subghz_protocol_keeloq_uses_12bit_serial(instance->manufacture_name)) {
                // DTM Neo, Came_Space uses 12bit serial -> simple learning
                // FAAC_RC,XT , Clemsa_Mutancode, Genius_Bravo, GSN 12bit serial -> normal learning
                // Rosh, Rossi, Pecinin -> 12bit serial - simple learning
                // Steelmate -> 12bit serial - normal learning
                // Cardin_S449 -> 12bit serial - normal learning
                // Stilmatic (r-tech) -> 12bit serial - normal learning
                // Wisniowski family -> 12bit serial - normal learning
                // ATA_PTX4 -> 12bit serial - normal learning
                // Fadini -> 12bit serial - simple learning
                // Seav -> 12bit serial - normal learning
                decrypt = btn << 28 | (instance->generic.serial & 0xFFF) << 16 |
                          instance->generic.cnt;
            } else if(
                (strcmp(instance->manufacture_name, "NICE_Smilo") == 0) ||
                (strcmp(instance->manufacture_name, "NICE_MHOUSE") == 0) ||
                (strcmp(instance->manufacture_name, "JCM_Tech") == 0) ||
                (strcmp(instance->manufacture_name, "Pujol_Vario") == 0) ||
                (strcmp(instance->manufacture_name, "Pujol") == 0) ||
                (strcmp(instance->manufacture_name, "Erreka") == 0)) {
                // Nice Smilo, MHouse, JCM, Pujol_Vario -> 8bit serial - simple learning
                // Pujol -> 8bit serial - special learning
                // Erreka -> 8bit serial - secure learning with seed
                decrypt = btn << 28 | (instance->generic.serial & 0xFF) << 16 |
                          instance->generic.cnt;
            } else if(
                (strcmp(instance->manufacture_name, "Beninca") == 0) ||
                (strcmp(instance->manufacture_name, "Merlin") == 0)) {
                decrypt = btn << 28 | (0x000) << 16 | instance->generic.cnt;
                // Beninca / Allmatic -> no serial - simple XOR
                // Merlin -> no serial - simple XOR
            } else if(strcmp(instance->manufacture_name, "Centurion") == 0) {
                decrypt = btn << 28 | (0x1CE) << 16 | instance->generic.cnt;
                // Centurion -> no serial in hop, uses fixed value 0x1CE - normal learning
            } else if(strcmp(instance->manufacture_name, "Monarch") == 0) {
                decrypt = btn << 28 | (0x100) << 16 | instance->generic.cnt;
                // Monarch -> no serial in hop, uses fixed value 0x100 - normal learning
            } else if(strcmp(instance->manufacture_name, "Dea_Mio") == 0) {
                uint8_t first_disc_num = (instance->generic.serial >> 8) & 0xF;
                uint8_t result_disc = (0xC + (first_disc_num % 4));
                uint32_t dea_serial = (instance->generic.serial & 0xFF) |
                                      (((uint32_t)result_disc) << 8);
                decrypt = btn << 28 | (dea_serial & 0xFFF) << 16 | instance->generic.cnt;
                // Dea_Mio -> modified serial in hop, uses last 3 digits modifying first one (example - 419 -> C19)
                // (see formula in result_disc var) - simple learning
            }
            // Old type selector fixage for compatibilitiy with old signal files
            uint8_t kl_type_en = instance->keystore->kl_type;
            for
                M_EACH(
                    manufacture_code,
                    *subghz_keystore_get_data(instance->keystore),
                    SubGhzKeyArray_t) {
                    res = strcmp(
                        furi_string_get_cstr(manufacture_code->name), instance->manufacture_name);
                    if(res == 0) {
                        switch(manufacture_code->type) {
                        case KEELOQ_LEARNING_SIMPLE:
                            //Simple Learning
                            hop = subghz_protocol_keeloq_common_encrypt(
                                decrypt, manufacture_code->key);
                            break;
                        case KEELOQ_LEARNING_NORMAL:
                            //Simple Learning
                            man = subghz_protocol_keeloq_common_normal_learning(
                                fix, manufacture_code->key);
                            hop = subghz_protocol_keeloq_common_encrypt(decrypt, man);
                            break;
                        case KEELOQ_LEARNING_SECURE:
                            //Secure Learning
                            man = subghz_protocol_keeloq_common_secure_learning(
                                fix, instance->generic.seed, manufacture_code->key);
                            hop = subghz_protocol_keeloq_common_encrypt(decrypt, man);
                            break;
                        case KEELOQ_LEARNING_MAGIC_XOR_TYPE_1:
                            //Magic XOR type-1 Learning
                            man = subghz_protocol_keeloq_common_magic_xor_type1_learning(
                                instance->generic.serial, manufacture_code->key);
                            hop = subghz_protocol_keeloq_common_encrypt(decrypt, man);
                            break;
                        case KEELOQ_LEARNING_MAGIC_SERIAL_TYPE_1:
                            //Magic Serial Type 1 learning
                            man = subghz_protocol_keeloq_common_magic_serial_type1_learning(
                                fix, manufacture_code->key);
                            hop = subghz_protocol_keeloq_common_encrypt(decrypt, man);
                            break;
                        case KEELOQ_LEARNING_MAGIC_SERIAL_TYPE_2:
                            //Magic Serial Type 2 learning
                            man = subghz_protocol_keeloq_common_magic_serial_type2_learning(
                                fix, manufacture_code->key);
                            hop = subghz_protocol_keeloq_common_encrypt(decrypt, man);
                            break;
                        case KEELOQ_LEARNING_MAGIC_SERIAL_TYPE_3:
                            //Magic Serial Type 3 learning
                            man = subghz_protocol_keeloq_common_magic_serial_type3_learning(
                                instance->generic.serial, manufacture_code->key);
                            hop = subghz_protocol_keeloq_common_encrypt(decrypt, man);
                            break;
                        case KEELOQ_LEARNING_AERF:
                            man = subghz_protocol_keeloq_common_learning_aerf(
                                fix, manufacture_code->key);
                            /* The decoder undoes this with the round-limited variant, so the
                             * encoder has to use the matching limited encryption. */
                            hop = subghz_protocol_keeloq_common_encrypt_derived(
                                decrypt, man, KEELOQ_NL_EXTEND_LIMIT_AERF_ENC);
                            break;
                        case KEELOQ_LEARNING_JCM_GEN2:
                            man = subghz_protocol_keeloq_common_learning_jcm_gen2(
                                fix, (uint8_t)instance->generic.seed, manufacture_code->key);
                            hop = subghz_protocol_keeloq_common_encrypt(decrypt, man);
                            break;
                        case KEELOQ_LEARNING_STAGNOLI:
                            man = subghz_protocol_keeloq_common_learning_stagnoli(
                                fix, manufacture_code->key);
                            hop = subghz_protocol_keeloq_common_encrypt(decrypt, man);
                            break;
                        case KEELOQ_LEARNING_TELCOMA_TABLE_HI: {
                            uint32_t telcoma_table[4] = {0};
                            if(subghz_protocol_keeloq_common_get_telcoma_table(
                                   instance->keystore, telcoma_table)) {
                                man = subghz_protocol_keeloq_common_learning_telcoma_table(
                                    fix, telcoma_table);
                                hop = subghz_protocol_keeloq_common_encrypt(decrypt, man);
                            }
                            break;
                        }
                        case KEELOQ_LEARNING_ERREKA:
                            man = subghz_protocol_keeloq_common_learning_erreka(
                                fix, instance->generic.seed, manufacture_code->key);
                            hop = subghz_protocol_keeloq_common_encrypt(decrypt, man);
                            break;
                        case KEELOQ_LEARNING_PUJOL:
                            man = subghz_protocol_keeloq_common_learning_pujol(
                                fix, manufacture_code->key);
                            hop = subghz_protocol_keeloq_common_encrypt(decrypt, man);
                            break;
                        case KEELOQ_LEARNING_SIMPLE_JCM:
                            //Simple Learning 8 bit serial
                            decrypt = btn << 28 | (instance->generic.serial & 0xFF) << 16 |
                                      instance->generic.cnt;
                            hop = subghz_protocol_keeloq_common_encrypt(
                                decrypt, manufacture_code->key);
                            break;
                        case KEELOQ_LEARNING_UNKNOWN:
                            if(kl_type_en == 1) {
                                hop = subghz_protocol_keeloq_common_encrypt(
                                    decrypt, manufacture_code->key);
                            }
                            if(kl_type_en == 2) {
                                man = subghz_protocol_keeloq_common_normal_learning(
                                    fix, manufacture_code->key);
                                hop = subghz_protocol_keeloq_common_encrypt(decrypt, man);
                            }
                            if(kl_type_en == 3) {
                                man = subghz_protocol_keeloq_common_secure_learning(
                                    fix, instance->generic.seed, manufacture_code->key);
                                hop = subghz_protocol_keeloq_common_encrypt(decrypt, man);
                            }
                            if(kl_type_en == 4) {
                                man = subghz_protocol_keeloq_common_magic_xor_type1_learning(
                                    instance->generic.serial, manufacture_code->key);
                                hop = subghz_protocol_keeloq_common_encrypt(decrypt, man);
                            }
                            break;
                        }
                        break;
                    }
                }
        }
    }
    if(hop || (prog_mode == PROG_MODE_KEELOQ_DEA_MIO) || (prog_mode == PROG_MODE_KEELOQ_BFT)) {
        // If we have hop - we will save it to generic data var that will be used later in transmission
        uint64_t yek = (uint64_t)fix << 32 | hop;
        instance->generic.data =
            subghz_protocol_blocks_reverse_key(yek, instance->generic.data_count_bit);
    } // What should happen if seed = 0 in bft programming mode -> collapse of the universe I think :)
    return true; // Always return true
}

bool subghz_protocol_keeloq_create_data(
    void* context,
    FlipperFormat* flipper_format,
    uint32_t serial,
    uint8_t btn,
    uint16_t cnt,
    const char* manufacture_name,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolEncoderKeeloq* instance = context;
    instance->generic.serial = serial;
    instance->generic.cnt = cnt;
    instance->manufacture_name = manufacture_name;
    instance->generic.data_count_bit = 64;
    if(subghz_protocol_keeloq_gen_data(instance, btn, false, true)) {
        return (
            subghz_block_generic_serialize(&instance->generic, flipper_format, preset) ==
            SubGhzProtocolStatusOk);
    }
    return false;
}

bool subghz_protocol_keeloq_seed_create_data(
    void* context,
    FlipperFormat* flipper_format,
    uint32_t serial,
    uint8_t btn,
    uint16_t cnt,
    uint32_t seed,
    const char* manufacture_name,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolEncoderKeeloq* instance = context;
    instance->generic.serial = serial;
    instance->generic.btn = btn;
    instance->generic.cnt = cnt;
    instance->generic.seed = seed;
    instance->manufacture_name = manufacture_name;
    instance->generic.data_count_bit = 64;
    // hehehehe
    if(subghz_protocol_keeloq_gen_data(instance, btn, false, true)) {
        return (
            subghz_block_generic_serialize(&instance->generic, flipper_format, preset) ==
            SubGhzProtocolStatusOk);
    }
    return false;
}

static size_t subghz_protocol_encoder_keeloq_encode_to_timings(
    SubGhzProtocolEncoderKeeloq* instance,
    uint8_t btn,
    bool counter_up,
    size_t index) {
    furi_assert(instance);
    // Generate new key
    if(!subghz_protocol_keeloq_gen_data(instance, btn, counter_up, false)) {
        return 0;
    }

    // Build the existing nominal waveform once. A fast file scales only this
    // appended frame afterwards, avoiding duplicated variable-timing packing.
    const size_t first = index;
    const uint32_t te_short = subghz_protocol_keeloq_const.te_short;
    const uint32_t te_long = subghz_protocol_keeloq_const.te_long;
    uint32_t gap_duration = te_short * 40;
    if((strcmp(instance->manufacture_name, "Sommer") == 0)) {
        gap_duration = te_short * 29;
    }

    //Send header
    for(uint8_t i = 11; i > 0; i--) {
        instance->encoder.upload[index++] = level_duration_make(true, te_short);
        instance->encoder.upload[index++] = level_duration_make(false, te_short);
    }
    instance->encoder.upload[index++] = level_duration_make(true, te_short);
    instance->encoder.upload[index++] = level_duration_make(false, te_short * 10);

    //Send key data
    for(uint8_t i = instance->generic.data_count_bit; i > 0; i--) {
        if(bit_read(instance->generic.data, i - 1)) {
            //send bit 1
            instance->encoder.upload[index++] = level_duration_make(true, te_short);
            instance->encoder.upload[index++] = level_duration_make(false, te_long);
        } else {
            //send bit 0
            instance->encoder.upload[index++] = level_duration_make(true, te_long);
            instance->encoder.upload[index++] = level_duration_make(false, te_short);
        }
    }
    // +send 2 status bit
    instance->encoder.upload[index++] = level_duration_make(true, te_short);
    instance->encoder.upload[index++] = level_duration_make(false, te_long);
    // send end
    instance->encoder.upload[index++] = level_duration_make(true, te_short);
    instance->encoder.upload[index++] = level_duration_make(false, gap_duration);

    if(instance->te == KEELOQ_TE_FAST) {
        for(size_t i = first; i < index; i++) {
            instance->encoder.upload[i].duration /= 2;
        }
    }

    return index;
}

/**
 * Generating an upload from data.
 * @param instance Pointer to a SubGhzProtocolEncoderKeeloq instance
 * @return true On success
 */
static bool
    subghz_protocol_encoder_keeloq_get_upload(SubGhzProtocolEncoderKeeloq* instance, uint8_t btn) {
    furi_assert(instance);

    instance->encoder.size_upload = 0;
    size_t upindex = 0;

    // if we change counter/button in SignalSettings menu then we must bypass counter_modes, just gen and save signal file.
    if(subghz_block_generic_global.cnt_need_override ||
       subghz_block_generic_global.btn_need_override)
        bypass = true;

    // Create mode7 upload only if counter and button was not changed by SignalSettings menu
    if(keeloq_counter_mode == 7 && !bypass) {
        uint16_t temp_cnt = instance->generic.cnt;
        instance->encoder.repeat = 1;
        for(uint8_t i = 7; i > 0; i--) {
            if(i == 3) {
                instance->generic.cnt = 0x0000;
                upindex = subghz_protocol_encoder_keeloq_encode_to_timings(
                    instance, (uint8_t)0x00, false, upindex);
                continue;
            } else if(i == 2) {
                instance->generic.cnt = temp_cnt;
                upindex = subghz_protocol_encoder_keeloq_encode_to_timings(
                    instance, btn, false, upindex);
                continue;
            } else if(i == 1) {
                instance->generic.cnt = temp_cnt + 1;
                upindex = subghz_protocol_encoder_keeloq_encode_to_timings(
                    instance, btn, false, upindex);
                continue;
            }
            upindex = subghz_protocol_encoder_keeloq_encode_to_timings(
                instance, (uint8_t)0x00, true, upindex);
        }
        instance->encoder.size_upload = upindex;
        return true;
    } else {
        instance->encoder.repeat = 3;
        instance->encoder.size_upload =
            subghz_protocol_encoder_keeloq_encode_to_timings(instance, btn, true, upindex);
    }

    return true;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_keeloq_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderKeeloq* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize_check_count_bit(
            &instance->generic,
            flipper_format,
            subghz_protocol_keeloq_const.min_count_bit_for_found);
        if(ret != SubGhzProtocolStatusOk) {
            break;
        }
        if(instance->generic.data_count_bit !=
           subghz_protocol_keeloq_const.min_count_bit_for_found) {
            FURI_LOG_E(TAG, "Wrong number of bits in key");
            break;
        }

        if(!subghz_protocol_keeloq_read_metadata(
               &instance->generic,
               flipper_format,
               instance->keystore,
               instance->manufacture_from_file,
               &instance->manufacture_name,
               &instance->te)) {
            ret = SubGhzProtocolStatusErrorParserOthers;
            break;
        }

        subghz_protocol_keeloq_check_remote_controller(
            &instance->generic, instance->keystore, &instance->manufacture_name);

        if(subghz_protocol_keeloq_receive_only(
               instance->manufacture_name, instance->generic.seed)) {
            FURI_LOG_W(TAG, "This variant supports receive/inspection only");
            ret = SubGhzProtocolStatusErrorEncoderGetUpload;
            break;
        }

        // Optional value
        flipper_format_read_uint32(
            flipper_format, "Repeat", (uint32_t*)&instance->encoder.repeat, 1);

        if(!subghz_protocol_encoder_keeloq_get_upload(instance, instance->generic.btn)) {
            ret = SubGhzProtocolStatusErrorEncoderGetUpload;
            break;
        }
        if(!flipper_format_rewind(flipper_format)) {
            FURI_LOG_E(TAG, "Rewind error");
            ret = SubGhzProtocolStatusErrorParserOthers;
            break;
        }
        uint8_t key_data[sizeof(uint64_t)] = {0};
        for(size_t i = 0; i < sizeof(uint64_t); i++) {
            key_data[sizeof(uint64_t) - i - 1] = (instance->generic.data >> (i * 8)) & 0xFF;
        }
        if(!flipper_format_update_hex(flipper_format, "Key", key_data, sizeof(uint64_t))) {
            FURI_LOG_E(TAG, "Unable to add Key");
            ret = SubGhzProtocolStatusErrorParserKey;
            break;
        }
        instance->encoder.front = 0; // reset before start
        instance->encoder.is_running = true;
    } while(false);

    return ret;
}

void subghz_protocol_encoder_keeloq_stop(void* context) {
    SubGhzProtocolEncoderKeeloq* instance = context;
    instance->encoder.is_running = false;
    instance->encoder.front = 0; // reset position
}

void* subghz_protocol_decoder_keeloq_alloc(SubGhzEnvironment* environment) {
    SubGhzProtocolDecoderKeeloq* instance = subghz_protocol_decoder_common_alloc(
        sizeof(SubGhzProtocolDecoderKeeloq), &subghz_protocol_keeloq);
    instance->keystore = subghz_environment_get_keystore(environment);
    instance->manufacture_from_file = furi_string_alloc();
    instance->te = subghz_protocol_keeloq_const.te_short;

    subghz_custom_btn_set_prog_mode(PROG_MODE_OFF);
    return instance;
}

void subghz_protocol_decoder_keeloq_free(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderKeeloq* instance = context;
    furi_string_free(instance->manufacture_from_file);

    free(instance);
}

void subghz_protocol_decoder_keeloq_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderKeeloq* instance = context;
    instance->decoder.parser_step = KeeloqDecoderStepReset;
    // Per-frame KeeLoq identification must not leak into the next receive session.
    instance->keystore->mfname = "";
    instance->keystore->kl_type = 0;
    instance->generic.seed = 0;

    // Drop only parser frame accumulation. Completed frames are handled synchronously
    // before the receiver is reset; decoded button and custom-button state stay intact.
    instance->generic.data = 0;
    instance->header_count = 0;
    instance->te = subghz_protocol_keeloq_const.te_short;
}

void subghz_protocol_decoder_keeloq_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderKeeloq* instance = context;
    const uint32_t te = instance->te;
    const uint32_t te_delta = subghz_protocol_keeloq_te_delta(te);

    switch(instance->decoder.parser_step) {
    case KeeloqDecoderStepReset:
        if(level && subghz_protocol_keeloq_te_preamble(duration)) {
            instance->decoder.parser_step = KeeloqDecoderStepCheckPreambula;
            instance->decoder.te_last = duration;
            instance->header_count++;
        }
        break;
    case KeeloqDecoderStepCheckPreambula:
        if(!level && subghz_protocol_keeloq_te_preamble(duration)) {
            instance->decoder.parser_step = KeeloqDecoderStepReset;
            break;
        }
        if(!level && instance->header_count > 2) {
            uint32_t locked_te = 0;
            // The sync bands overlap. A standard 400us preamble must not be
            // stolen by the fast path when its sync gap is near 2200..2450us.
            if(instance->decoder.te_last <
                   (KEELOQ_TE_FAST + subghz_protocol_keeloq_const.te_short) / 2 &&
               DURATION_DIFF(duration, KEELOQ_TE_FAST * 10) < KEELOQ_TE_FAST_GAP_DELTA) {
                locked_te = duration / 10;
            } else if(
                DURATION_DIFF(duration, subghz_protocol_keeloq_const.te_short * 10) <
                subghz_protocol_keeloq_const.te_delta * 10) {
                locked_te = subghz_protocol_keeloq_const.te_short;
            }
            if(locked_te) {
                instance->te = locked_te;
                instance->decoder.parser_step = KeeloqDecoderStepSaveDuration;
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
                break;
            }
        }
        instance->decoder.parser_step = KeeloqDecoderStepReset;
        instance->header_count = 0;
        break;
    case KeeloqDecoderStepSaveDuration:
        if(level) {
            instance->decoder.te_last = duration;
            instance->decoder.parser_step = KeeloqDecoderStepCheckDuration;
        }
        break;
    case KeeloqDecoderStepCheckDuration:
        if(!level) {
            if(duration >= te * 2 + te_delta) {
                // Found end TX
                instance->decoder.parser_step = KeeloqDecoderStepReset;
                if((instance->decoder.decode_count_bit >=
                    subghz_protocol_keeloq_const.min_count_bit_for_found) &&
                   (instance->decoder.decode_count_bit <=
                    subghz_protocol_keeloq_const.min_count_bit_for_found + 2)) {
                    if(instance->generic.data != instance->decoder.decode_data) {
                        instance->generic.data = instance->decoder.decode_data;
                        instance->generic.data_count_bit =
                            subghz_protocol_keeloq_const.min_count_bit_for_found;
                        if(instance->base.callback)
                            instance->base.callback(&instance->base, instance->base.context);
                    }
                    instance->decoder.decode_data = 0;
                    instance->decoder.decode_count_bit = 0;
                    instance->header_count = 0;
                }
                break;
            }
            const bool bit = DURATION_DIFF(instance->decoder.te_last, te) < te_delta &&
                             DURATION_DIFF(duration, te * 2) < te_delta * 2;
            // Never let an overlong frame wrap the uint8_t bit counter.
            if(instance->decoder.decode_count_bit <
                   subghz_protocol_keeloq_const.min_count_bit_for_found + 2 &&
               (bit || (DURATION_DIFF(instance->decoder.te_last, te * 2) < te_delta * 2 &&
                        DURATION_DIFF(duration, te) < te_delta))) {
                if(instance->decoder.decode_count_bit <
                   subghz_protocol_keeloq_const.min_count_bit_for_found) {
                    subghz_protocol_blocks_add_bit(&instance->decoder, bit);
                } else {
                    instance->decoder.decode_count_bit++;
                }
                instance->decoder.parser_step = KeeloqDecoderStepSaveDuration;
            } else {
                instance->decoder.parser_step = KeeloqDecoderStepReset;
                instance->header_count = 0;
            }
        } else {
            instance->decoder.parser_step = KeeloqDecoderStepReset;
            instance->header_count = 0;
        }
        break;
    }
}

/**
 * Validation of decrypt data.
 * @param instance Pointer to a SubGhzBlockGeneric instance
 * @param decrypt Decrypted data
 * @param btn Button number, 4 bit
 * @param end_serial decrement the last 10 bits of the serial number
 * @return true On success
 */
static inline bool subghz_protocol_keeloq_check_decrypt(
    SubGhzBlockGeneric* instance,
    uint32_t decrypt,
    uint8_t btn,
    uint32_t end_serial) {
    furi_assert(instance);
    if((decrypt >> 28 == btn) && (((((uint16_t)(decrypt >> 16)) & 0xFF) == end_serial) ||
                                  ((((uint16_t)(decrypt >> 16)) & 0xFF) == 0))) {
        instance->cnt = decrypt & 0x0000FFFF;
        /*FURI_LOG_I(
            "KL",
            "decrypt: 0x%08lX, btn: %d, end_serial: 0x%03lX, cnt: %ld",
            decrypt,
            btn,
            end_serial,
            instance->cnt);*/
        return true;
    }
    return false;
}
// Centurion specific check
static inline bool subghz_protocol_keeloq_check_decrypt_centurion(
    SubGhzBlockGeneric* instance,
    uint32_t decrypt,
    uint8_t btn) {
    furi_assert(instance);

    if((decrypt >> 28 == btn) && ((((uint16_t)(decrypt >> 16)) & 0x3FF) == 0x1CE)) {
        instance->cnt = decrypt & 0x0000FFFF;
        /*FURI_LOG_I(
            "KL",
            "decrypt: 0x%08lX, btn: %d, end_serial: 0x%03lX, cnt: %ld",
            decrypt,
            btn,
            end_serial,
            instance->cnt);*/
        return true;
    }
    return false;
}

// Monarch specific check
// Preserve the received variant for file inspection; no new encoder is enabled.
static inline bool subghz_protocol_keeloq_check_decrypt_monarch(
    SubGhzBlockGeneric* instance,
    uint32_t decrypt,
    uint8_t btn) {
    furi_assert(instance);

    uint16_t disc = ((uint16_t)(decrypt >> 16)) & 0xFFF;
    if((decrypt >> 28 == btn) &&
       ((disc == KEELOQ_MONARCH_DISCRIMINATOR_A) || (disc == KEELOQ_MONARCH_DISCRIMINATOR_B))) {
        instance->cnt = decrypt & 0x0000FFFF;
        instance->seed = disc;
        return true;
    }
    return false;
}

// KEY specific check - constant discriminator, same situation as Monarch
static inline bool subghz_protocol_keeloq_check_decrypt_key(
    SubGhzBlockGeneric* instance,
    uint32_t decrypt,
    uint8_t btn) {
    furi_assert(instance);

    if((decrypt >> 28 == btn) &&
       ((((uint16_t)(decrypt >> 16)) & 0xFFF) == KEELOQ_KEY_DISCRIMINATOR)) {
        instance->cnt = decrypt & 0x0000FFFF;
        return true;
    }
    return false;
}

// Pecinin specific check
static inline bool subghz_protocol_keeloq_check_decrypt_pecinin(
    SubGhzBlockGeneric* instance,
    uint32_t decrypt,
    uint16_t end_serial) {
    furi_assert(instance);

    const uint16_t serial_fragment = (decrypt >> 16) & 0x0FFFU;
    if(serial_fragment != end_serial) return false;

    instance->cnt = decrypt & 0x0000FFFFU;
    return true;
}

/** 
 * Checking the accepted code against the database manafacture key
 * @param instance Pointer to a SubGhzBlockGeneric* instance
 * @param fix Fix part of the parcel
 * @param hop Hop encrypted part of the parcel
 * @param keystore Pointer to a SubGhzKeystore* instance
 * @param manufacture_name 
 * @return uint32_t decrypt data */

static uint32_t subghz_protocol_keeloq_check_remote_controller_selector(
    SubGhzBlockGeneric* instance,
    uint32_t fix,
    uint32_t hop,
    SubGhzKeystore* keystore,
    const char** manufacture_name) {
    // protocol HCS300 uses 10 bits in discriminator, HCS200 uses 8 bits, for backward compatibility, we are looking for the 8-bit pattern
    // HCS300 -> uint16_t end_serial = (uint16_t)(fix & 0x3FF);
    // HCS200 -> uint16_t end_serial = (uint16_t)(fix & 0xFF);

    uint16_t end_serial = (uint16_t)(fix & 0xFF);
    uint8_t btn = (uint8_t)(fix >> 28);
    uint32_t decrypt = 0;
    uint64_t man;
    bool mf_not_set = false;
    // TODO:
    // if(mfname == 0x0) {
    //     mfname = "";
    // }

    const char* mfname = keystore->mfname;

    if(strcmp(mfname, "Unknown") == 0) {
        return 0;
    } else if(strcmp(mfname, "") == 0) {
        mf_not_set = true;
    }
    for
        M_EACH(manufacture_code, *subghz_keystore_get_data(keystore), SubGhzKeyArray_t) {
            if(mf_not_set || (strcmp(furi_string_get_cstr(manufacture_code->name), mfname) == 0)) {
                switch(manufacture_code->type) {
                case KEELOQ_LEARNING_SIMPLE:
                    // Simple Learning
                    decrypt = subghz_protocol_keeloq_common_decrypt(hop, manufacture_code->key);
                    if((strcmp(furi_string_get_cstr(manufacture_code->name), "Pecinin") == 0)) {
                        if(subghz_protocol_keeloq_check_decrypt_pecinin(
                               instance, decrypt, (uint16_t)(fix & 0x0FFFU))) {
                            *manufacture_name = furi_string_get_cstr(manufacture_code->name);
                            keystore->mfname = *manufacture_name;
                            // Pecinin does not transmit button code in fix
                            instance->btn = decrypt >> 28;
                            return decrypt;
                        }
                    } else if((strcmp(furi_string_get_cstr(manufacture_code->name), "KEY") == 0)) {
                        if(subghz_protocol_keeloq_check_decrypt_key(instance, decrypt, btn)) {
                            *manufacture_name = furi_string_get_cstr(manufacture_code->name);
                            keystore->mfname = *manufacture_name;
                            return decrypt;
                        }
                    } else {
                        if(subghz_protocol_keeloq_check_decrypt(
                               instance, decrypt, btn, end_serial)) {
                            *manufacture_name = furi_string_get_cstr(manufacture_code->name);
                            keystore->mfname = *manufacture_name;
                            return decrypt;
                        }
                    }
                    break;
                case KEELOQ_LEARNING_NORMAL:
                    // Normal Learning
                    // https://phreakerclub.com/forum/showpost.php?p=43557&postcount=37
                    man =
                        subghz_protocol_keeloq_common_normal_learning(fix, manufacture_code->key);
                    decrypt = subghz_protocol_keeloq_common_decrypt(hop, man);
                    if((strcmp(furi_string_get_cstr(manufacture_code->name), "Centurion") == 0)) {
                        if(subghz_protocol_keeloq_check_decrypt_centurion(instance, decrypt, btn)) {
                            *manufacture_name = furi_string_get_cstr(manufacture_code->name);
                            keystore->mfname = *manufacture_name;
                            return decrypt;
                        }
                    } else if((strcmp(furi_string_get_cstr(manufacture_code->name), "Monarch") ==
                               0)) {
                        if(subghz_protocol_keeloq_check_decrypt_monarch(instance, decrypt, btn)) {
                            *manufacture_name = furi_string_get_cstr(manufacture_code->name);
                            keystore->mfname = *manufacture_name;
                            return decrypt;
                        }
                    } else {
                        if(subghz_protocol_keeloq_check_decrypt(
                               instance, decrypt, btn, end_serial)) {
                            *manufacture_name = furi_string_get_cstr(manufacture_code->name);
                            keystore->mfname = *manufacture_name;
                            return decrypt;
                        }
                    }
                    break;
                case KEELOQ_LEARNING_SECURE:
                    bool reset_seed_back = false;
                    if((strcmp(furi_string_get_cstr(manufacture_code->name), "BFT") == 0)) {
                        // Try current seed from file if present
                        man = subghz_protocol_keeloq_common_secure_learning(
                            fix, instance->seed, manufacture_code->key);
                        decrypt = subghz_protocol_keeloq_common_decrypt(hop, man);
                        if(subghz_protocol_keeloq_check_decrypt(
                               instance, decrypt, btn, end_serial)) {
                            *manufacture_name = furi_string_get_cstr(manufacture_code->name);
                            keystore->mfname = *manufacture_name;
                            return decrypt;
                        }
                        // Try seed from serial
                        //if(instance->seed == 0) {
                        instance->seed = (fix & 0xFFFFFFF);
                        reset_seed_back = true;
                        //}
                    }
                    // Try seed from serial or zero seed
                    man = subghz_protocol_keeloq_common_secure_learning(
                        fix, instance->seed, manufacture_code->key);
                    decrypt = subghz_protocol_keeloq_common_decrypt(hop, man);
                    if(subghz_protocol_keeloq_check_decrypt(instance, decrypt, btn, end_serial)) {
                        *manufacture_name = furi_string_get_cstr(manufacture_code->name);
                        keystore->mfname = *manufacture_name;
                        return decrypt;
                    } else {
                        if(reset_seed_back) instance->seed = 0;
                        // Try with zero seed (some strange remotes have been reported to use 0 seed)
                        man = subghz_protocol_keeloq_common_secure_learning(
                            fix, instance->seed, manufacture_code->key);
                        decrypt = subghz_protocol_keeloq_common_decrypt(hop, man);
                        if(subghz_protocol_keeloq_check_decrypt(
                               instance, decrypt, btn, end_serial)) {
                            *manufacture_name = furi_string_get_cstr(manufacture_code->name);
                            keystore->mfname = *manufacture_name;
                            return decrypt;
                        }
                    }
                    break;
                case KEELOQ_LEARNING_MAGIC_XOR_TYPE_1:
                    man = subghz_protocol_keeloq_common_magic_xor_type1_learning(
                        fix, manufacture_code->key);
                    decrypt = subghz_protocol_keeloq_common_decrypt(hop, man);
                    if(subghz_protocol_keeloq_check_decrypt(instance, decrypt, btn, end_serial)) {
                        *manufacture_name = furi_string_get_cstr(manufacture_code->name);
                        keystore->mfname = *manufacture_name;
                        return decrypt;
                    }
                    break;
                case KEELOQ_LEARNING_MAGIC_SERIAL_TYPE_1:
                    man = subghz_protocol_keeloq_common_magic_serial_type1_learning(
                        fix, manufacture_code->key);
                    decrypt = subghz_protocol_keeloq_common_decrypt(hop, man);
                    if(subghz_protocol_keeloq_check_decrypt(instance, decrypt, btn, end_serial)) {
                        *manufacture_name = furi_string_get_cstr(manufacture_code->name);
                        keystore->mfname = *manufacture_name;
                        return decrypt;
                    }
                    break;
                case KEELOQ_LEARNING_MAGIC_SERIAL_TYPE_2:
                    man = subghz_protocol_keeloq_common_magic_serial_type2_learning(
                        fix, manufacture_code->key);
                    decrypt = subghz_protocol_keeloq_common_decrypt(hop, man);
                    if(subghz_protocol_keeloq_check_decrypt(instance, decrypt, btn, end_serial)) {
                        *manufacture_name = furi_string_get_cstr(manufacture_code->name);
                        keystore->mfname = *manufacture_name;
                        return decrypt;
                    }
                    break;
                case KEELOQ_LEARNING_MAGIC_SERIAL_TYPE_3:
                    man = subghz_protocol_keeloq_common_magic_serial_type3_learning(
                        fix, manufacture_code->key);
                    decrypt = subghz_protocol_keeloq_common_decrypt(hop, man);
                    if(subghz_protocol_keeloq_check_decrypt(instance, decrypt, btn, end_serial)) {
                        *manufacture_name = furi_string_get_cstr(manufacture_code->name);
                        keystore->mfname = *manufacture_name;
                        return decrypt;
                    }
                    break;
                case KEELOQ_LEARNING_AERF:
                    man = subghz_protocol_keeloq_common_learning_aerf(fix, manufacture_code->key);
                    decrypt = subghz_protocol_keeloq_common_decrypt_derived(hop, man, 0x240u);
                    if(subghz_protocol_keeloq_check_decrypt(instance, decrypt, btn, end_serial)) {
                        *manufacture_name = furi_string_get_cstr(manufacture_code->name);
                        keystore->mfname = *manufacture_name;
                        return decrypt;
                    }
                    decrypt = subghz_protocol_keeloq_common_decrypt_derived(hop, man, 0x210u);
                    if(subghz_protocol_keeloq_check_decrypt(instance, decrypt, btn, end_serial)) {
                        *manufacture_name = furi_string_get_cstr(manufacture_code->name);
                        keystore->mfname = *manufacture_name;
                        return decrypt;
                    }
                    decrypt = subghz_protocol_keeloq_common_decrypt(hop, man);
                    if(subghz_protocol_keeloq_check_decrypt(instance, decrypt, btn, end_serial)) {
                        *manufacture_name = furi_string_get_cstr(manufacture_code->name);
                        keystore->mfname = *manufacture_name;
                        return decrypt;
                    }
                    break;
                case KEELOQ_LEARNING_ERREKA:
                    man = subghz_protocol_keeloq_common_learning_erreka(
                        fix, instance->seed, manufacture_code->key);
                    decrypt = subghz_protocol_keeloq_common_decrypt(hop, man);
                    if(subghz_protocol_keeloq_check_decrypt(instance, decrypt, btn, end_serial)) {
                        *manufacture_name = furi_string_get_cstr(manufacture_code->name);
                        keystore->mfname = *manufacture_name;
                        return decrypt;
                    }
                    break;
                case KEELOQ_LEARNING_PUJOL:
                    man = subghz_protocol_keeloq_common_learning_pujol(fix, manufacture_code->key);
                    decrypt = subghz_protocol_keeloq_common_decrypt(hop, man);
                    if(subghz_protocol_keeloq_check_decrypt(instance, decrypt, btn, end_serial)) {
                        *manufacture_name = furi_string_get_cstr(manufacture_code->name);
                        keystore->mfname = *manufacture_name;
                        return decrypt;
                    }
                    break;
                case KEELOQ_LEARNING_SIMPLE_JCM:
                    // Simple Learning 8 bit serial
                    decrypt = subghz_protocol_keeloq_common_decrypt(hop, manufacture_code->key);
                    if(subghz_protocol_keeloq_check_decrypt(instance, decrypt, btn, end_serial)) {
                        *manufacture_name = furi_string_get_cstr(manufacture_code->name);
                        keystore->mfname = *manufacture_name;
                        return decrypt;
                    }
                    break;
                case KEELOQ_LEARNING_JCM_GEN2:
                    // The second input byte is not carried in the packet, brute force it.
                    for(uint32_t jcm_seed = 0; jcm_seed < 0x100u; jcm_seed++) {
                        man = subghz_protocol_keeloq_common_learning_jcm_gen2(
                            fix, (uint8_t)jcm_seed, manufacture_code->key);
                        decrypt = subghz_protocol_keeloq_common_decrypt(hop, man);
                        if(subghz_protocol_keeloq_check_decrypt(
                               instance, decrypt, btn, end_serial)) {
                            *manufacture_name = furi_string_get_cstr(manufacture_code->name);
                            keystore->mfname = *manufacture_name;
                            instance->seed = jcm_seed;
                            return decrypt;
                        }
                    }
                    break;
                case KEELOQ_LEARNING_STAGNOLI:
                    man = subghz_protocol_keeloq_common_learning_stagnoli(
                        fix, manufacture_code->key);
                    decrypt = subghz_protocol_keeloq_common_decrypt(hop, man);
                    if(subghz_protocol_keeloq_check_decrypt(instance, decrypt, btn, end_serial)) {
                        *manufacture_name = furi_string_get_cstr(manufacture_code->name);
                        keystore->mfname = *manufacture_name;
                        return decrypt;
                    }
                    break;
                case KEELOQ_LEARNING_TELCOMA_TABLE_HI: {
                    uint32_t telcoma_table[4] = {0};
                    if(!subghz_protocol_keeloq_common_get_telcoma_table(keystore, telcoma_table))
                        break;
                    man = subghz_protocol_keeloq_common_learning_telcoma_table(fix, telcoma_table);
                    decrypt = subghz_protocol_keeloq_common_decrypt(hop, man);
                    if(subghz_protocol_keeloq_check_decrypt(instance, decrypt, btn, end_serial)) {
                        *manufacture_name = furi_string_get_cstr(manufacture_code->name);
                        keystore->mfname = *manufacture_name;
                        return decrypt;
                    }
                    break;
                }
                case KEELOQ_LEARNING_UNKNOWN:
                    // Simple Learning
                    decrypt = subghz_protocol_keeloq_common_decrypt(hop, manufacture_code->key);
                    if(subghz_protocol_keeloq_check_decrypt(instance, decrypt, btn, end_serial)) {
                        *manufacture_name = furi_string_get_cstr(manufacture_code->name);
                        keystore->mfname = *manufacture_name;
                        keystore->kl_type = 1;
                        return decrypt;
                    }

                    // Check for mirrored man
                    uint64_t man_rev = 0;
                    uint64_t man_rev_byte = 0;
                    for(uint8_t i = 0; i < 64; i += 8) {
                        man_rev_byte = (uint8_t)(manufacture_code->key >> i);
                        man_rev = man_rev | man_rev_byte << (56 - i);
                    }

                    decrypt = subghz_protocol_keeloq_common_decrypt(hop, man_rev);
                    if(subghz_protocol_keeloq_check_decrypt(instance, decrypt, btn, end_serial)) {
                        *manufacture_name = furi_string_get_cstr(manufacture_code->name);
                        keystore->mfname = *manufacture_name;
                        keystore->kl_type = 1;
                        return decrypt;
                    }

                    //###########################
                    // Normal Learning
                    // https://phreakerclub.com/forum/showpost.php?p=43557&postcount=37
                    man =
                        subghz_protocol_keeloq_common_normal_learning(fix, manufacture_code->key);
                    decrypt = subghz_protocol_keeloq_common_decrypt(hop, man);
                    if(subghz_protocol_keeloq_check_decrypt(instance, decrypt, btn, end_serial)) {
                        *manufacture_name = furi_string_get_cstr(manufacture_code->name);
                        keystore->mfname = *manufacture_name;
                        keystore->kl_type = 2;
                        return decrypt;
                    }

                    // Check for mirrored man
                    man = subghz_protocol_keeloq_common_normal_learning(fix, man_rev);
                    decrypt = subghz_protocol_keeloq_common_decrypt(hop, man);
                    if(subghz_protocol_keeloq_check_decrypt(instance, decrypt, btn, end_serial)) {
                        *manufacture_name = furi_string_get_cstr(manufacture_code->name);
                        keystore->mfname = *manufacture_name;
                        keystore->kl_type = 2;
                        return decrypt;
                    }

                    // Secure Learning
                    man = subghz_protocol_keeloq_common_secure_learning(
                        fix, instance->seed, manufacture_code->key);
                    decrypt = subghz_protocol_keeloq_common_decrypt(hop, man);
                    if(subghz_protocol_keeloq_check_decrypt(instance, decrypt, btn, end_serial)) {
                        *manufacture_name = furi_string_get_cstr(manufacture_code->name);
                        keystore->mfname = *manufacture_name;
                        keystore->kl_type = 3;
                        return decrypt;
                    }

                    // Check for mirrored man
                    man = subghz_protocol_keeloq_common_secure_learning(
                        fix, instance->seed, man_rev);
                    decrypt = subghz_protocol_keeloq_common_decrypt(hop, man);
                    if(subghz_protocol_keeloq_check_decrypt(instance, decrypt, btn, end_serial)) {
                        *manufacture_name = furi_string_get_cstr(manufacture_code->name);
                        keystore->mfname = *manufacture_name;
                        keystore->kl_type = 3;
                        return decrypt;
                    }

                    // Magic xor type1 learning
                    man = subghz_protocol_keeloq_common_magic_xor_type1_learning(
                        fix, manufacture_code->key);
                    decrypt = subghz_protocol_keeloq_common_decrypt(hop, man);
                    if(subghz_protocol_keeloq_check_decrypt(instance, decrypt, btn, end_serial)) {
                        *manufacture_name = furi_string_get_cstr(manufacture_code->name);
                        keystore->mfname = *manufacture_name;
                        keystore->kl_type = 4;
                        return decrypt;
                    }

                    // Check for mirrored man
                    man = subghz_protocol_keeloq_common_magic_xor_type1_learning(fix, man_rev);
                    decrypt = subghz_protocol_keeloq_common_decrypt(hop, man);
                    if(subghz_protocol_keeloq_check_decrypt(instance, decrypt, btn, end_serial)) {
                        *manufacture_name = furi_string_get_cstr(manufacture_code->name);
                        keystore->mfname = *manufacture_name;
                        keystore->kl_type = 4;
                        return decrypt;
                    }

                    break;
                }
            }
        }

    // MF not found
    *manufacture_name = "Unknown";
    keystore->mfname = "Unknown";
    instance->cnt = 0;

    return 0;
}

static uint32_t subghz_protocol_keeloq_check_remote_controller(
    SubGhzBlockGeneric* instance,
    SubGhzKeystore* keystore,
    const char** manufacture_name) {
    // Reverse key, split FIX and HOP parts
    uint64_t key = subghz_protocol_blocks_reverse_key(instance->data, instance->data_count_bit);
    uint32_t key_fix = key >> 32;
    uint32_t key_hop = key & 0x00000000ffffffff;
    static uint16_t temp_counter = 0; // Be careful with prog_mode
    uint32_t resdecrypt = 0;

    // If we are in BFT / Aprimatic / Dea_Mio programming mode we will set previous remembered counter and skip mf keys check
    ProgMode prog_mode = subghz_custom_btn_get_prog_mode();
    if(prog_mode == PROG_MODE_OFF) {
        if(keystore->mfname == 0x0) {
            keystore->mfname = "";
        }
        if(*manufacture_name == 0x0) {
            *manufacture_name = "";
        }

        // Case when we have no mf name means that we are checking for the first time and we have to check all conditions
        if(strlen(keystore->mfname) < 1) {
            // Check key AN-Motors
            if((key_hop >> 24) == ((key_hop >> 16) & 0x00ff) &&
               (key_fix >> 28) == ((key_hop >> 12) & 0x0f) && (key_hop & 0xFFF) == 0x404) {
                *manufacture_name = "AN-Motors";
                keystore->mfname = *manufacture_name;
                instance->cnt = key_hop >> 16;
            } else if((key_hop & 0xFFF) == (0x000) && (key_fix >> 28) == ((key_hop >> 12) & 0x0f)) {
                *manufacture_name = "HCS101";
                keystore->mfname = *manufacture_name;
                instance->cnt = key_hop >> 16;
            } else {
                resdecrypt = subghz_protocol_keeloq_check_remote_controller_selector(
                    instance, key_fix, key_hop, keystore, manufacture_name);
            }
        } else {
            // If we have mfname and its one of AN-Motors or HCS101 we should preform only check for this system
            if(strcmp(keystore->mfname, "AN-Motors") == 0) {
                // Force key to AN-Motors
                *manufacture_name = "AN-Motors";
                keystore->mfname = *manufacture_name;
                instance->cnt = key_hop >> 16;
            } else if(strcmp(keystore->mfname, "HCS101") == 0) {
                // Force key to HCS101
                *manufacture_name = "HCS101";
                keystore->mfname = *manufacture_name;
                instance->cnt = key_hop >> 16;
            } else {
                // Else we have mfname that is not AN-Motors or HCS101 we should check it via default selector
                resdecrypt = subghz_protocol_keeloq_check_remote_controller_selector(
                    instance, key_fix, key_hop, keystore, manufacture_name);
            }
        }
        // Save original counter as temp counter in case of later usage of prog mode
        temp_counter = instance->cnt;

    } else if(prog_mode == PROG_MODE_KEELOQ_BFT) {
        // When we are in prog mode we should fix mfname and apply temp counter
        *manufacture_name = "BFT";
        keystore->mfname = *manufacture_name;
        instance->cnt = temp_counter;
    } else if(prog_mode == PROG_MODE_KEELOQ_APRIMATIC) {
        // When we are in prog mode we should fix mfname and apply temp counter
        *manufacture_name = "Aprimatic";
        keystore->mfname = *manufacture_name;
        instance->cnt = temp_counter;
    } else if(prog_mode == PROG_MODE_KEELOQ_DEA_MIO) {
        // When we are in prog mode we should fix mfname and apply temp counter
        *manufacture_name = "Dea_Mio";
        keystore->mfname = *manufacture_name;
        instance->cnt = temp_counter;
    } else {
        // Counter protection
        furi_crash("Unsupported Prog Mode");
    }

    // Get serial and button code from FIX part of the key
    instance->serial = key_fix & 0x0FFFFFFF;
    if(strcmp(*manufacture_name, "Pecinin") != 0) {
        instance->btn = key_fix >> 28;
    }

    // Save original button for later use
    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(instance->btn);
    }
    // Set max custom buttons
    subghz_custom_btn_set_max(4);

    return resdecrypt;
}

SubGhzProtocolStatus subghz_protocol_decoder_keeloq_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderKeeloq* instance = context;

    SubGhzProtocolStatus res =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);

    subghz_protocol_keeloq_check_remote_controller(
        &instance->generic, instance->keystore, &instance->manufacture_name);

    // Preserve the Erreka seed and Monarch discriminator when saving a loaded capture.
    if((strcmp(instance->manufacture_name, "BFT") == 0) ||
       (strcmp(instance->manufacture_name, "Erreka") == 0) ||
       (strcmp(instance->manufacture_name, "Monarch") == 0)) {
        uint8_t seed_data[sizeof(uint32_t)] = {0};
        for(size_t i = 0; i < sizeof(uint32_t); i++) {
            seed_data[sizeof(uint32_t) - i - 1] = (instance->generic.seed >> i * 8) & 0xFF;
        }
        if((res == SubGhzProtocolStatusOk) &&
           !flipper_format_write_hex(flipper_format, "Seed", seed_data, sizeof(uint32_t))) {
            FURI_LOG_E(TAG, "DECODER Serialize: Unable to add Seed");
            res = SubGhzProtocolStatusError;
        }
        instance->generic.seed = seed_data[0] << 24 | seed_data[1] << 16 | seed_data[2] << 8 |
                                 seed_data[3];
    }

    if((res == SubGhzProtocolStatusOk) &&
       !flipper_format_write_string_cstr(
           flipper_format, "Manufacture", instance->manufacture_name)) {
        FURI_LOG_E(TAG, "DECODER Serialize: Unable to add manufacture name");
        res = SubGhzProtocolStatusError;
    }
    // Legacy 400us captures keep their existing layout. Only a fast capture
    // needs the nominal TE field, including after a load/save round trip.
    uint32_t te = subghz_protocol_keeloq_te_nominal(instance->te);
    if(res == SubGhzProtocolStatusOk && te == KEELOQ_TE_FAST &&
       !flipper_format_write_uint32(flipper_format, "TE", &te, 1)) {
        res = SubGhzProtocolStatusError;
    }
    return res;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_keeloq_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderKeeloq* instance = context;
    SubGhzProtocolStatus res = SubGhzProtocolStatusError;
    do {
        if(SubGhzProtocolStatusOk !=
           subghz_block_generic_deserialize(&instance->generic, flipper_format)) {
            FURI_LOG_E(TAG, "Deserialize error");
            break;
        }
        if(instance->generic.data_count_bit !=
           subghz_protocol_keeloq_const.min_count_bit_for_found) {
            FURI_LOG_E(TAG, "Wrong number of bits in key");
            break;
        }

        if(!subghz_protocol_keeloq_read_metadata(
               &instance->generic,
               flipper_format,
               instance->keystore,
               instance->manufacture_from_file,
               &instance->manufacture_name,
               &instance->te)) {
            break;
        }

        res = SubGhzProtocolStatusOk;
    } while(false);

    return res;
}

static uint8_t subghz_protocol_keeloq_get_btn_code(uint8_t last_btn_code) {
    const uint8_t custom_btn_id = subghz_custom_btn_get();
    const uint8_t original = subghz_custom_btn_get_original();
    if(last_btn_code == 0) {
        last_btn_code = 0xA;
    }

    // Same mapping for every uint8_t original/direction/fallback value; the
    // exhaustive frozen-vector test also includes unknown codes and zero.
    switch(custom_btn_id) {
    case SUBGHZ_CUSTOM_BTN_UP:
        return original == 0x1 || original == 0x9 || original == 0x6 ? 0x2 : 0x1;
    case SUBGHZ_CUSTOM_BTN_DOWN:
        return original == 0x4 ? last_btn_code : 0x4;
    case SUBGHZ_CUSTOM_BTN_LEFT:
        if(original == 0x8) return 0x2;
        if(original == 0x9) return 0x6;
        if(original == 0x6) return 0x9;
        return 0x8;
    case SUBGHZ_CUSTOM_BTN_RIGHT:
        return original == 0xA || original == 0x4 || original == 0xF ? 0x2 : last_btn_code;
    default:
        return original;
    }
}

void subghz_protocol_decoder_keeloq_get_string(void* context, FuriString* output) {
    furi_assert(context);
    SubGhzProtocolDecoderKeeloq* instance = context;

    uint32_t hopdecrypt = 0;
    // Try to get decrypt for display if mf is known, if not it will be 0 and display will be without decrypt part since it might come already decrypted like in HCS101 or AN-Motors
    // Or we might have Unknown MF
    hopdecrypt = subghz_protocol_keeloq_check_remote_controller(
        &instance->generic, instance->keystore, &instance->manufacture_name);

    uint32_t code_found_hi = instance->generic.data >> 32;
    uint32_t code_found_lo = instance->generic.data & 0x00000000ffffffff;

    uint64_t code_found_reverse = subghz_protocol_blocks_reverse_key(
        instance->generic.data, instance->generic.data_count_bit);
    uint32_t code_found_reverse_hi = code_found_reverse >> 32;
    uint32_t code_found_reverse_lo = code_found_reverse & 0x00000000ffffffff;

    // Allow button edit
    subghz_block_generic_global.btn_is_available = true;
    subghz_block_generic_global.current_btn = instance->generic.btn;
    subghz_block_generic_global.btn_length_bit = 4;

    if(strcmp(instance->manufacture_name, "AN-Motors") == 0) {
        // No counter only pseudo counter
        subghz_block_generic_global.cnt_is_available = false;
        furi_string_cat_printf(
            output,
            "%s %dbit\r\n"
            "Key:%08lX%08lX\r\n"
            "Fix:0x%08lX PsCn:%04lX\r\n"
            "Hop:0x%08lX    Btn:%01X\r\n"
            "MF:%s",
            instance->generic.protocol_name,
            instance->generic.data_count_bit,
            code_found_hi,
            code_found_lo,
            code_found_reverse_hi,
            instance->generic.cnt,
            code_found_reverse_lo,
            instance->generic.btn,
            instance->manufacture_name);
    } else if(strcmp(instance->manufacture_name, "HCS101") == 0) {
        // Counter is present but not encrypted
        subghz_block_generic_global.cnt_is_available = true;
        subghz_block_generic_global.cnt_length_bit = 16;
        subghz_block_generic_global.current_cnt = instance->generic.cnt;
        furi_string_cat_printf(
            output,
            "%s %dbit\r\n"
            "Key:%08lX%08lX\r\n"
            "Fix:0x%08lX    Cnt:%04lX\r\n"
            "Hop:0x%08lX    Btn:%01X\r\n"
            "MF:%s",
            instance->generic.protocol_name,
            instance->generic.data_count_bit,
            code_found_hi,
            code_found_lo,
            code_found_reverse_hi,
            instance->generic.cnt,
            code_found_reverse_lo,
            instance->generic.btn,
            instance->manufacture_name);
    } else if(
        (strcmp(instance->manufacture_name, "BFT") == 0) ||
        (strcmp(instance->manufacture_name, "Erreka") == 0)) {
        // Allow counter edit
        subghz_block_generic_global.cnt_is_available = true;
        subghz_block_generic_global.cnt_length_bit = 16;
        subghz_block_generic_global.current_cnt = instance->generic.cnt;

        ProgMode prog_mode = subghz_custom_btn_get_prog_mode();
        if(prog_mode == PROG_MODE_KEELOQ_BFT) {
            furi_string_cat_printf(
                output,
                "%s %dbit\r\n"
                "Key:%08lX%08lX\r\n"
                "Fix:0x%08lX    Cnt:%04lX\r\n"
                "Hop:0x%08lX    Btn:%01X\r\n"
                "MF:%s PRG Sd:%08lX",
                instance->generic.protocol_name,
                instance->generic.data_count_bit,
                code_found_hi,
                code_found_lo,
                code_found_reverse_hi,
                instance->generic.cnt,
                code_found_reverse_lo,
                instance->generic.btn,
                instance->manufacture_name,
                instance->generic.seed);
        } else {
            furi_string_cat_printf(
                output,
                "%s %dbit\r\n"
                "Key:%08lX%08lX\r\n"
                "Fix:0x%08lX    Cnt:%04lX\r\n"
                "Hop:0x%08lX    Btn:%01X\r\n"
                "MF:%s Sd:%08lX",
                instance->generic.protocol_name,
                instance->generic.data_count_bit,
                code_found_hi,
                code_found_lo,
                code_found_reverse_hi,
                instance->generic.cnt,
                hopdecrypt,
                instance->generic.btn,
                instance->manufacture_name,
                instance->generic.seed);
        }
    } else if(strcmp(instance->manufacture_name, "Unknown") == 0) {
        // No counter info with unknown MF
        instance->generic.cnt = 0x0;
        subghz_block_generic_global.cnt_is_available = false;
        furi_string_cat_printf(
            output,
            "%s %dbit\r\n"
            "Key:%08lX%08lX\r\n"
            "Fix:0x%08lX    Cnt:????\r\n"
            "Hop:0x%08lX    Btn:%01X\r\n"
            "MF:%s",
            instance->generic.protocol_name,
            instance->generic.data_count_bit,
            code_found_hi,
            code_found_lo,
            code_found_reverse_hi,
            code_found_reverse_lo,
            instance->generic.btn,
            instance->manufacture_name);
    } else {
        // All other known MF with counter info, allow counter edit
        subghz_block_generic_global.cnt_is_available = true;
        subghz_block_generic_global.cnt_length_bit = 16;
        subghz_block_generic_global.current_cnt = instance->generic.cnt;
        furi_string_cat_printf(
            output,
            "%s %dbit\r\n"
            "Key:%08lX%08lX\r\n"
            "Fix:0x%08lX    Cnt:%04lX\r\n"
            "Hop:0x%08lX    Btn:%01X\r\n"
            "MF:%s",
            instance->generic.protocol_name,
            instance->generic.data_count_bit,
            code_found_hi,
            code_found_lo,
            code_found_reverse_hi,
            instance->generic.cnt,
            hopdecrypt,
            instance->generic.btn,
            instance->manufacture_name);
    }
}
