#!/usr/bin/env python3

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
APP_ROOT = REPO_ROOT / "applications_user/tumofabric_node"


class TumoFabricNodeTest(unittest.TestCase):
    def test_manifest_and_routes(self) -> None:
        manifest = (APP_ROOT / "application.fam").read_text(encoding="utf-8")
        cockpit = (
            REPO_ROOT / "applications_user/module_one_cockpit/module_one_cockpit.c"
        ).read_text(encoding="utf-8")
        validator = (REPO_ROOT / "tools/tumoflip/validate_release.py").read_text(
            encoding="utf-8"
        )

        self.assertIn('appid="tumofabric_node"', manifest)
        self.assertIn('fap_icon="icon.png"', manifest)
        self.assertIn('fap_category="Module One/Labs"', manifest)
        self.assertIn(
            'fap_dist_path="apps/Module One/Labs/tumofabric_node.fap"', manifest
        )
        self.assertIn('EXT_PATH("apps/Module One/Labs/tumofabric_node.fap")', cockpit)
        self.assertIn('{"VM: Fabric", ModuleOneCockpitActionLaunchTumoFabric}', cockpit)
        self.assertIn('"apps/Module One/Labs/tumofabric_node.fap"', validator)

    def test_ui_is_a_real_stateful_surface(self) -> None:
        source = (APP_ROOT / "tumofabric_node.c").read_text(encoding="utf-8")
        for required in (
            "TumoFabric Node",
            "FontBigNumbers",
            'elements_button_center(canvas, "Start")',
            'elements_button_center(canvas, "Cancel")',
            'elements_button_center(canvas, "Reset")',
            'elements_button_left(canvas, "Back")',
            "get_fabric_state",
            "open_local_fabric",
            "step_local_fabric",
            "cancel_fabric",
        ):
            self.assertIn(required, source)


if __name__ == "__main__":
    unittest.main()
