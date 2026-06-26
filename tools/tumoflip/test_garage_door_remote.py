#!/usr/bin/env python3

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


class GarageDoorRemoteIntegrationTest(unittest.TestCase):
    def test_garage_door_remote_is_visible_arf_tool(self) -> None:
        manifest = (
            REPO_ROOT / "applications_user/garage_door_remote/application.fam"
        ).read_text(encoding="utf-8")

        self.assertIn('appid="garage_door_remote"', manifest)
        self.assertIn('name="Garage Door Remote"', manifest)
        self.assertIn('fap_category="ARF Tools"', manifest)
        self.assertIn("D4C1 Labs; tumoflip integration", manifest)
        self.assertNotIn("fap_dist_path", manifest)

    def test_import_excludes_upstream_prebuilt_artifacts(self) -> None:
        app_dir = REPO_ROOT / "applications_user/garage_door_remote"
        forbidden_suffixes = {".fap", ".fal", ".elf", ".dfu", ".bin"}
        found = [
            path.relative_to(REPO_ROOT).as_posix()
            for path in app_dir.rglob("*")
            if path.is_file() and path.suffix.lower() in forbidden_suffixes
        ]

        self.assertEqual(found, [])
        self.assertFalse((app_dir / "dist").exists())

    def test_embedded_plugin_appids_do_not_collide_with_protopirate(self) -> None:
        manifest = (
            REPO_ROOT / "applications_user/garage_door_remote/application.fam"
        ).read_text(encoding="utf-8")
        app_i = (
            REPO_ROOT / "applications_user/garage_door_remote/protopirate_app_i.c"
        ).read_text(encoding="utf-8")
        rx_chain = (
            REPO_ROOT / "applications_user/garage_door_remote/helpers/protopirate_rx_chain.c"
        ).read_text(encoding="utf-8")
        emulate_scene = (
            REPO_ROOT / "applications_user/garage_door_remote/scenes/protopirate_scene_emulate.c"
        ).read_text(encoding="utf-8")

        self.assertIn('appid="garage_door_remote_am_plugin"', manifest)
        self.assertIn('appid="garage_door_remote_fm_plugin"', manifest)
        self.assertIn('appid="garage_door_remote_emulate_plugin"', manifest)
        self.assertNotIn('appid="protopirate_am_plugin"', manifest)
        self.assertNotIn('appid="protopirate_fm_plugin"', manifest)
        self.assertNotIn('appid="protopirate_emulate_plugin"', manifest)
        self.assertIn("garage_door_remote_am_plugin.fal", app_i)
        self.assertIn("garage_door_remote_fm_plugin.fal", rx_chain)
        self.assertIn("garage_door_remote_emulate_plugin.fal", emulate_scene)

    def test_release_and_deploy_include_garage_door_remote(self) -> None:
        validate = (REPO_ROOT / "tools/tumoflip/validate_release.py").read_text(
            encoding="utf-8"
        )
        deploy = (REPO_ROOT / "tools/tumoflip/deploy_module_one_apps.py").read_text(
            encoding="utf-8"
        )
        docs = (REPO_ROOT / "docs/arf-subghz-full.md").read_text(encoding="utf-8")

        self.assertIn('"garage_door_remote"', validate)
        self.assertIn("garage_door_remote.fap", deploy)
        self.assertIn("Garage Door Remote", docs)


if __name__ == "__main__":
    unittest.main()
