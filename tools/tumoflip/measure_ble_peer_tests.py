#!/usr/bin/env python3
"""Measure actual GAP lifecycle and peer-name C functions, not the radio/RTOS mocks."""

from tools.tumoflip.measure_upstream_1435_tests import measure
from tools.tumoflip import test_ble_gap_lifecycle as gap
from tools.tumoflip import test_hid_peer_scenes as scenes

FUNCTIONS = {
    "gap_init", "gap_wait_peer_idle", "gap_set_connection_peer", "gap_forget_bonded_device",
    "hid_peer_name_valid", "hid_peer_saved_name", "hid_peer_has_name", "hid_peer_name_in_use",
    "hid_peer_label", "hid_peer_devices_refresh", "hid_scene_devices_on_event",
    "hid_scene_peer_name_on_enter", "hid_scene_peer_name_on_event", "hid_scene_peer_name_on_exit",
}


if __name__ == "__main__":
    raise SystemExit(measure((gap, scenes), FUNCTIONS))
