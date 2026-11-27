"""Measure the handoff, direct-load and allocation paths, not device coverage."""

import os

from tools.tumoflip import test_saved_raw_edit_launch
from tools.tumoflip.measure_upstream_1435_tests import measure

FUNCTIONS = {
    "subghz_scene_more_raw_can_edit",
    "subghz_scene_more_raw_on_enter",
    "raw_edit_capture_path_is_valid",
    "raw_edit_handle_launch_argument",
    "lr_read_line",
    "load_sub",
    "safe_malloc",
    "safe_realloc",
}

if __name__ == "__main__":
    os.environ["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
    raise SystemExit(measure((test_saved_raw_edit_launch,), FUNCTIONS))
