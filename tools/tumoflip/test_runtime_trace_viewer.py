#!/usr/bin/env python3

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
APP_DIR = REPO_ROOT / "applications_user/runtime_trace_viewer"
APP_SOURCE = APP_DIR / "runtime_trace_viewer.c"
APP_MANIFEST = APP_DIR / "application.fam"
COCKPIT_SOURCE = REPO_ROOT / "applications_user/module_one_cockpit/module_one_cockpit.c"
ACCEPTANCE_SOURCE = REPO_ROOT / "applications_user/tumo_acceptance_suite/tumo_acceptance_suite.c"
VALIDATOR = REPO_ROOT / "tools/tumoflip/validate_release.py"
CHECKLIST = REPO_ROOT / "docs/hardware-regression-checklist.md"


class RuntimeTraceViewerTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = APP_SOURCE.read_text(encoding="utf-8")
        cls.manifest = APP_MANIFEST.read_text(encoding="utf-8")
        cls.cockpit = COCKPIT_SOURCE.read_text(encoding="utf-8")
        cls.acceptance = ACCEPTANCE_SOURCE.read_text(encoding="utf-8")
        cls.validator = VALIDATOR.read_text(encoding="utf-8")
        cls.checklist = CHECKLIST.read_text(encoding="utf-8")

    def test_app_is_module_one_diagnostics_fap(self) -> None:
        self.assertIn('appid="runtime_trace_viewer"', self.manifest)
        self.assertIn('name="Runtime Trace"', self.manifest)
        self.assertIn('apptype=FlipperAppType.EXTERNAL', self.manifest)
        self.assertIn('requires=["gui", "storage"]', self.manifest)
        self.assertIn('fap_category="Module One/Diagnostics"', self.manifest)
        self.assertIn(
            'fap_dist_path="apps/Module One/Diagnostics/runtime_trace_viewer.fap"',
            self.manifest,
        )
        self.assertIn('fap_icon="icon.png"', self.manifest)
        self.assertTrue((APP_DIR / "icon.png").is_file())

    def test_viewer_reads_runtime_record_without_ble(self) -> None:
        for required in (
            "#include <tumoflip_runtime/tumoflip_runtime.h>",
            "furi_record_open(RECORD_TUMOFLIP_RUNTIME)",
            "app->runtime->get_trace(app->runtime, output, size)",
            "RUNTIME_TRACE_VIEWER_TRACE_MAX 160U",
            "runtime_trace_viewer_build_report",
            "runtime_trace_viewer_event_code",
            "runtime_trace_viewer_command_name",
            "schema=1;depth=0;count=0",
        ):
            self.assertIn(required, self.source)

        self.assertNotIn("bt_app_bridge_send", self.source)
        self.assertNotIn("RECORD_BT", self.source)

    def test_viewer_exports_bounded_text_report_to_sd(self) -> None:
        for required in (
            'EXT_PATH("apps_data/runtime_trace_viewer")',
            '"/trace_%s.txt"',
            "storage_common_mkdir(storage, path)",
            "storage_file_open(file, path, FSAM_WRITE, FSOM_CREATE_ALWAYS)",
            "storage_file_write(file, text, size)",
            "storage_file_sync(file)",
            "Export Report",
            "command metadata only",
        ):
            self.assertIn(required, self.source)

    def test_package_and_cockpit_routes_are_registered(self) -> None:
        for required in (
            "System: Runtime Trace",
            'EXT_PATH("apps/Module One/Diagnostics/runtime_trace_viewer.fap")',
        ):
            self.assertIn(required, self.cockpit)

        self.assertIn(
            'EXT_PATH("apps/Module One/Diagnostics/runtime_trace_viewer.fap")',
            self.acceptance,
        )
        self.assertIn(
            '"apps/Module One/Diagnostics/runtime_trace_viewer.fap"',
            self.validator,
        )
        self.assertIn("Runtime Trace viewer", self.checklist)


if __name__ == "__main__":
    unittest.main()
