"""LLVM region coverage of named real timing bodies; mocks/RF are not coverage."""
import os

from tools.tumoflip import test_upstream_20261105 as timing
from tools.tumoflip.measure_upstream_1435_tests import measure

FUNCTIONS = {
    "subghz_protocol_decoder_hormann_check_pattern",
    "subghz_protocol_decoder_hormann_feed",
    "subghz_protocol_keeloq_te_delta",
    "subghz_protocol_keeloq_te_preamble",
    "subghz_protocol_keeloq_read_metadata",
    "subghz_protocol_keeloq_te_sanitize",
    "subghz_protocol_keeloq_te_nominal",
    "subghz_protocol_decoder_keeloq_reset",
    "subghz_protocol_decoder_keeloq_feed",
    "subghz_protocol_encoder_keeloq_encode_to_timings",
    "subghz_protocol_encoder_keeloq_deserialize",
    "subghz_protocol_decoder_keeloq_deserialize",
    "subghz_protocol_decoder_keeloq_serialize",
}


def main():
    # The shared runner instruments run_c; the ordinary fixtures call native.
    timing.native = lambda body: timing.run_c(body)
    os.environ["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
    return measure((timing,), FUNCTIONS)


if __name__ == "__main__":
    raise SystemExit(main())
