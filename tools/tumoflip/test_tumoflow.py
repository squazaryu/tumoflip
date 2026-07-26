#!/usr/bin/env python3

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
APP_DIR = REPO_ROOT / "applications_user/tumoflow"
SOURCE = APP_DIR / "tumoflow.c"
MANIFEST = APP_DIR / "application.fam"
COCKPIT = REPO_ROOT / "applications_user/module_one_cockpit/module_one_cockpit.c"
RESOURCES = REPO_ROOT / "tools/tumoflip/sd_resources/apps_data/tumoflow/workflows"


class TumoFlowTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = SOURCE.read_text(encoding="utf-8")
        cls.manifest = MANIFEST.read_text(encoding="utf-8")
        cls.cockpit = COCKPIT.read_text(encoding="utf-8")

    def test_external_api_88_fap_route(self) -> None:
        self.assertIn('appid="tumoflow"', self.manifest)
        self.assertIn("FlipperAppType.EXTERNAL", self.manifest)
        self.assertIn('fap_category="Module One/Automation"', self.manifest)
        self.assertIn(
            'fap_dist_path="apps/Module One/Automation/tumoflow.fap"',
            self.manifest,
        )
        self.assertTrue((APP_DIR / "icon.png").is_file())

    def test_legacy_sources_are_nondestructive(self) -> None:
        self.assertIn("TUMOFLOW_LEGACY_SCRIPT_DIR", self.source)
        self.assertIn("TUMOFLOW_LEGACY_MACRO_DIR", self.source)
        self.assertIn("storage_common_copy", self.source)
        self.assertIn('"Original kept"', self.source)
        self.assertNotIn("storage_common_remove(app->storage, workflow.path)", self.source)

    def test_field_triggers_and_actions_are_supported(self) -> None:
        for token in (
            "TumoFlowTriggerCountdown",
            "TumoFlowTriggerIr",
            "TumoFlowTriggerSubGhz",
            "TumoFlowTriggerNfc",
            "TumoFlowTriggerLfRfid",
            "TumoFlowTriggerIButton",
            "TumoFlowStepIrFile",
            "TumoFlowStepSubGhzFile",
            "TumoFlowStepGpioPulse",
            "TumoFlowStepBridge",
            "TumoFlowStepBranchOk",
            "TumoFlowStepBranchError",
        ):
            self.assertIn(token, self.source)

    def test_runtime_is_bounded_and_cancellable(self) -> None:
        self.assertIn("#define TUMOFLOW_MAX_STEPS 32U", self.source)
        self.assertIn("#define TUMOFLOW_MAX_EXECUTED_STEPS 64U", self.source)
        self.assertIn("#define TUMOFLOW_MAX_RUNTIME_MS 900000UL", self.source)
        self.assertIn("plan->emission_count > 8U", self.source)
        self.assertIn('"OK Arm / Back cancel"', self.source)
        self.assertIn("SubGhzRadioBrokerStateCleaningUp", self.source)
        self.assertIn("subghz_worker_stop(worker)", self.source)
        self.assertIn("furi_hal_gpio_init_simple(&gpio_ext_pc0, GpioModeAnalog)", self.source)

    def test_onboarding_and_guide_are_discoverable(self) -> None:
        self.assertIn("TUMOFLOW_ONBOARDING_PATH", self.source)
        self.assertIn("TUMOFLOW_HELP_PAGE_COUNT 5U", self.source)
        self.assertIn("TumoFlowModeDashboard", self.source)
        self.assertIn("TumoFlowModeHelp", self.source)
        self.assertIn('elements_button_left(canvas, "Guide")', self.source)
        self.assertIn('elements_button_center(canvas, "Open")', self.source)
        self.assertIn('"Hold Right: Dry Run"', self.source)
        self.assertIn('"Hold OK; original stays"', self.source)
        self.assertIn("tumoflow_mark_onboarding_seen", self.source)

    def test_cockpit_and_samples_are_packaged(self) -> None:
        self.assertIn("Automation: TumoFlow", self.cockpit)
        self.assertIn(
            'EXT_PATH("apps/Module One/Automation/tumoflow.fap")',
            self.cockpit,
        )
        self.assertTrue((RESOURCES / "field_demo.tflow").is_file())
        self.assertTrue((RESOURCES / "bounded_outputs.tflow").is_file())


if __name__ == "__main__":
    unittest.main()
