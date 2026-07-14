#!/usr/bin/env python3

import re
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
ARF_ROOT = REPO_ROOT / "applications_user/arf_subghz_full"
SUBGHZ_APP = REPO_ROOT / "applications/main/subghz/subghz.c"


class ArfFrequencyAnalyzerPresetScanTest(unittest.TestCase):
    def test_scan_is_dynamic_bounded_and_receive_only(self) -> None:
        worker = (ARF_ROOT / "helpers/subghz_frequency_analyzer_worker.c").read_text(
            encoding="utf-8"
        )
        probe = (ARF_ROOT / "helpers/subghz_txrx.c").read_text(encoding="utf-8")

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
        view = (ARF_ROOT / "views/subghz_frequency_analyzer.c").read_text(
            encoding="utf-8"
        )
        scene = (ARF_ROOT / "scenes/subghz_scene_frequency_analyzer.c").read_text(
            encoding="utf-8"
        )

        for label in ('"Scan"', '"Freq"', '"RX"', "I_ButtonUp_7x4", "I_ButtonDown_7x4"):
            self.assertIn(label, view)
        self.assertIn("subghz_frequency_analyzer_show_preset_rank", view)
        self.assertIn("SubGhzCustomEventViewFreqAnalPresetRx", view)

        self.assertIn("subghz_frequency_analyzer_get_selected_preset", scene)
        self.assertIn("subghz_txrx_set_preset_internal", scene)
        self.assertIn('loader_enqueue_launch(loader, "Sub-GHz", "receiver"', scene)
        self.assertIn("ARF_SUBGHZ_HUB_PATH", scene)
        self.assertIn("loader_clear_launch_queue", scene)

        subghz_app = SUBGHZ_APP.read_text(encoding="utf-8")
        self.assertIn('strcmp(p, "receiver") == 0', subghz_app)
        self.assertIn("const bool alloc_for_tx = p && strlen(p) && !open_receiver", subghz_app)
        self.assertIn("if(open_receiver)", subghz_app)
        self.assertIn("SubGhzSceneReceiver", subghz_app)


if __name__ == "__main__":
    unittest.main()
