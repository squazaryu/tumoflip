"""Fatal sanitizers and scoped coverage; not whole-firmware/hardware coverage."""
import os

from tools.tumoflip import test_upstream_20261119
from tools.tumoflip.measure_upstream_1435_tests import measure

FUNCTIONS = {
    "subghz_protocol_decoder_feron_feed",
    "subghz_last_settings_load_standard",
    "subghz_last_settings_load_arf",
}


def main():
    os.environ["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
    return measure((test_upstream_20261119,), FUNCTIONS)


if __name__ == "__main__":
    raise SystemExit(main())
