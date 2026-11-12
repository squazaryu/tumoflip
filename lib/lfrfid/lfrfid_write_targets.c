#include "lfrfid_write_targets.h"

LFRFIDWriteTargetMask lfrfid_write_targets_default(void) {
    // All existing writers are blind and target blank/clone chips. Hitag S / ID8268 is the
    // exception: pages 4 and 5 may be application data on a genuine tag, so require an explicit
    // opt-in in Settings before the worker can select and overwrite it.
    return LFRFID_WRITE_TARGET_MASK_ALL & ~LFRFID_WRITE_TARGET_BIT(LFRFIDWriteTargetHitagS8268);
}
