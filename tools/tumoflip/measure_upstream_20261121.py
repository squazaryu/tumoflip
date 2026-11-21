"""Scope coverage to the input, drawing and save-name functions under test."""

import os

from tools.tumoflip import test_upstream_20261121
from tools.tumoflip.measure_upstream_1435_tests import measure


FUNCTIONS = {
    "text_input_length",
    "text_input_draw_line",
    "text_input_set_result_callback",
    "subghz_save_name_standard",
    "subghz_save_name_arf",
}


if __name__ == "__main__":
    os.environ["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
    raise SystemExit(measure((test_upstream_20261121,), FUNCTIONS))
