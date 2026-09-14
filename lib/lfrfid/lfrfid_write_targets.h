/** @file lfrfid_write_targets.h
 *
 * Writable chips a key can be cloned onto.
 *
 * LFRFIDWriteType says how a write is encoded; a target additionally pins down which chip
 * is being addressed, since the ID82xx / Hitag micro family shares one encoding but needs a
 * different password per variant.
 *
 * Targets are what the user enables or disables in settings, and the enum value is the bit
 * position in the saved mask - so append new ones at the end, and see lfrfid_settings.c for
 * what that means for an existing settings file.
 */

#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LFRFIDWriteTargetT5577,
    LFRFIDWriteTargetEM4305,
    LFRFIDWriteTargetHitagMicro8265,
    LFRFIDWriteTargetHitagMicro8210,
    LFRFIDWriteTargetHitagMicroH55,
    LFRFIDWriteTargetHitagS8268,

    LFRFIDWriteTargetMax,
} LFRFIDWriteTarget;

/** A set of write targets, one bit per LFRFIDWriteTarget. */
typedef uint32_t LFRFIDWriteTargetMask;

_Static_assert(
    LFRFIDWriteTargetMax < 32,
    "A write target mask is a uint32_t, so there is room for 31 targets");

/** Every target. Use it to validate a mask, not to build one - see lfrfid_write_targets_default(). */
#define LFRFID_WRITE_TARGET_MASK_ALL ((LFRFIDWriteTargetMask)((1UL << LFRFIDWriteTargetMax) - 1))

/** Bit this target occupies in a mask. */
#define LFRFID_WRITE_TARGET_BIT(target) ((LFRFIDWriteTargetMask)(1UL << (target)))

/** Targets enabled when the user has expressed no preference.
 *
 * Hitag S / ID8268 is deliberately opt-in because pages 4 and 5 can contain application data on
 * a genuine Hitag S, which the Flipper cannot distinguish from a clone at write time.
 */
LFRFIDWriteTargetMask lfrfid_write_targets_default(void);

#ifdef __cplusplus
}
#endif
