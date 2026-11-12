"""Fatal sanitizer + named production-region coverage; not RF/whole-firmware coverage."""
import os
from tools.tumoflip import test_upstream_20261107 as archive
from tools.tumoflip import test_faac_tm as faac
from tools.tumoflip import test_raw_edit_readonly as raw
from tools.tumoflip.measure_upstream_1435_tests import measure

FUNCTIONS = {
    "archive_file_array_rm_selected", "archive_file_array_swap", "archive_delete_file",
    "subghz_protocol_encoder_faac_tm_get_upload", "subghz_protocol_faac_tm_read_key",
    "subghz_protocol_encoder_faac_tm_deserialize", "subghz_protocol_decoder_faac_tm_reset",
    "subghz_protocol_decoder_faac_tm_feed", "subghz_protocol_came_is_foreign_12bit",
    "subghz_protocol_decoder_came_feed", "fff_load_copy", "synthesize_via_transmitter",
    "fill_sub_into",
}


def main():
    os.environ["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
    for module in (archive, faac, raw):
        module.native = lambda body, selected=module: selected.run_c(body)
    return measure((archive, faac, raw), FUNCTIONS)


if __name__ == "__main__":
    raise SystemExit(main())
