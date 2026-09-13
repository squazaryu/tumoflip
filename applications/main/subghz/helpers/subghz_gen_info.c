#include "subghz_gen_info.h"
#include "../helpers/subghz_txrx_create_protocol_key.h"
#include <lib/subghz/protocols/protocol_items.h>
#include <lib/subghz/blocks/math.h>

typedef struct {
    uint8_t mask;
    uint8_t prefix;
    uint8_t frequency;
    uint8_t modulation;
    uint8_t button;
    uint16_t counter;
    const char* manufacturer;
} SubGhzKeeloqGenerationPreset;

static const uint32_t subghz_keeloq_serial_masks[] = {
    0x000FFF00,
    0x00FFFFFF,
    0x00FFFF00,
    0x0000FFFF,
    0x000FFFFF,
    0x0FFFFFFF,
    0x00FFFFF0,
    0x0FFFF000,
};

static const uint32_t subghz_keeloq_serial_prefixes[] = {
    0x00800080,
    0x00000000,
    0x01000011,
    0x02000000,
    0x04700000,
    0x00600000,
    0x01700000,
    0x00000869,
    0x018F0000,
    0x02200000,
    0x00100000,
};

static const uint32_t subghz_keeloq_frequencies[] = {
    433920000,
    868350000,
    868460000,
    434420000,
    868800000,
    315000000,
};

static const char* const subghz_keeloq_modulations[] = {
    "AM650",
    "FM476",
    "FM12K",
};

enum {
    KEELOQ_MASK_12 = 0,
    KEELOQ_MASK_24 = 1,
    KEELOQ_MASK_24_NO_LOW_BYTE = 2,
    KEELOQ_MASK_16 = 3,
    KEELOQ_MASK_20 = 4,
    KEELOQ_MASK_28 = 5,
    KEELOQ_MASK_24_ALIGNED = 6,
    KEELOQ_MASK_DEA_MIO = 7,
};

enum {
    KEELOQ_PREFIX_BENINCA = 0,
    KEELOQ_PREFIX_NONE = 1,
    KEELOQ_PREFIX_ALLMATIC = 2,
    KEELOQ_PREFIX_ELMES = 3,
    KEELOQ_PREFIX_AN_MOTORS = 4,
    KEELOQ_PREFIX_APRIMATIC = 5,
    KEELOQ_PREFIX_SOMMER = 6,
    KEELOQ_PREFIX_DEA_MIO = 7,
    KEELOQ_PREFIX_NOVOFERM = 8,
    KEELOQ_PREFIX_ECOSTAR = 9,
    KEELOQ_PREFIX_FAAC = 10,
};

enum {
    KEELOQ_FREQUENCY_433_920 = 0,
    KEELOQ_FREQUENCY_868_350 = 1,
    KEELOQ_FREQUENCY_868_460 = 2,
    KEELOQ_FREQUENCY_434_420 = 3,
    KEELOQ_FREQUENCY_868_800 = 4,
    KEELOQ_FREQUENCY_315_000 = 5,
};

enum {
    KEELOQ_MODULATION_AM650 = 0,
    KEELOQ_MODULATION_FM476 = 1,
    KEELOQ_MODULATION_FM12K = 2,
};

#define SUBGHZ_KEELOQ_PRESET( \
    mask_index, \
    prefix_index, \
    frequency_index, \
    modulation_index, \
    button_code, \
    counter_value, \
    manufacturer_name) \
    { \
        .mask = (mask_index), \
        .prefix = (prefix_index), \
        .frequency = (frequency_index), \
        .modulation = (modulation_index), \
        .button = (button_code), \
        .counter = (counter_value), \
        .manufacturer = (manufacturer_name), \
    }

static const SubGhzKeeloqGenerationPreset subghz_keeloq_generation_presets[] = {
    // Beninca and Comunello
    [0] = SUBGHZ_KEELOQ_PRESET(KEELOQ_MASK_12, KEELOQ_PREFIX_BENINCA, KEELOQ_FREQUENCY_433_920, KEELOQ_MODULATION_AM650, 0x01, 0x05, "Beninca"),
    [1] = SUBGHZ_KEELOQ_PRESET(KEELOQ_MASK_12, KEELOQ_PREFIX_BENINCA, KEELOQ_FREQUENCY_868_350, KEELOQ_MODULATION_AM650, 0x01, 0x05, "Beninca"),
    [2] = SUBGHZ_KEELOQ_PRESET(KEELOQ_MASK_24, KEELOQ_PREFIX_NONE, KEELOQ_FREQUENCY_433_920, KEELOQ_MODULATION_AM650, 0x08, 0x05, "Comunello"),
    [3] = SUBGHZ_KEELOQ_PRESET(KEELOQ_MASK_24, KEELOQ_PREFIX_NONE, KEELOQ_FREQUENCY_868_460, KEELOQ_MODULATION_AM650, 0x08, 0x05, "Comunello"),
    [4] = SUBGHZ_KEELOQ_PRESET(KEELOQ_MASK_24_NO_LOW_BYTE, KEELOQ_PREFIX_ALLMATIC, KEELOQ_FREQUENCY_433_920, KEELOQ_MODULATION_AM650, 0x0C, 0x05, "Beninca"),
    [5] = SUBGHZ_KEELOQ_PRESET(KEELOQ_MASK_24_NO_LOW_BYTE, KEELOQ_PREFIX_ALLMATIC, KEELOQ_FREQUENCY_868_350, KEELOQ_MODULATION_AM650, 0x0C, 0x05, "Beninca"),

    // Centurion through Stilmatic
    [6] = SUBGHZ_KEELOQ_PRESET(KEELOQ_MASK_16, KEELOQ_PREFIX_NONE, KEELOQ_FREQUENCY_433_920, KEELOQ_MODULATION_AM650, 0x02, 0x03, "Centurion"),
    [7] = SUBGHZ_KEELOQ_PRESET(KEELOQ_MASK_16, KEELOQ_PREFIX_NONE, KEELOQ_FREQUENCY_433_920, KEELOQ_MODULATION_AM650, 0x0A, 0x03, "Monarch"),
    [8] = SUBGHZ_KEELOQ_PRESET(KEELOQ_MASK_20, KEELOQ_PREFIX_NONE, KEELOQ_FREQUENCY_433_920, KEELOQ_MODULATION_AM650, 0x02, 0x03, "Jolly_Motors"),
    [9] = SUBGHZ_KEELOQ_PRESET(KEELOQ_MASK_24, KEELOQ_PREFIX_ELMES, KEELOQ_FREQUENCY_433_920, KEELOQ_MODULATION_AM650, 0x02, 0x03, "Elmes_Poland"),
    [10] = SUBGHZ_KEELOQ_PRESET(KEELOQ_MASK_20, KEELOQ_PREFIX_AN_MOTORS, KEELOQ_FREQUENCY_433_920, KEELOQ_MODULATION_AM650, 0x02, 0x21, "AN-Motors"),
    [11] = SUBGHZ_KEELOQ_PRESET(KEELOQ_MASK_20, KEELOQ_PREFIX_APRIMATIC, KEELOQ_FREQUENCY_433_920, KEELOQ_MODULATION_AM650, 0x08, 0x03, "Aprimatic"),
    [12] = SUBGHZ_KEELOQ_PRESET(KEELOQ_MASK_24, KEELOQ_PREFIX_NONE, KEELOQ_FREQUENCY_433_920, KEELOQ_MODULATION_AM650, 0x02, 0x03, "Gibidi"),
    [13] = SUBGHZ_KEELOQ_PRESET(KEELOQ_MASK_28, KEELOQ_PREFIX_NONE, KEELOQ_FREQUENCY_433_920, KEELOQ_MODULATION_AM650, 0x02, 0x03, "GSN"),
    [14] = SUBGHZ_KEELOQ_PRESET(KEELOQ_MASK_24_ALIGNED, KEELOQ_PREFIX_NONE, KEELOQ_FREQUENCY_433_920, KEELOQ_MODULATION_AM650, 0x04, 0x05, "IronLogic"),
    [15] = SUBGHZ_KEELOQ_PRESET(KEELOQ_MASK_24_ALIGNED, KEELOQ_PREFIX_NONE, KEELOQ_FREQUENCY_433_920, KEELOQ_MODULATION_AM650, 0x04, 0x05, "IL-100(Smart)"),
    [16] = SUBGHZ_KEELOQ_PRESET(KEELOQ_MASK_28, KEELOQ_PREFIX_NONE, KEELOQ_FREQUENCY_433_920, KEELOQ_MODULATION_AM650, 0x01, 0x03, "Stilmatic"),

    // Sommer, DTM Neo and Came Space
    [17] = SUBGHZ_KEELOQ_PRESET(KEELOQ_MASK_16, KEELOQ_PREFIX_SOMMER, KEELOQ_FREQUENCY_434_420, KEELOQ_MODULATION_FM476, 0x02, 0x03, "Sommer"),
    [18] = SUBGHZ_KEELOQ_PRESET(KEELOQ_MASK_16, KEELOQ_PREFIX_SOMMER, KEELOQ_FREQUENCY_868_800, KEELOQ_MODULATION_FM476, 0x02, 0x03, "Sommer"),
    [19] = SUBGHZ_KEELOQ_PRESET(KEELOQ_MASK_16, KEELOQ_PREFIX_NONE, KEELOQ_FREQUENCY_434_420, KEELOQ_MODULATION_FM12K, 0x02, 0x03, "Sommer"),
    [20] = SUBGHZ_KEELOQ_PRESET(KEELOQ_MASK_16, KEELOQ_PREFIX_NONE, KEELOQ_FREQUENCY_868_800, KEELOQ_MODULATION_FM12K, 0x02, 0x03, "Sommer"),
    [21] = SUBGHZ_KEELOQ_PRESET(KEELOQ_MASK_20, KEELOQ_PREFIX_NONE, KEELOQ_FREQUENCY_433_920, KEELOQ_MODULATION_AM650, 0x02, 0x05, "DTM_Neo"),
    [22] = SUBGHZ_KEELOQ_PRESET(KEELOQ_MASK_24, KEELOQ_PREFIX_NONE, KEELOQ_FREQUENCY_433_920, KEELOQ_MODULATION_AM650, 0x04, 0x03, "Came_Space"),

    // Remaining Keeloq generation presets
    [23] = SUBGHZ_KEELOQ_PRESET(KEELOQ_MASK_28, KEELOQ_PREFIX_NONE, KEELOQ_FREQUENCY_433_920, KEELOQ_MODULATION_AM650, 0x01, 0x03, "Motorline"),
    [24] = SUBGHZ_KEELOQ_PRESET(KEELOQ_MASK_28, KEELOQ_PREFIX_NONE, KEELOQ_FREQUENCY_433_920, KEELOQ_MODULATION_AM650, 0x02, 0x03, "DoorHan"),
    [25] = SUBGHZ_KEELOQ_PRESET(KEELOQ_MASK_28, KEELOQ_PREFIX_NONE, KEELOQ_FREQUENCY_315_000, KEELOQ_MODULATION_AM650, 0x02, 0x03, "DoorHan"),
    [26] = SUBGHZ_KEELOQ_PRESET(KEELOQ_MASK_24, KEELOQ_PREFIX_NONE, KEELOQ_FREQUENCY_433_920, KEELOQ_MODULATION_AM650, 0x02, 0x03, "NICE_Smilo"),
    [27] = SUBGHZ_KEELOQ_PRESET(KEELOQ_MASK_24, KEELOQ_PREFIX_NONE, KEELOQ_FREQUENCY_433_920, KEELOQ_MODULATION_AM650, 0x09, 0x03, "NICE_MHOUSE"),
    [28] = SUBGHZ_KEELOQ_PRESET(KEELOQ_MASK_DEA_MIO, KEELOQ_PREFIX_DEA_MIO, KEELOQ_FREQUENCY_433_920, KEELOQ_MODULATION_AM650, 0x02, 0x03, "Dea_Mio"),
    [29] = SUBGHZ_KEELOQ_PRESET(KEELOQ_MASK_24, KEELOQ_PREFIX_NONE, KEELOQ_FREQUENCY_433_920, KEELOQ_MODULATION_AM650, 0x09, 0x03, "Genius_Bravo"),
    [30] = SUBGHZ_KEELOQ_PRESET(KEELOQ_MASK_24, KEELOQ_PREFIX_NONE, KEELOQ_FREQUENCY_433_920, KEELOQ_MODULATION_AM650, 0x02, 0x03, "JCM_Tech"),
    [31] = SUBGHZ_KEELOQ_PRESET(KEELOQ_MASK_16, KEELOQ_PREFIX_NOVOFERM, KEELOQ_FREQUENCY_433_920, KEELOQ_MODULATION_AM650, 0x01, 0x03, "Novoferm"),
    [32] = SUBGHZ_KEELOQ_PRESET(KEELOQ_MASK_20, KEELOQ_PREFIX_ECOSTAR, KEELOQ_FREQUENCY_433_920, KEELOQ_MODULATION_AM650, 0x04, 0x03, "EcoStar"),
    [33] = SUBGHZ_KEELOQ_PRESET(KEELOQ_MASK_20, KEELOQ_PREFIX_NONE, KEELOQ_FREQUENCY_433_920, KEELOQ_MODULATION_FM12K, 0x02, 0x03, "Cardin_S449"),
    [34] = SUBGHZ_KEELOQ_PRESET(KEELOQ_MASK_24, KEELOQ_PREFIX_NONE, KEELOQ_FREQUENCY_433_920, KEELOQ_MODULATION_AM650, 0x02, 0x03, "Pujol"),
    [35] = SUBGHZ_KEELOQ_PRESET(KEELOQ_MASK_24, KEELOQ_PREFIX_NONE, KEELOQ_FREQUENCY_433_920, KEELOQ_MODULATION_AM650, 0x04, 0x03, "ET_Blue"),
    [36] = SUBGHZ_KEELOQ_PRESET(KEELOQ_MASK_24, KEELOQ_PREFIX_NONE, KEELOQ_FREQUENCY_433_920, KEELOQ_MODULATION_AM650, 0x04, 0x03, "ET_Blue_Mix"),
    [37] = SUBGHZ_KEELOQ_PRESET(KEELOQ_MASK_24, KEELOQ_PREFIX_NONE, KEELOQ_FREQUENCY_433_920, KEELOQ_MODULATION_AM650, 0x02, 0x03, "ATA_PTX4"),
    [38] = SUBGHZ_KEELOQ_PRESET(KEELOQ_MASK_24, KEELOQ_PREFIX_NONE, KEELOQ_FREQUENCY_433_920, KEELOQ_MODULATION_AM650, 0x02, 0x03, "Pujol_Vario"),
    [39] = SUBGHZ_KEELOQ_PRESET(KEELOQ_MASK_24, KEELOQ_PREFIX_NONE, KEELOQ_FREQUENCY_433_920, KEELOQ_MODULATION_AM650, 0x02, 0x03, "Seav"),
    [40] = SUBGHZ_KEELOQ_PRESET(KEELOQ_MASK_24, KEELOQ_PREFIX_NONE, KEELOQ_FREQUENCY_433_920, KEELOQ_MODULATION_AM650, 0x02, 0x03, "Wisniowski"),
    [41] = SUBGHZ_KEELOQ_PRESET(KEELOQ_MASK_24, KEELOQ_PREFIX_NONE, KEELOQ_FREQUENCY_433_920, KEELOQ_MODULATION_AM650, 0x02, 0x03, "Fadini"),
    [42] = SUBGHZ_KEELOQ_PRESET(KEELOQ_MASK_24, KEELOQ_PREFIX_NONE, KEELOQ_FREQUENCY_433_920, KEELOQ_MODULATION_AM650, 0x02, 0x03, "Mc_Garcia"),
    [43] = SUBGHZ_KEELOQ_PRESET(KEELOQ_MASK_24, KEELOQ_PREFIX_NONE, KEELOQ_FREQUENCY_433_920, KEELOQ_MODULATION_AM650, 0x02, 0x03, "Clemsa_Mutancode"),
    [44] = SUBGHZ_KEELOQ_PRESET(KEELOQ_MASK_24, KEELOQ_PREFIX_NONE, KEELOQ_FREQUENCY_433_920, KEELOQ_MODULATION_AM650, 0x02, 0x03, "Doormatic"),
    [45] = SUBGHZ_KEELOQ_PRESET(KEELOQ_MASK_24, KEELOQ_PREFIX_NONE, KEELOQ_FREQUENCY_433_920, KEELOQ_MODULATION_AM650, 0x02, 0x03, "Elvox"),
    [46] = SUBGHZ_KEELOQ_PRESET(KEELOQ_MASK_24, KEELOQ_PREFIX_NONE, KEELOQ_FREQUENCY_433_920, KEELOQ_MODULATION_AM650, 0x02, 0x03, "Verex"),
    [47] = SUBGHZ_KEELOQ_PRESET(KEELOQ_MASK_16, KEELOQ_PREFIX_FAAC, KEELOQ_FREQUENCY_433_920, KEELOQ_MODULATION_AM650, 0x02, 0x03, "FAAC_RC,XT"),
    [48] = SUBGHZ_KEELOQ_PRESET(KEELOQ_MASK_16, KEELOQ_PREFIX_FAAC, KEELOQ_FREQUENCY_868_350, KEELOQ_MODULATION_AM650, 0x02, 0x03, "FAAC_RC,XT"),
    [49] = SUBGHZ_KEELOQ_PRESET(KEELOQ_MASK_16, KEELOQ_PREFIX_NONE, KEELOQ_FREQUENCY_433_920, KEELOQ_MODULATION_AM650, 0x04, 0x03, "Normstahl"),
    [50] = SUBGHZ_KEELOQ_PRESET(KEELOQ_MASK_20, KEELOQ_PREFIX_NONE, KEELOQ_FREQUENCY_433_920, KEELOQ_MODULATION_AM650, 0x02, 0x03, "HCS101"),
};

static const uint8_t subghz_keeloq_preset_by_type[SetTypeMAX] = {
    [SetTypeBeninca433] = 1,
    [SetTypeBeninca868] = 2,
    [SetTypeComunello433] = 3,
    [SetTypeComunello868] = 4,
    [SetTypeAllmatic433] = 5,
    [SetTypeAllmatic868] = 6,
    [SetTypeCenturion433] = 7,
    [SetTypeMonarch433] = 8,
    [SetTypeJollyMotors433] = 9,
    [SetTypeElmesElectronic] = 10,
    [SetTypeANMotorsAT4] = 11,
    [SetTypeAprimatic] = 12,
    [SetTypeGibidi433] = 13,
    [SetTypeGSN] = 14,
    [SetTypeIronLogic] = 15,
    [SetTypeIronLogicSmart] = 16,
    [SetTypeStilmatic] = 17,
    [SetTypeSommer_FM_434] = 18,
    [SetTypeSommer_FM_868] = 19,
    [SetTypeSommer_FM12K_434] = 20,
    [SetTypeSommer_FM12K_868] = 21,
    [SetTypeDTMNeo433] = 22,
    [SetTypeCAMESpace] = 23,
    [SetTypeMotorline433] = 24,
    [SetTypeDoorHan_433_92] = 25,
    [SetTypeDoorHan_315_00] = 26,
    [SetTypeNiceSmilo_433_92] = 27,
    [SetTypeNiceMHouse_433_92] = 28,
    [SetTypeDeaMio433] = 29,
    [SetTypeGeniusBravo433] = 30,
    [SetTypeJCM_433_92] = 31,
    [SetTypeNovoferm_433_92] = 32,
    [SetTypeHormannEcoStar_433_92] = 33,
    [SetTypeCardinS449_433FM] = 34,
    [SetTypePujol433] = 35,
    [SetTypeET_Blue433] = 36,
    [SetTypeET_Blue_Mix433] = 37,
    [SetTypeATA_PTX4_433] = 38,
    [SetTypePujol_Vario433] = 39,
    [SetTypeSeav433] = 40,
    [SetTypeWisniowski433] = 41,
    [SetTypeFadini433] = 42,
    [SetTypeMc_Garcia433] = 43,
    [SetTypeClemsa_Mutancode433] = 44,
    [SetTypeDoormatic433] = 45,
    [SetTypeElvox433] = 46,
    [SetTypeVerex433] = 47,
    [SetTypeFAACRCXT_433_92] = 48,
    [SetTypeFAACRCXT_868] = 49,
    [SetTypeNormstahl_433_92] = 50,
    [SetTypeHCS101_433_92] = 51,
};

static bool subghz_gen_info_fill_keeloq(GenInfo* gen_info, SetType type, uint64_t key) {
    if(type >= SetTypeMAX) return false;

    const uint8_t preset_index = subghz_keeloq_preset_by_type[type];
    if(preset_index == 0) return false;

    const SubGhzKeeloqGenerationPreset* preset =
        &subghz_keeloq_generation_presets[preset_index - 1];

    gen_info->type = GenKeeloq;
    gen_info->mod = subghz_keeloq_modulations[preset->modulation];
    gen_info->freq = subghz_keeloq_frequencies[preset->frequency];
    gen_info->keeloq.serial =
        (key & subghz_keeloq_serial_masks[preset->mask]) |
        subghz_keeloq_serial_prefixes[preset->prefix];
    gen_info->keeloq.btn = preset->button;
    gen_info->keeloq.cnt = preset->counter;
    gen_info->keeloq.manuf = preset->manufacturer;

    return true;
}

void subghz_gen_info_reset(GenInfo* gen_info) {
    furi_assert(gen_info);
    memset(gen_info, 0, sizeof(GenInfo));
}

void subghz_scene_set_type_fill_generation_infos(GenInfo* infos_dest, SetType type) {
    GenInfo gen_info = {0};
    uint64_t key = (uint64_t)rand();

    uint64_t gangqi_key;
    subghz_txrx_gen_serial_gangqi(&gangqi_key);

    uint64_t marantec_key;
    subghz_txrx_gen_key_marantec(&marantec_key);

    if(subghz_gen_info_fill_keeloq(&gen_info, type, key)) {
        *infos_dest = gen_info;
        return;
    }

    switch(type) {
    case SetTypePricenton433:
        gen_info = (GenInfo){
            .type = GenData,
            .mod = "AM650",
            .freq = 433920000,
            .data.name = subghz_protocol_princeton.name,
            .data.key = (key & 0x00FFFFF0) | 0x4, // btn 0x1, 0x2, 0x4, 0x8
            .data.bits = 24,
            .data.te = 400};
        break;
    case SetTypeTelcomaEdge433:
        gen_info = (GenInfo){
            .type = GenData,
            .mod = "AM650",
            .freq = 433920000,
            .data.name = subghz_protocol_telcoma_edge.name,
            // 0xFF preamble + serial; one-hot channel in low 3 bits
            // (gate = 0, others 0x1 / 0x2 / 0x4)
            .data.key = 0xFF000000 | (key & 0x00FFFFF8),
            .data.bits = 32};
        break;
    case SetTypePricenton315:
        gen_info = (GenInfo){
            .type = GenData,
            .mod = "AM650",
            .freq = 315000000,
            .data.name = subghz_protocol_princeton.name,
            .data.key = (key & 0x00FFFFF0) | 0x4, // btn 0x1, 0x2, 0x4, 0x8
            .data.bits = 24,
            .data.te = 400};
        break;
    case SetTypeZKTeco430:
        gen_info = (GenInfo){
            .type = GenData,
            .mod = "AM650",
            .freq = 430500000,
            .data.name = subghz_protocol_princeton.name,
            .data.key = (key & 0x00FFFF00) | 0x30, // btn 0x30(UP), 0x03(STOP), 0x0C(DOWN)
            .data.bits = 24,
            .data.te = 357};
        break;
    case SetTypeNiceFlo12bit:
        gen_info = (GenInfo){
            .type = GenData,
            .mod = "AM650",
            .freq = 433920000,
            .data.name = subghz_protocol_nice_flo.name,
            .data.key = (key & 0x00000FF0) | 0x1, // btn 0x1, 0x2, 0x4
            .data.bits = 12,
            .data.te = 0};
        break;
    case SetTypeNiceFlo24bit:
        gen_info = (GenInfo){
            .type = GenData,
            .mod = "AM650",
            .freq = 433920000,
            .data.name = subghz_protocol_nice_flo.name,
            .data.key = (key & 0x00FFFFF0) | 0x4, // btn 0x1, 0x2, 0x4, 0x8
            .data.bits = 24,
            .data.te = 0};
        break;
    case SetTypeCAME12bit:
        gen_info = (GenInfo){
            .type = GenData,
            .mod = "AM650",
            .freq = 433920000,
            .data.name = subghz_protocol_came.name,
            .data.key = (key & 0x00000FF0) | 0x1, // btn 0x1, 0x2, 0x4
            .data.bits = 12,
            .data.te = 0};
        break;
    case SetTypeCAME24bit:
        gen_info = (GenInfo){
            .type = GenData,
            .mod = "AM650",
            .freq = 433920000,
            .data.name = subghz_protocol_came.name,
            .data.key = (key & 0x00FFFFF0) | 0x4, // btn 0x1, 0x2, 0x4, 0x8
            .data.bits = 24,
            .data.te = 0};
        break;
    case SetTypeCAME12bit868:
        gen_info = (GenInfo){
            .type = GenData,
            .mod = "AM650",
            .freq = 868350000,
            .data.name = subghz_protocol_came.name,
            .data.key = (key & 0x00000FF0) | 0x1, // btn 0x1, 0x2, 0x4
            .data.bits = 12,
            .data.te = 0};
        break;
    case SetTypeCAME24bit868:
        gen_info = (GenInfo){
            .type = GenData,
            .mod = "AM650",
            .freq = 868350000,
            .data.name = subghz_protocol_came.name,
            .data.key = (key & 0x00FFFFF0) | 0x4, // btn 0x1, 0x2, 0x4, 0x8
            .data.bits = 24,
            .data.te = 0};
        break;
    case SetTypeRoger_433:
        gen_info = (GenInfo){
            .type = GenData,
            .mod = "AM650",
            .freq = 433920000,
            .data.name = subghz_protocol_roger.name,
            .data.key = (key & 0xFFFF000) | 0x0000101, // button code 0x1 and (crc?) is 0x01
            .data.bits = 28,
            .data.te = 0};
        break;
    case SetTypeLinear_300_00:
        gen_info = (GenInfo){
            .type = GenData,
            .mod = "AM650",
            .freq = 300000000,
            .data.name = subghz_protocol_linear.name,
            .data.key = (key & 0x3FF),
            .data.bits = 10,
            .data.te = 0};
        break;
    case SetTypeBETT_433:
        gen_info = (GenInfo){
            .type = GenData,
            .mod = "AM650",
            .freq = 433920000,
            .data.name = subghz_protocol_bett.name,
            .data.key = (key & 0x0000FFF0),
            .data.bits = 18,
            .data.te = 0};
        break;
    case SetTypeCAMETwee:
        gen_info = (GenInfo){
            .type = GenData,
            .mod = "AM650",
            .freq = 433920000,
            .data.name = subghz_protocol_came_twee.name,
            .data.key = 0x003FFF7200000000 | ((key & 0x0FFFFFF0) ^ 0xE0E0E0EE), // ????
            .data.bits = 54,
            .data.te = 0};
        break;
    case SetTypeGateTX:
        gen_info = (GenInfo){
            .type = GenData,
            .mod = "AM650",
            .freq = 433920000,
            .data.name = subghz_protocol_gate_tx.name, // btn 0xF, 0xC, 0xA, 0x6 (?)
            .data.key = subghz_protocol_blocks_reverse_key((key & 0x00F0FF00) | 0xF0040, 24),
            .data.bits = 24,
            .data.te = 0};
        break;
    case SetTypeGangQi_433:
        gen_info = (GenInfo){
            .type = GenData,
            .mod = "AM650",
            .freq = 433920000,
            .data.name = subghz_protocol_gangqi.name, // Add button 0xD arm and crc sum to the end
            .data.key = gangqi_key,
            .data.bits = 34,
            .data.te = 0};
        break;
    case SetTypeHollarm_433:
        gen_info = (GenInfo){
            .type = GenData,
            .mod = "AM650",
            .freq = 433920000,
            .data.name = subghz_protocol_hollarm.name, // Add button 0x2 and crc sum to the end
            .data.key = (key & 0x000FFF0000) | 0xF0B0002200 |
                        ((((((key & 0x000FFF0000) | 0xF0B0002200) >> 32) & 0xFF) +
                          ((((key & 0x000FFF0000) | 0xF0B0002200) >> 24) & 0xFF) +
                          ((((key & 0x000FFF0000) | 0xF0B0002200) >> 16) & 0xFF) +
                          ((((key & 0x000FFF0000) | 0xF0B0002200) >> 8) & 0xFF)) &
                         0xFF),
            .data.bits = 42,
            .data.te = 0};
        break;
    case SetTypeReversRB2_433:
        gen_info = (GenInfo){
            .type = GenData,
            .mod = "AM650",
            .freq = 433920000,
            .data.name = subghz_protocol_revers_rb2.name, // 64bits no buttons
            .data.key = (key & 0x00000FFFFFFFF000) | 0xFFFFF00000000000 | 0x0000000000000A00,
            .data.bits = 64,
            .data.te = 0};
        break;
    case SetTypeMarantec24_868:
        gen_info = (GenInfo){
            .type = GenData,
            .mod = "AM650",
            .freq = 868350000,
            .data.name = subghz_protocol_marantec24.name, // Add button code 0x8 to the end
            .data.key = (key & 0xFFFFF0) | 0x000008,
            .data.bits = 24,
            .data.te = 0};
        break;
    case SetTypeMarantec_433:
        gen_info = (GenInfo){
            .type = GenData,
            .mod = "AM650",
            .freq = 433920000,
            .data.name =
                subghz_protocol_marantec.name, // Button code is 0x4 and crc sum to the end
            .data.key = marantec_key,
            .data.bits = 49,
            .data.te = 0};
        break;
    case SetTypeMarantec_868:
        gen_info = (GenInfo){
            .type = GenData,
            .mod = "AM650",
            .freq = 868350000,
            .data.name =
                subghz_protocol_marantec.name, // Button code is 0x4 and crc sum to the end
            .data.key = marantec_key,
            .data.bits = 49,
            .data.te = 0};
        break;
    case SetTypeFaacSLH_433:
        gen_info = (GenInfo){
            .type = GenFaacSLH,
            .mod = "AM650",
            .freq = 433920000,
            .faac_slh.serial = ((key & 0x00FFFFF0) | 0xA0000006) >> 4,
            .faac_slh.btn = 0x06,
            .faac_slh.cnt = 0x02,
            .faac_slh.seed = (uint32_t)key,
            .faac_slh.manuf = "FAAC_SLH"};
        break;
    case SetTypeFaacSLH_868:
        gen_info = (GenInfo){
            .type = GenFaacSLH,
            .mod = "AM650",
            .freq = 868350000,
            .faac_slh.serial = ((key & 0x00FFFFF0) | 0xA0000006) >> 4,
            .faac_slh.btn = 0x06,
            .faac_slh.cnt = 0x02,
            .faac_slh.seed = (uint32_t)key,
            .faac_slh.manuf = "FAAC_SLH"};
        break;
    case SetTypeCameAtomo433:
        gen_info = (GenInfo){
            .type = GenCameAtomo,
            .mod = "AM650",
            .freq = 433920000,
            .came_atomo.serial = (key & 0x0FFFFFFF) | 0x10000000,
            .came_atomo.cnt = 0x03};
        break;
    case SetTypeCameAtomo868:
        gen_info = (GenInfo){
            .type = GenCameAtomo,
            .mod = "AM650",
            .freq = 868350000,
            .came_atomo.serial = (key & 0x0FFFFFFF) | 0x10000000,
            .came_atomo.cnt = 0x03};
        break;
    case SetTypeBFTMitto:
        gen_info = (GenInfo){
            .type = GenKeeloqSeed,
            .mod = "AM650",
            .freq = 433920000,
            .keeloq_seed.serial = key & 0x000FFFFF,
            .keeloq_seed.btn = 0x02,
            .keeloq_seed.cnt = 0x02,
            .keeloq_seed.seed = key & 0x000FFFFF,
            .keeloq_seed.manuf = "BFT"};
        break;
    case SetTypeErreka433:
        gen_info = (GenInfo){
            .type = GenKeeloqSeed,
            .mod = "AM650",
            .freq = 433920000,
            .keeloq_seed.serial = key & 0x000FFFFF,
            .keeloq_seed.btn = 0x02,
            .keeloq_seed.cnt = 0x02,
            .keeloq_seed.seed = key & 0x000FFFFF,
            .keeloq_seed.manuf = "Erreka"};
        break;
    case SetTypeAlutechAT4N:
        gen_info = (GenInfo){
            .type = GenAlutechAt4n,
            .mod = "AM650",
            .freq = 433920000,
            .alutech_at_4n.serial = (key & 0x000FFFFF) | 0x00100000,
            .alutech_at_4n.btn = 0x44,
            .alutech_at_4n.cnt = 0x03};
        break;
    case SetTypeSomfyTelis:
        gen_info = (GenInfo){
            .type = GenSomfyTelis,
            .mod = "AM650",
            .freq = 433420000,
            .somfy_telis.serial = key & 0x00FFFFFF,
            .somfy_telis.btn = 0x02,
            .somfy_telis.cnt = 0x03};
        break;
    case SetTypeSomfyKeytis:
        gen_info = (GenInfo){
            .type = GenSomfyKeytis,
            .mod = "AM650",
            .freq = 433420000,
            .somfy_keytis.serial = (key & 0x0000FFFF) | 0x00D50000,
            .somfy_keytis.btn = 0x04,
            .somfy_keytis.cnt = 0x03};
        break;
    case SetTypeKingGatesStylo4k:
        gen_info = (GenInfo){
            .type = GenKingGatesStylo4k,
            .mod = "AM650",
            .freq = 433920000,
            .kinggates_stylo_4k.serial = key & 0xFFFFFFFF,
            .kinggates_stylo_4k.btn = 0x0E,
            .kinggates_stylo_4k.cnt = 0x03};
        break;
    case SetTypeBenincaARC:
        gen_info = (GenInfo){
            .type = GenBenincaARC,
            .mod = "AM650",
            .freq = 433920000,
            .beninca_arc.serial = key & 0x00FFFFFF,
            .beninca_arc.btn = 0x02,
            .beninca_arc.cnt = 0x03};
        break;
    case SetTypeJarolift:
        gen_info = (GenInfo){
            .type = GenJarolift,
            .mod = "AM650",
            .freq = 433920000,
            .jarolift.serial = key & 0xFFFFF00,
            .jarolift.btn = 0x02,
            .jarolift.cnt = 0x03};
        break;
    case SetTypeSuperrollo:
        gen_info = (GenInfo){
            .type = GenSuperrollo,
            .mod = "AM650",
            .freq = 433920000,
            .superrollo.serial = key & 0xFFFFFF0,
            .superrollo.btn = 0x03, /* UP (valid: 0x03=Up 0x05=Down 0x07=Stop) */
            .superrollo.cnt = 0x0001};
        break;
    case SetTypeDitecGOL4:
        gen_info = (GenInfo){
            .type = GenDitecGOL4,
            .mod = "AM650",
            .freq = 433920000,
            .ditec_gol4.serial = (key & 0x0000FFFF) | 0xCC090000,
            .ditec_gol4.btn = 0x01,
            .ditec_gol4.cnt = 0xC200};
        break;
    case SetTypeNiceFlorS_433_92:
        gen_info = (GenInfo){
            .type = GenNiceFlorS,
            .mod = "AM650",
            .freq = 433920000,
            .nice_flor_s.serial = key & 0x0FFFFFFF,
            .nice_flor_s.btn = 0x01,
            .nice_flor_s.cnt = 0x03,
            .nice_flor_s.nice_one = false};
        break;
    case SetTypeNiceOne_433_92:
        gen_info = (GenInfo){
            .type = GenNiceFlorS,
            .mod = "AM650",
            .freq = 433920000,
            .nice_flor_s.serial = key & 0x0FFFFFFF,
            .nice_flor_s.btn = 0x01,
            .nice_flor_s.cnt = 0x03,
            .nice_flor_s.nice_one = true};
        break;
    case SetTypeSecPlus_v1_315_00:
        gen_info = (GenInfo){.type = GenSecPlus1, .mod = "AM650", .freq = 315000000};
        break;
    case SetTypeSecPlus_v1_390_00:
        gen_info = (GenInfo){.type = GenSecPlus1, .mod = "AM650", .freq = 390000000};
        break;
    case SetTypeSecPlus_v1_433_00:
        gen_info = (GenInfo){.type = GenSecPlus1, .mod = "AM650", .freq = 433920000};
        break;
    case SetTypeSecPlus_v2_310_00:
        gen_info = (GenInfo){
            .type = GenSecPlus2,
            .mod = "AM650",
            .freq = 310000000,
            .sec_plus_2.serial = (key & 0x7FFFF3FC), // 850LM pairing
            .sec_plus_2.btn = 0x68,
            .sec_plus_2.cnt = 0xE500000};
        break;
    case SetTypeSecPlus_v2_315_00:
        gen_info = (GenInfo){
            .type = GenSecPlus2,
            .mod = "AM650",
            .freq = 315000000,
            .sec_plus_2.serial = (key & 0x7FFFF3FC), // 850LM pairing
            .sec_plus_2.btn = 0x68,
            .sec_plus_2.cnt = 0xE500000};
        break;
    case SetTypeSecPlus_v2_390_00:
        gen_info = (GenInfo){
            .type = GenSecPlus2,
            .mod = "AM650",
            .freq = 390000000,
            .sec_plus_2.serial = (key & 0x7FFFF3FC), // 850LM pairing
            .sec_plus_2.btn = 0x68,
            .sec_plus_2.cnt = 0xE500000};
        break;
    case SetTypeSecPlus_v2_433_00:
        gen_info = (GenInfo){
            .type = GenSecPlus2,
            .mod = "AM650",
            .freq = 433920000,
            .sec_plus_2.serial = (key & 0x7FFFF3FC), // 850LM pairing
            .sec_plus_2.btn = 0x68,
            .sec_plus_2.cnt = 0xE500000};
        break;
    case SetTypePhoenix_V2_433:
        gen_info = (GenInfo){
            .type = GenPhoenixV2,
            .mod = "AM650",
            .freq = 433920000,
            .phoenix_v2.serial = (key & 0x0FFFFFFF) | 0xB0000000,
            .phoenix_v2.cnt = 0x025D};
        break;
    default:
        gen_info.type = GenUnsupported;
        break;
    }
    *infos_dest = gen_info;
}
