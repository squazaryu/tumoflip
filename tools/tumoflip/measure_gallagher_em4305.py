"""Fatal ASan/UBSan + scoped writer/builder coverage, not hardware coverage."""
import os
from tools.tumoflip import test_gallagher_em4305
from tools.tumoflip.measure_upstream_1435_tests import measure

FUNCTIONS = {
    "protocol_gallagher_write_data",
    "lfrfid_write_fill_t5577",
    "lfrfid_write_fill_em4305",
}


def main():
    os.environ["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
    return measure((test_gallagher_em4305,), FUNCTIONS)


if __name__ == "__main__":
    raise SystemExit(main())
