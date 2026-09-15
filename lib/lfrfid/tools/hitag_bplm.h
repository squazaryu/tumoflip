#pragma once

// Reader-to-tag BPLM/OOK timing shared by Hitag Micro and Hitag S. T0 is one
// 125 kHz carrier cycle (8 us): each cell begins with an 8 T0 field gap and
// keeps the field on for the remaining T0s. Keeping these values in one header
// prevents the resident writer and the lazy Hitag S package from drifting.
#define LFRFID_HITAG_BPLM_GAP_US       64u
#define LFRFID_HITAG_BPLM_BIT0_ON_US  96u
#define LFRFID_HITAG_BPLM_BIT1_ON_US 160u
