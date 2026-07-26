#!/usr/bin/env python3

import re
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
CORE_ROOT = REPO_ROOT / "applications/main/subghz"
SUBGHZ_APP = REPO_ROOT / "applications/main/subghz/subghz.c"


class ArfFrequencyAnalyzerPresetScanTest(unittest.TestCase):
    def test_scan_is_dynamic_bounded_and_receive_only(self) -> None:
        worker = (CORE_ROOT / "helpers/subghz_frequency_analyzer_worker.c").read_text(
            encoding="utf-8"
        )
        probe = (CORE_ROOT / "helpers/subghz_txrx.c").read_text(encoding="utf-8")

        self.assertIn("subghz_setting_get_preset_count(instance->setting)", worker)
        self.assertIn("PRESET_SCAN_MAX_RESULTS", worker)
        self.assertIn("PRESET_SCAN_SAMPLE_COUNT", worker)
        self.assertIn("instance->worker_running", worker)
        self.assertIn("subghz_txrx_analyzer_begin", worker)
        self.assertIn("subghz_txrx_analyzer_end", worker)

        preset_thread = re.search(
            r"static int32_t subghz_frequency_analyzer_worker_preset_thread\(.*?\n\}",
            worker,
            re.DOTALL,
        )
        self.assertIsNotNone(preset_thread)
        self.assertNotIn("set_tx", preset_thread.group(0))
        self.assertNotIn("start_async_tx", preset_thread.group(0))

        self.assertIn("subghz_devices_set_rx(instance->radio_device)", probe)
        self.assertIn("SubGhzRadioBrokerStateRx", probe)
        self.assertIn("subghz_devices_is_connect(instance->radio_device)", probe)
        self.assertIn("SubGhzRadioDeviceTypeInternal", probe)

    def test_ranked_result_can_handoff_to_standard_receiver(self) -> None:
        view = (CORE_ROOT / "views/subghz_frequency_analyzer.c").read_text(
            encoding="utf-8"
        )
        scene = (CORE_ROOT / "scenes/subghz_scene_frequency_analyzer.c").read_text(
            encoding="utf-8"
        )

        for label in ('"Scan"', '"Freq"', '"RX"', "I_ButtonUp_7x4", "I_ButtonDown_7x4"):
            self.assertIn(label, view)
        self.assertIn("subghz_frequency_analyzer_show_preset_rank", view)
        self.assertIn("SubGhzCustomEventViewFreqAnalPresetRx", view)

        self.assertIn("subghz_frequency_analyzer_get_selected_preset", scene)
        self.assertIn("subghz_txrx_set_preset_internal", scene)
        self.assertIn("subghz_scene_frequency_analyzer_open_receiver", scene)
        self.assertIn("SubGhzSceneReceiver", scene)

        subghz_app = SUBGHZ_APP.read_text(encoding="utf-8")
        self.assertIn('strcmp(p, "receiver") == 0', subghz_app)
        self.assertIn('strcmp(p, "tumospectrum_raw") == 0', subghz_app)
        self.assertNotIn('strcmp(p, "frequency_analyzer") == 0', subghz_app)
        self.assertNotIn("open_frequency_analyzer", subghz_app)
        self.assertIn("if(open_receiver || open_capture_raw)", subghz_app)
        self.assertIn(
            "open_capture_raw ? SubGhzSceneReadRAW : SubGhzSceneReceiver",
            subghz_app,
        )
        self.assertIn("SubGhzSceneReceiver", subghz_app)

    def test_empty_history_slots_do_not_repeat_mhz_suffix(self) -> None:
        view = (CORE_ROOT / "views/subghz_frequency_analyzer.c").read_text(
            encoding="utf-8"
        )

        self.assertIn("else if(model->history_frequency[i])", view)
        self.assertNotIn(
            'else {\n            canvas_draw_str(canvas, current_x + 41, current_y, "MHz");',
            view,
        )
        self.assertNotIn("const uint8_t icon_x = 119", view)

    def test_frequency_scan_restores_radio_boot_state_on_exit(self) -> None:
        worker = (CORE_ROOT / "helpers/subghz_frequency_analyzer_worker.c").read_text(
            encoding="utf-8"
        )
        frequency_thread = re.search(
            r"static int32_t subghz_frequency_analyzer_worker_frequency_thread"
            r"\(.*?\n\}",
            worker,
            re.DOTALL,
        )
        self.assertIsNotNone(frequency_thread)

        teardown = frequency_thread.group(0)
        idle = teardown.rindex("furi_hal_subghz_idle();")
        reset = teardown.rindex("furi_hal_subghz_reset();")
        isolate = teardown.rindex(
            "furi_hal_subghz_set_path(FuriHalSubGhzPathIsolate);"
        )
        sleep = teardown.rindex("furi_hal_subghz_sleep();")
        self.assertLess(idle, reset)
        self.assertLess(reset, isolate)
        self.assertLess(isolate, sleep)


if __name__ == "__main__":
    unittest.main()
