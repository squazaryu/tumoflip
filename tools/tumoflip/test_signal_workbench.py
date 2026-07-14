#!/usr/bin/env python3

import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
APP_DIR = REPO_ROOT / "applications_user/signal_workbench"
APP_SOURCE = APP_DIR / "signal_workbench.c"
PARSER_SOURCE = APP_DIR / "tumospectrum_parser.c"
ANALYSIS_SOURCE = APP_DIR / "tumospectrum_analysis.c"
STORAGE_SOURCE = APP_DIR / "tumospectrum_storage.c"
APP_MANIFEST = APP_DIR / "application.fam"
COCKPIT_SOURCE = REPO_ROOT / "applications_user/module_one_cockpit/module_one_cockpit.c"
ACCEPTANCE_SOURCE = REPO_ROOT / "applications_user/tumo_acceptance_suite/tumo_acceptance_suite.c"
VALIDATOR = REPO_ROOT / "tools/tumoflip/validate_release.py"


class TumoSpectrumTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = APP_SOURCE.read_text(encoding="utf-8")
        cls.parser = PARSER_SOURCE.read_text(encoding="utf-8")
        cls.analysis = ANALYSIS_SOURCE.read_text(encoding="utf-8")
        cls.storage = STORAGE_SOURCE.read_text(encoding="utf-8")
        cls.manifest = APP_MANIFEST.read_text(encoding="utf-8")
        cls.cockpit = COCKPIT_SOURCE.read_text(encoding="utf-8")
        cls.acceptance = ACCEPTANCE_SOURCE.read_text(encoding="utf-8")
        cls.validator = VALIDATOR.read_text(encoding="utf-8")

    def test_app_migrates_in_place_without_duplicate_fap(self) -> None:
        self.assertIn('appid="signal_workbench"', self.manifest)
        self.assertIn('name="TumoSpectrum"', self.manifest)
        self.assertIn('fap_version="1.0.2"', self.manifest)
        self.assertIn('fap_category="Module One/Signals"', self.manifest)
        self.assertIn(
            'fap_dist_path="apps/Module One/Signals/signal_workbench.fap"', self.manifest
        )
        self.assertIn('fap_icon="icon.png"', self.manifest)
        self.assertTrue((APP_DIR / "icon.png").is_file())

    def test_parsers_are_typed_bounded_and_read_only(self) -> None:
        for required in (
            "flipper_format_read_header",
            '"Flipper SubGhz RAW File"',
            '"Flipper SubGhz Key File"',
            '"IR signals file"',
            '"$timescale 1 ns"',
            "TUMOSPECTRUM_FILE_SIZE_MAX",
            "TUMOSPECTRUM_IR_VALUES_MAX",
            "TUMOSPECTRUM_MAX_TIMINGS",
            "tumospectrum_subghz_stream_raw",
            "TumoSpectrumStatusUnsupported",
            "TumoSpectrumStatusMalformed",
        ):
            self.assertIn(required, self.parser)
        self.assertNotIn("FSAM_WRITE", self.parser)
        self.assertNotIn("FSOM_CREATE", self.parser)

    def test_raw_analysis_and_comparison_are_bounded(self) -> None:
        for required in (
            "TUMOSPECTRUM_HISTOGRAM_BUCKETS",
            "tumospectrum_repeat_score",
            "gap_threshold_us",
            "histogram_similarity",
            "overall_similarity",
            "tumospectrum_types_compatible",
        ):
            self.assertIn(required, self.analysis)

    def test_product_ui_uses_real_controls_and_stock_handoff(self) -> None:
        for required in (
            'submenu_set_header(app->menu, "TumoSpectrum")',
            '"Open Sub-GHz"',
            '"Open Infrared"',
            '"Open TumoScope"',
            '"Compare Capture"',
            '"Send to Companion"',
            'elements_button_left(canvas, "Prev")',
            'elements_button_center(canvas, capture->status == TumoSpectrumStatusOk ? "Actions" : "Menu")',
            'elements_button_right(canvas, compare_page ? "Compare" : "Next")',
            'tumospectrum_compare_capture(app);',
            'return tumospectrum_type_has_timings(capture->type) ? 4U : 1U;',
            "dialog_file_browser_show",
            "I_sub1_10px",
            "I_ir_10px",
            'target = "Sub-GHz"',
            'target = "Infrared"',
            "loader_enqueue_launch",
        ):
            self.assertIn(required, self.source)

    def test_module_views_keep_their_internal_context(self) -> None:
        for forbidden in (
            "view_set_context(text_box_get_view",
            "view_set_context(text_input_get_view",
        ):
            self.assertNotIn(forbidden, self.source)
        for required in (
            "tumospectrum_text_previous_menu",
            "tumospectrum_text_previous_result",
            "tumospectrum_note_previous",
        ):
            self.assertIn(required, self.source)

    def test_app_never_transmits_directly(self) -> None:
        combined = self.source + self.parser + self.analysis + self.storage
        for forbidden in (
            "furi_hal_subghz",
            "subghz_txrx_tx_start",
            "infrared_send",
            "infrared_signal_transmit",
            "GpioModeOutputPushPull",
        ):
            self.assertNotIn(forbidden, combined)

    def test_reports_have_json_notebook_and_fab2_contract(self) -> None:
        for required in (
            '"schema\\\":1',
            '"app\\\":\\\"TumoSpectrum',
            "TUMOSPECTRUM_NOTEBOOK_CSV",
            "tumospectrum_write_file",
            "storage_common_rename",
            "tumospectrum_append_json_string",
        ):
            self.assertIn(required, self.storage)
        self.assertIn('TUMOSPECTRUM_BRIDGE_COMMAND "report"', self.source)
        self.assertIn("bt_app_bridge_send_text_v2", self.source)
        self.assertIn("BT_APP_BRIDGE_V2_PAYLOAD_LEN_MAX", self.source)

    def test_existing_routes_still_point_to_same_fap(self) -> None:
        for text in (self.cockpit, self.acceptance, self.validator):
            self.assertIn("signal_workbench.fap", text)
        self.assertIn('"apps/Module One/Signals/signal_workbench.fap"', self.validator)

    def test_analysis_core_executes_on_host(self) -> None:
        harness = textwrap.dedent(
            """
            #include "tumospectrum_analysis.h"
            #include <assert.h>
            #include <string.h>

            int main(void) {
                TumoSpectrumCapture first = {0};
                first.status = TumoSpectrumStatusOk;
                first.type = TumoSpectrumCaptureSubGhzRaw;
                first.frequency_hz = 433920000U;
                const int32_t values[] = {
                    400, -400, 1200, -1200, 400, -8000,
                    400, -400, 1200, -1200, 400, -8000,
                };
                memcpy(first.timings, values, sizeof(values));
                first.timing_count = sizeof(values) / sizeof(values[0]);
                tumospectrum_analyze(&first);
                assert(first.analysis.count == 12U);
                assert(first.analysis.burst_count == 3U);
                assert(first.analysis.repeat_score >= 80U);

                TumoSpectrumCapture second = first;
                tumospectrum_analyze(&second);
                TumoSpectrumComparison comparison = tumospectrum_compare(&first, &second);
                assert(comparison.compatible);
                assert(comparison.likely_same);
                assert(comparison.overall_similarity == 100U);

                second.type = TumoSpectrumCaptureInfraredRaw;
                comparison = tumospectrum_compare(&first, &second);
                assert(!comparison.compatible);
                return 0;
            }
            """
        )
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            source = tmp_path / "analysis_test.c"
            binary = tmp_path / "analysis_test"
            source.write_text(harness, encoding="utf-8")
            subprocess.run(
                [
                    "clang",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(APP_DIR),
                    str(source),
                    str(ANALYSIS_SOURCE),
                    "-o",
                    str(binary),
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            subprocess.run([str(binary)], check=True, capture_output=True, text=True)


if __name__ == "__main__":
    unittest.main()
