#pragma once
#include "gap.h"

// Controller operations; caller owns the GAP lock and stops advertising first.
bool gap_peer_read(GapBondedDevices* devices);
bool gap_peer_select(const GapBondedDevice* peer);
bool gap_peer_forget(const GapBondedDevice* peer);
