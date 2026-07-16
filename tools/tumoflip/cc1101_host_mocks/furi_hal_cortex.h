#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef uint32_t FuriHalCortexTimer;

FuriHalCortexTimer furi_hal_cortex_timer_get(uint32_t timeout_us);
bool furi_hal_cortex_timer_is_expired(FuriHalCortexTimer timer);
