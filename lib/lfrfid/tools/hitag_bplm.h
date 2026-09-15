#pragma once

#include <stdbool.h>
#include <furi.h>
#include <furi_hal_rfid.h>

// Reader-to-tag BPLM/OOK timing shared by Hitag Micro and Hitag S. T0 is one
// 125 kHz carrier cycle (8 us): each cell begins with an 8 T0 field gap and
// keeps the field on for the remaining T0s. Keeping these values in one header
// prevents the resident writer and the lazy Hitag S package from drifting.
#define LFRFID_HITAG_BPLM_GAP_US       64u
#define LFRFID_HITAG_BPLM_BIT0_ON_US  96u
#define LFRFID_HITAG_BPLM_BIT1_ON_US 160u

static inline void lfrfid_hitag_bplm_gap(void) {
    furi_hal_rfid_tim_read_pause();
    furi_delay_us(LFRFID_HITAG_BPLM_GAP_US);
    furi_hal_rfid_tim_read_continue();
}

static inline void lfrfid_hitag_bplm_send_bit(bool one) {
    lfrfid_hitag_bplm_gap();
    furi_delay_us(one ? LFRFID_HITAG_BPLM_BIT1_ON_US : LFRFID_HITAG_BPLM_BIT0_ON_US);
}
