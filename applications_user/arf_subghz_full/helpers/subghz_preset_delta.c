#include "subghz_preset_delta.h"

static const uint8_t preset_delta_am650_to_fm476[] = {
    0x10,
    0x67,
    0x11,
    0x83,
    0x12,
    0x04,
    0x13,
    0x02,
    0x19,
    0x16,
    0x21,
    0x56,
    0x22,
    0x10,
};

static const uint8_t preset_delta_am650_to_fm476_pa[8] = {
    0xC0,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
};

static const uint8_t preset_delta_am650_to_fm95[] = {
    0x10,
    0x67,
    0x11,
    0x83,
    0x12,
    0x04,
    0x13,
    0x02,
    0x14,
    0xF8,
    0x15,
    0x24,
    0x19,
    0x16,
    0x21,
    0x56,
    0x22,
    0x10,
};

static const uint8_t preset_delta_am650_to_fm95_pa[8] = {
    0xC0,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
};

static const uint8_t preset_delta_fm476_to_am650[] = {
    0x10,
    0x17,
    0x11,
    0x32,
    0x12,
    0x30,
    0x13,
    0x00,
    0x19,
    0x18,
    0x21,
    0xB6,
    0x22,
    0x11,
};

static const uint8_t preset_delta_fm476_to_am650_pa[8] = {
    0x00,
    0xC0,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
};

static const uint8_t preset_delta_fm476_to_fm95[] = {
    0x14,
    0xF8,
    0x15,
    0x24,
};

static const uint8_t preset_delta_fm95_to_am650[] = {
    0x10,
    0x17,
    0x11,
    0x32,
    0x12,
    0x30,
    0x13,
    0x00,
    0x14,
    0x00,
    0x15,
    0x47,
    0x19,
    0x18,
    0x21,
    0xB6,
    0x22,
    0x11,
};

static const uint8_t preset_delta_fm95_to_am650_pa[8] = {
    0x00,
    0xC0,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
};

static const uint8_t preset_delta_fm95_to_fm476[] = {
    0x14,
    0x00,
    0x15,
    0x47,
};

const PresetDeltaEntry preset_delta_table[HOP_PRESET_COUNT][HOP_PRESET_COUNT] = {
    {
        {0},
        {
            .delta = preset_delta_am650_to_fm476,
            .delta_len = sizeof(preset_delta_am650_to_fm476),
            .needs_scal = false,
            .pa_table = preset_delta_am650_to_fm476_pa,
        },
        {
            .delta = preset_delta_am650_to_fm95,
            .delta_len = sizeof(preset_delta_am650_to_fm95),
            .needs_scal = false,
            .pa_table = preset_delta_am650_to_fm95_pa,
        },
    },
    {
        {
            .delta = preset_delta_fm476_to_am650,
            .delta_len = sizeof(preset_delta_fm476_to_am650),
            .needs_scal = false,
            .pa_table = preset_delta_fm476_to_am650_pa,
        },
        {0},
        {
            .delta = preset_delta_fm476_to_fm95,
            .delta_len = sizeof(preset_delta_fm476_to_fm95),
            .needs_scal = false,
            .pa_table = NULL,
        },
    },
    {
        {
            .delta = preset_delta_fm95_to_am650,
            .delta_len = sizeof(preset_delta_fm95_to_am650),
            .needs_scal = false,
            .pa_table = preset_delta_fm95_to_am650_pa,
        },
        {
            .delta = preset_delta_fm95_to_fm476,
            .delta_len = sizeof(preset_delta_fm95_to_fm476),
            .needs_scal = false,
            .pa_table = NULL,
        },
        {0},
    },
};
