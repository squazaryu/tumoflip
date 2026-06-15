#include "toyota.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#define TAG "SubGhzProtocolToyota"

/*
 * TOYOTA KEELOQ — DUAL VARIANT DECODER
 *
 * VARIANT A — Corolla Verso 2004-2010 (433 MHz)
 *   TE short  : 400 us   TE long : 800 us   delta: 175 us
 *   Encoding  : PWM pairs — LS=0 (Long HIGH + Short LOW)
 *                            SL=1 (Short HIGH + Long LOW)
 *   Preamble  : repeated SS pairs, ends on first non-SS pair
 *   Frame     : 68 bits
 *   Repeats   : 8x
 *
 * VARIANT B — Tundra 2011 (315 MHz)
 *   Encoding  : NRZ — each individual pulse encodes one bit:
 *                 pulse <= 287 us -> bit 0
 *                 pulse >  287 us -> bit 1
 *   Preamble  : alternating short/long pulses (~200/~390 us each)
 *               ends with sync gap ~1938 us (a LOW > 1500 us)
 *   Frame     : 67 bits (each pulse = 1 bit, HIGH and LOW alike)
 *   Repeats   : 30x
 *   Inter-frame gap: ~51000 us
 *
 * Confirmed from real capture analysis:
 *   Preamble HIGHs: ~200 us (short)
 *   Preamble LOWs : ~390 us (long) — counted as preamble pairs
 *   Sync gap      : ~1938 us LOW after last preamble HIGH
 *   Data pulses   : each pulse independently = 0 if <=287us, 1 if >287us
 *   Boundary 287  = midpoint between 200 us and 375 us centers
 *
 * generic.data (64 bits):
 *   [63..32] = hop    (32 bits)
 *   [31..4]  = serial (28 bits)
 *   [3..0]   = button (4 bits)
 *
 * generic.cnt:
 *   0 = Variant A (Corolla/433MHz)
 *   1 = Variant B (Tundra/315MHz)
 */

/* ----------------------------------------------------------------
 * Physical constants — Variant A
 * ---------------------------------------------------------------- */

static const SubGhzBlockConst toyota_const_a = {
    .te_short                = 400,
    .te_long                 = 800,
    .te_delta                = 175,
    .min_count_bit_for_found = 60,
};

/* ----------------------------------------------------------------
 * Physical constants — Variant B (preamble classification only)
 * Data phase uses midpoint, not these tolerances.
 * ---------------------------------------------------------------- */

static const SubGhzBlockConst toyota_const_b = {
    .te_short                = 200,
    .te_long                 = 390,
    .te_delta                = 120,
    .min_count_bit_for_found = 60,
};

/*
 * NRZ midpoint for Variant B data pulses.
 * Pulses <= this value are bit 0, pulses > this value are bit 1.
 * Midpoint between short center (200us) and long center (375us).
 * Value 287 confirmed correct against real capture:
 *   192us->0  181us->0  383us->1  434us->1  380us->1  etc.
 */
#define TOYOTA_B_NRZ_MIDPOINT   287u

/* Sync gap: LOW pulse separating preamble from data */
#define TOYOTA_B_SYNC_GAP_MIN   1500u
#define TOYOTA_B_SYNC_GAP_MAX   2600u

/* Any pulse above this is an inter-frame gap */
#define TOYOTA_INTER_FRAME_GAP  5000u

/* Minimum preamble pairs before accepting sync gap */
#define TOYOTA_B_PREAMBLE_MIN   6u
#define TOYOTA_A_PREAMBLE_MIN   6u

/* Frame lengths in bits */
#define TOYOTA_A_BITS  68u
#define TOYOTA_B_BITS  67u

/* First HIGH duration below this -> Variant B, above -> Variant A */
#define TOYOTA_VARIANT_THRESH  310u

/* ----------------------------------------------------------------
 * Button codes
 * ---------------------------------------------------------------- */

#define TOYOTA_A_BTN_LOCK    0x08
#define TOYOTA_A_BTN_UNLOCK  0x01

#define TOYOTA_B_BTN_LOCK    0x0A
#define TOYOTA_B_BTN_UNLOCK  0x05

/* ----------------------------------------------------------------
 * Parser states
 * ---------------------------------------------------------------- */

typedef enum {
    ToyotaStepReset = 0,
    ToyotaStepPreambleA,
    ToyotaStepDataA,
    ToyotaStepPreambleB,
    ToyotaStepDataB,
} ToyotaDecoderStep;

/* ----------------------------------------------------------------
 * Decoder instance
 * ---------------------------------------------------------------- */

typedef struct {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder        decoder;
    SubGhzBlockGeneric        generic;

    uint64_t bits_lo;
    uint8_t  bits_hi;
    uint8_t  bit_count;

    uint32_t te_last;
    bool     have_high;
    uint16_t preamble_count;

    uint8_t  variant;   /* 0 = Corolla/433MHz,  1 = Tundra/315MHz */

    uint32_t hop;
    uint32_t serial;
    uint8_t  button;

} SubGhzProtocolDecoderToyota;

/* ----------------------------------------------------------------
 * Protocol descriptor
 * ---------------------------------------------------------------- */

const SubGhzProtocolDecoder subghz_protocol_toyota_decoder = {
    .alloc         = subghz_protocol_decoder_toyota_alloc,
    .free          = subghz_protocol_decoder_toyota_free,
    .feed          = subghz_protocol_decoder_toyota_feed,
    .reset         = subghz_protocol_decoder_toyota_reset,
    .get_hash_data = subghz_protocol_decoder_toyota_get_hash_data,
    .serialize     = subghz_protocol_decoder_toyota_serialize,
    .deserialize   = subghz_protocol_decoder_toyota_deserialize,
    .get_string    = subghz_protocol_decoder_toyota_get_string,
};

const SubGhzProtocol subghz_protocol_toyota = {
    .name    = SUBGHZ_PROTOCOL_TOYOTA_NAME,
    .type    = SubGhzProtocolTypeDynamic,
    .flag    = SubGhzProtocolFlag_433 |
               SubGhzProtocolFlag_315 |
               SubGhzProtocolFlag_AM  |
               SubGhzProtocolFlag_Decodable |
               SubGhzProtocolFlag_Load |
               SubGhzProtocolFlag_Save,
    .decoder = &subghz_protocol_toyota_decoder,
    .encoder = NULL,
};

/* ----------------------------------------------------------------
 * TE helpers (preamble phase only)
 * ---------------------------------------------------------------- */

static inline bool te_is_short(uint32_t d, const SubGhzBlockConst* c) {
    return DURATION_DIFF(d, (uint32_t)c->te_short) < (uint32_t)c->te_delta;
}

static inline bool te_is_long(uint32_t d, const SubGhzBlockConst* c) {
    return DURATION_DIFF(d, (uint32_t)c->te_long) < (uint32_t)c->te_delta;
}

/* ----------------------------------------------------------------
 * Bit accumulator
 * ---------------------------------------------------------------- */

static void toyota_push_bit(SubGhzProtocolDecoderToyota* inst, uint8_t bit) {
    uint8_t carry = (uint8_t)(inst->bits_lo >> 63) & 1;
    inst->bits_hi = (inst->bits_hi << 1) | carry;
    inst->bits_lo = (inst->bits_lo << 1) | (bit & 1);
    inst->bit_count++;
}

static uint32_t toyota_extract(
    const SubGhzProtocolDecoderToyota* inst,
    uint8_t offset,
    uint8_t length)
{
    uint32_t result = 0;
    uint8_t  total  = inst->bit_count;
    for(uint8_t i = 0; i < length; i++) {
        int8_t  pos = (int8_t)(total - 1) - (int8_t)(offset + i);
        uint8_t b   = 0;
        if(pos >= 64)     b = (inst->bits_hi >> (pos - 64)) & 1;
        else if(pos >= 0) b = (inst->bits_lo >> pos) & 1;
        result = (result << 1) | b;
    }
    return result;
}

/* ----------------------------------------------------------------
 * Name helpers
 * ---------------------------------------------------------------- */

static const char* toyota_button_name(uint8_t btn, uint8_t variant) {
    if(variant == 1) {
        switch(btn & 0x0F) {
        case TOYOTA_B_BTN_LOCK:   return "Lock";
        case TOYOTA_B_BTN_UNLOCK: return "Unlock";
        case 0x0F:                return "Lock+Unlock";
        case 0x04:                return "Trunk";
        default:                  return "Unknown";
        }
    }
    switch(btn & 0x0F) {
    case TOYOTA_A_BTN_LOCK:   return "Lock";
    case TOYOTA_A_BTN_UNLOCK: return "Unlock";
    case 0x09:                return "Lock+Unlock";
    case 0x02:                return "Trunk";
    case 0x04:                return "Aux";
    default:                  return "Unknown";
    }
}

static const char* toyota_model_name(uint8_t variant) {
    return (variant == 1) ? "Tundra" : "Corolla";
}

/* ----------------------------------------------------------------
 * Decode and fire callback
 * ---------------------------------------------------------------- */

static void toyota_decode_and_fire(SubGhzProtocolDecoderToyota* inst) {
    const SubGhzBlockConst* c =
        (inst->variant == 1) ? &toyota_const_b : &toyota_const_a;

    if(inst->bit_count < (uint8_t)c->min_count_bit_for_found) return;

    inst->hop    = toyota_extract(inst,  0, 32);
    inst->serial = toyota_extract(inst, 32, 28);
    inst->button = (uint8_t)toyota_extract(inst, 60, 4);

    inst->generic.data =
        ((uint64_t)inst->hop    << 32) |
        ((uint64_t)inst->serial <<  4) |
        ((uint64_t)inst->button & 0x0F);

    inst->generic.data_count_bit = inst->bit_count;
    inst->generic.serial         = inst->serial;
    inst->generic.btn            = inst->button;
    inst->generic.cnt            = inst->variant;

    inst->decoder.decode_data      = inst->generic.data;
    inst->decoder.decode_count_bit = inst->generic.data_count_bit;

    FURI_LOG_D(TAG, "FIRE var=%d bits=%d hop=%08lX serial=%07lX btn=%X",
        (int)inst->variant, (int)inst->bit_count,
        (unsigned long)inst->hop,
        (unsigned long)inst->serial,
        (unsigned int)inst->button);

    if(inst->base.callback)
        inst->base.callback(&inst->base, inst->base.context);
}

/* ----------------------------------------------------------------
 * Alloc / Free / Reset
 * ---------------------------------------------------------------- */

void* subghz_protocol_decoder_toyota_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderToyota* inst =
        malloc(sizeof(SubGhzProtocolDecoderToyota));
    inst->base.protocol         = &subghz_protocol_toyota;
    inst->generic.protocol_name = inst->base.protocol->name;
    return inst;
}

void subghz_protocol_decoder_toyota_free(void* context) {
    furi_assert(context);
    free(context);
}

void subghz_protocol_decoder_toyota_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderToyota* inst = context;

    inst->decoder.parser_step = ToyotaStepReset;
    inst->decoder.te_last     = 0;
    inst->bits_lo             = 0;
    inst->bits_hi             = 0;
    inst->bit_count           = 0;
    inst->te_last             = 0;
    inst->have_high           = false;
    inst->preamble_count      = 0;
    inst->hop                 = 0;
    inst->serial              = 0;
    inst->button              = 0;
    /* variant intentionally NOT reset — detected once per session */
}

/* ----------------------------------------------------------------
 * FEED — Variant A (Corolla 433 MHz)
 *
 * PWM pair encoding: LS=0, SL=1.
 * Preamble: SS pairs until first non-SS pair = first data bit.
 * ---------------------------------------------------------------- */

static void toyota_feed_variant_a(
    SubGhzProtocolDecoderToyota* inst,
    bool level, uint32_t duration)
{
    const SubGhzBlockConst* c = &toyota_const_a;

    if(inst->decoder.parser_step == ToyotaStepPreambleA) {

        if(level) {
            inst->te_last   = duration;
            inst->have_high = true;
            return;
        }

        if(!inst->have_high) {
            subghz_protocol_decoder_toyota_reset(inst);
            return;
        }
        inst->have_high = false;

        bool hs = te_is_short(inst->te_last, c);
        bool hl = te_is_long (inst->te_last, c);
        bool ls = te_is_short(duration, c);
        bool ll = te_is_long (duration, c);

        if(hs && ls) {
            inst->preamble_count++;
            return;
        }

        if(inst->preamble_count < TOYOTA_A_PREAMBLE_MIN) {
            subghz_protocol_decoder_toyota_reset(inst);
            return;
        }

        inst->bits_lo   = 0;
        inst->bits_hi   = 0;
        inst->bit_count = 0;

        if     (hl && ls) toyota_push_bit(inst, 0);
        else if(hs && ll) toyota_push_bit(inst, 1);

        inst->decoder.parser_step = ToyotaStepDataA;
        return;
    }

    if(inst->decoder.parser_step == ToyotaStepDataA) {

        if(level) {
            if(te_is_short(duration, c) || te_is_long(duration, c)) {
                inst->te_last   = duration;
                inst->have_high = true;
            } else {
                if(inst->bit_count >= (uint8_t)c->min_count_bit_for_found)
                    toyota_decode_and_fire(inst);
                subghz_protocol_decoder_toyota_reset(inst);
            }
            return;
        }

        if(!inst->have_high) return;
        inst->have_high = false;

        bool hs = te_is_short(inst->te_last, c);
        bool hl = te_is_long (inst->te_last, c);
        bool ls = te_is_short(duration, c);
        bool ll = te_is_long (duration, c);

        if(hl && ls) {
            toyota_push_bit(inst, 0);
        } else if(hs && ll) {
            toyota_push_bit(inst, 1);
        } else {
            if(inst->bit_count >= (uint8_t)c->min_count_bit_for_found)
                toyota_decode_and_fire(inst);
            subghz_protocol_decoder_toyota_reset(inst);
            return;
        }

        if(inst->bit_count >= TOYOTA_A_BITS) {
            toyota_decode_and_fire(inst);
            subghz_protocol_decoder_toyota_reset(inst);
        }
    }
}

/* ----------------------------------------------------------------
 * FEED — Variant B (Tundra 315 MHz)
 *
 * PREAMBLE state:
 *   Processes HIGH+LOW pairs using tight TE matching.
 *   Each [SHORT HIGH + LONG LOW] pair increments preamble_count.
 *   A LOW >= TOYOTA_B_SYNC_GAP_MIN after a valid preamble
 *   transitions to DATA state.
 *
 * DATA state — TRUE NRZ:
 *   Every single pulse (HIGH or LOW) independently encodes one bit.
 *   The Flipper delivers them alternating level=true/false.
 *   We process EACH pulse regardless of polarity:
 *     duration <= TOYOTA_B_NRZ_MIDPOINT (287us) -> bit 0
 *     duration >  TOYOTA_B_NRZ_MIDPOINT          -> bit 1
 *   A pulse >= TOYOTA_B_SYNC_GAP_MIN ends the frame.
 * ---------------------------------------------------------------- */

static void toyota_feed_variant_b(
    SubGhzProtocolDecoderToyota* inst,
    bool level, uint32_t duration)
{
    const SubGhzBlockConst* c = &toyota_const_b;

    /* ── PREAMBLE ── */
    if(inst->decoder.parser_step == ToyotaStepPreambleB) {

        if(level) {
            if(te_is_short(duration, c)) {
                inst->te_last   = duration;
                inst->have_high = true;
            } else {
                subghz_protocol_decoder_toyota_reset(inst);
            }
            return;
        }

        /* Falling edge */
        if(!inst->have_high) {
            subghz_protocol_decoder_toyota_reset(inst);
            return;
        }
        inst->have_high = false;

        /* Sync gap: LOW ~1938us -> transition to data */
        if(duration >= TOYOTA_B_SYNC_GAP_MIN &&
           duration <= TOYOTA_B_SYNC_GAP_MAX)
        {
            if(inst->preamble_count >= TOYOTA_B_PREAMBLE_MIN) {
                FURI_LOG_D(TAG, "B: sync gap after %d pairs -> NRZ data",
                    (int)inst->preamble_count);
                inst->bits_lo   = 0;
                inst->bits_hi   = 0;
                inst->bit_count = 0;
                inst->have_high = false;
                inst->decoder.parser_step = ToyotaStepDataB;
            } else {
                subghz_protocol_decoder_toyota_reset(inst);
            }
            return;
        }

        /* Normal preamble LOW must be LONG */
        if(te_is_long(duration, c)) {
            inst->preamble_count++;
            return;
        }

        subghz_protocol_decoder_toyota_reset(inst);
        return;
    }

    /* ── DATA (NRZ) ── */
    if(inst->decoder.parser_step == ToyotaStepDataB) {

        /*
         * Every pulse — HIGH or LOW — encodes one bit independently.
         * A pulse >= sync gap minimum signals end of frame.
         * A pulse >= inter-frame gap also ends frame.
         */
        if(duration >= TOYOTA_B_SYNC_GAP_MIN) {
            /* Frame ended by gap */
            if(inst->bit_count >= (uint8_t)c->min_count_bit_for_found) {
                toyota_decode_and_fire(inst);
            }
            subghz_protocol_decoder_toyota_reset(inst);
            return;
        }

        /*
         * NRZ bit: midpoint classification.
         * <= 287us -> bit 0
         * >  287us -> bit 1
         */
        uint8_t bit = (duration > TOYOTA_B_NRZ_MIDPOINT) ? 1 : 0;
        toyota_push_bit(inst, bit);

        if(inst->bit_count >= TOYOTA_B_BITS) {
            toyota_decode_and_fire(inst);
            subghz_protocol_decoder_toyota_reset(inst);
        }
    }
}

/* ----------------------------------------------------------------
 * Public feed — dispatcher
 * ---------------------------------------------------------------- */

void subghz_protocol_decoder_toyota_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderToyota* inst = context;

    if(inst->decoder.parser_step == ToyotaStepReset) {
        if(!level) return;

        /*
         * Variant detection from first SHORT HIGH pulse:
         *   < 310us  -> Variant B (Tundra 315MHz, TE_short~200us)
         *   >= 310us -> Variant A (Corolla 433MHz, TE_short~400us)
         */
        bool fits_b = te_is_short(duration, &toyota_const_b) &&
                      (duration < TOYOTA_VARIANT_THRESH);
        bool fits_a = te_is_short(duration, &toyota_const_a) &&
                      (duration >= TOYOTA_VARIANT_THRESH);

        if(fits_b) {
            inst->variant             = 1;
            inst->te_last             = duration;
            inst->have_high           = true;
            inst->preamble_count      = 0;
            inst->decoder.parser_step = ToyotaStepPreambleB;
            FURI_LOG_D(TAG, "Detected Variant B (Tundra), first HIGH=%lu",
                (unsigned long)duration);
        } else if(fits_a) {
            inst->variant             = 0;
            inst->te_last             = duration;
            inst->have_high           = true;
            inst->preamble_count      = 0;
            inst->decoder.parser_step = ToyotaStepPreambleA;
            FURI_LOG_D(TAG, "Detected Variant A (Corolla), first HIGH=%lu",
                (unsigned long)duration);
        }
        return;
    }

    if(inst->variant == 1) {
        toyota_feed_variant_b(inst, level, duration);
    } else {
        toyota_feed_variant_a(inst, level, duration);
    }
}

/* ----------------------------------------------------------------
 * Hash
 * ---------------------------------------------------------------- */

uint8_t subghz_protocol_decoder_toyota_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderToyota* inst = context;
    return subghz_protocol_blocks_get_hash_data(
        &inst->decoder,
        (inst->decoder.decode_count_bit / 8) + 1);
}

/* ----------------------------------------------------------------
 * Serialize
 * ---------------------------------------------------------------- */

SubGhzProtocolStatus subghz_protocol_decoder_toyota_serialize(
    void*              context,
    FlipperFormat*     flipper_format,
    SubGhzRadioPreset* preset)
{
    furi_assert(context);
    SubGhzProtocolDecoderToyota* inst = context;
    inst->generic.cnt = inst->variant;
    return subghz_block_generic_serialize(&inst->generic, flipper_format, preset);
}

/* ----------------------------------------------------------------
 * Deserialize
 * ---------------------------------------------------------------- */

SubGhzProtocolStatus subghz_protocol_decoder_toyota_deserialize(
    void*          context,
    FlipperFormat* flipper_format)
{
    furi_assert(context);
    SubGhzProtocolDecoderToyota* inst = context;

    SubGhzProtocolStatus ret =
        subghz_block_generic_deserialize_check_count_bit(
            &inst->generic,
            flipper_format,
            toyota_const_b.min_count_bit_for_found);

    if(ret == SubGhzProtocolStatusOk) {
        inst->hop    = (uint32_t)(inst->generic.data >> 32);
        inst->serial = (uint32_t)((inst->generic.data >> 4) & 0x0FFFFFFF);
        inst->button = (uint8_t)(inst->generic.data & 0x0F);

        inst->generic.serial = inst->serial;
        inst->generic.btn    = inst->button;
        inst->variant        = (inst->generic.cnt != 0) ? 1 : 0;
        inst->generic.cnt    = inst->variant;
    }

    return ret;
}

/* ----------------------------------------------------------------
 * get_string
 * ---------------------------------------------------------------- */

void subghz_protocol_decoder_toyota_get_string(void* context, FuriString* output) {
    furi_assert(context);
    SubGhzProtocolDecoderToyota* inst = context;

    uint32_t hop    = (uint32_t)(inst->generic.data >> 32);
    uint32_t serial = (uint32_t)((inst->generic.data >> 4) & 0x0FFFFFFF);
    uint8_t  button = (uint8_t)(inst->generic.data & 0x0F);
    uint8_t  var    = (inst->generic.cnt != 0) ? 1 : 0;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Hop: %08lX\r\n"
        "Sn:  %07lX\r\n"
        "Btn: %X [%s]",
        toyota_model_name(var),
        inst->generic.data_count_bit,
        (unsigned long)hop,
        (unsigned long)serial,
        button,
        toyota_button_name(button, var));
}
