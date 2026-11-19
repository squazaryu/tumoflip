#pragma once

#include "types.h"

// Only the five visible receiver ignore categories belong in persisted settings.
// RAW/Decodable, modulation and frequency flags are not ignore options. Keep
// Standard and the external ARF host aligned without introducing an ABI symbol.
#define SUBGHZ_IGNORE_FILTER_MASK                                                            \
    (SubGhzProtocolFlag_ReversRB2 | SubGhzProtocolFlag_Alarms | SubGhzProtocolFlag_Sensors | \
     SubGhzProtocolFlag_Princeton | SubGhzProtocolFlag_NiceFlorS)
