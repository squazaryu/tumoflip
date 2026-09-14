#include "lfrfid_write_targets.h"

#include <furi.h>

// Strictly less than: LFRFID_WRITE_TARGET_MASK_ALL shifts by LFRFIDWriteTargetMax, and a shift
// by the full width of the type is undefined.
_Static_assert(
    LFRFIDWriteTargetMax < 32,
    "A write target mask is a uint32_t, so there is room for 31 targets");

static const LFRFIDWriteType lfrfid_write_target_types[LFRFIDWriteTargetMax] = {
    [LFRFIDWriteTargetT5577] = LFRFIDWriteTypeT5577,
    [LFRFIDWriteTargetEM4305] = LFRFIDWriteTypeEM4305,
    [LFRFIDWriteTargetHitagMicro8265] = LFRFIDWriteTypeHitagMicro,
    [LFRFIDWriteTargetHitagMicro8210] = LFRFIDWriteTypeHitagMicro,
    [LFRFIDWriteTargetHitagMicroH55] = LFRFIDWriteTypeHitagMicro,
};

_Static_assert(
    sizeof(lfrfid_write_target_types) / sizeof(lfrfid_write_target_types[0]) ==
        LFRFIDWriteTargetMax,
    "Every write target needs a write type");

LFRFIDWriteType lfrfid_write_target_type(LFRFIDWriteTarget target) {
    furi_check(target < LFRFIDWriteTargetMax);
    return lfrfid_write_target_types[target];
}

const char* lfrfid_write_target_name(LFRFIDWriteTarget target) {
    furi_check(target < LFRFIDWriteTargetMax);
    switch(target) {
    case LFRFIDWriteTargetT5577:
        return "T5577";
    case LFRFIDWriteTargetEM4305:
        return "EM4305";
    case LFRFIDWriteTargetHitagMicro8265:
    case LFRFIDWriteTargetHitagMicro8210:
    case LFRFIDWriteTargetHitagMicroH55:
        return hitagmicro_variant_name(
            (HitagMicroVariant)(target - LFRFIDWriteTargetHitagMicro8265));
    case LFRFIDWriteTargetMax:
        break;
    }

    furi_crash("Unknown write target");
}
