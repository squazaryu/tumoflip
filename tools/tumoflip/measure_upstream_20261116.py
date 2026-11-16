"""Named production-C coverage of November 16 adaptations, not RF acceptance."""
import os

from tools.tumoflip import test_upstream_20261116 as core
from tools.tumoflip import test_manual_parameters as manual
from tools.tumoflip import test_belkorn_rx as belkorn
from tools.tumoflip import test_dea_mio_metadata as dea
from tools.tumoflip import test_rx_labels_20261116 as history
from tools.tumoflip.measure_upstream_1435_tests import measure

FUNCTIONS = {
    "subghz_devices_begin",
    "subghz_manual_band_count", "subghz_manual_band_get", "subghz_manual_band_index",
    "subghz_manual_timing", "subghz_manual_value_valid", "subghz_manual_number_back",
    "subghz_manual_refresh", "subghz_manual_radio_changed", "subghz_manual_number_result",
    "subghz_manual_enter", "subghz_scene_set_manual_params_on_enter",
    "subghz_scene_set_manual_params_on_event", "subghz_scene_set_manual_params_on_exit",
    "subghz_scene_set_type_proceed_advanced", "subghz_scene_set_type_on_event",
    "subghz_scene_set_key_on_enter", "subghz_scene_set_key_on_event", "subghz_scene_set_key_on_exit",
    "subghz_protocol_belkorn_frame_valid", "subghz_protocol_belkorn_metadata",
    "subghz_protocol_decoder_belkorn_reset", "subghz_protocol_decoder_belkorn_feed",
    "subghz_protocol_decoder_belkorn_serialize", "subghz_protocol_decoder_belkorn_deserialize",
    "subghz_protocol_keeloq_check_decrypt_dea_mio", "subghz_protocol_keeloq_dea_mio_disc",
    "subghz_history_name_variant",
}


def main():
    os.environ["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
    modules = (core, manual, belkorn, dea, history)
    for module in modules:
        module.native = lambda body, selected=module: selected.run_c(body)
    return measure(modules, FUNCTIONS)


if __name__ == "__main__":
    raise SystemExit(main())
