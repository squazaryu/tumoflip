#!/usr/bin/env python3

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


class RollJamStandaloneIntegrationTest(unittest.TestCase):
    def test_rolljam_standalone_is_visible_arf_tool(self) -> None:
        manifest = (
            REPO_ROOT / "applications_user/rolljam_standalone/application.fam"
        ).read_text(encoding="utf-8")

        self.assertIn('appid="rolljam_standalone"', manifest)
        self.assertIn('name="RollJam"', manifest)
        self.assertIn('fap_category="ARF Tools"', manifest)
        self.assertIn("D4C1 Labs; tumoflip integration", manifest)
        self.assertNotIn("fap_dist_path", manifest)

    def test_rolljam_standalone_is_source_only_import(self) -> None:
        app_dir = REPO_ROOT / "applications_user/rolljam_standalone"
        forbidden_suffixes = {".fap", ".fal", ".elf", ".dfu", ".bin"}
        found = [
            path.relative_to(REPO_ROOT).as_posix()
            for path in app_dir.rglob("*")
            if path.is_file() and path.suffix.lower() in forbidden_suffixes
        ]

        self.assertEqual(found, [])

    def test_rolljam_rx_chain_uses_embedded_plugins(self) -> None:
        source = (
            REPO_ROOT / "applications_user/rolljam_standalone/helpers/rolljam_rx_chain.c"
        ).read_text(encoding="utf-8")

        self.assertIn("plugin_manager_load_single", source)
        self.assertIn("rolljam_am_plugin.fal", source)
        self.assertIn("rolljam_fm_plugin.fal", source)
        self.assertNotIn("extern const SubGhzProtocolRegistry rolljam_protocol_registry_am", source)
        self.assertNotIn("extern const SubGhzProtocolRegistry rolljam_protocol_registry_fm", source)

    def test_rolljam_protocol_plugin_appids_match_loaded_filters(self) -> None:
        app_i = (REPO_ROOT / "applications_user/rolljam_standalone/rolljam_app_i.c").read_text(
            encoding="utf-8"
        )
        rx_chain = (
            REPO_ROOT / "applications_user/rolljam_standalone/helpers/rolljam_rx_chain.c"
        ).read_text(encoding="utf-8")
        am_plugin = (
            REPO_ROOT
            / "applications_user/rolljam_standalone/protocols/plugins/rolljam_am_plugin.c"
        ).read_text(encoding="utf-8")
        fm_plugin = (
            REPO_ROOT
            / "applications_user/rolljam_standalone/protocols/plugins/rolljam_fm_plugin.c"
        ).read_text(encoding="utf-8")
        fm_extra_plugin = (
            REPO_ROOT
            / "applications_user/rolljam_standalone/protocols/plugins/rolljam_fm_plugin_extra.c"
        ).read_text(encoding="utf-8")

        self.assertIn("rolljam_protocol_plugin_app_id_for_filter(filter)", app_i)
        self.assertIn("rolljam_protocol_plugin_app_id_for_filter(chain->filter)", rx_chain)
        self.assertIn(".appid = ROLLJAM_PROTOCOL_AM_PLUGIN_APP_ID", am_plugin)
        self.assertIn(".appid = ROLLJAM_PROTOCOL_FM_PLUGIN_APP_ID", fm_plugin)
        self.assertIn(
            ".appid = ROLLJAM_PROTOCOL_FM_EXTRA_PLUGIN_APP_ID",
            fm_extra_plugin,
        )

    def test_release_and_deploy_include_rolljam_standalone(self) -> None:
        validate = (REPO_ROOT / "tools/tumoflip/validate_release.py").read_text(
            encoding="utf-8"
        )
        deploy = (REPO_ROOT / "tools/tumoflip/deploy_module_one_apps.py").read_text(
            encoding="utf-8"
        )
        docs = (REPO_ROOT / "docs/arf-subghz-full.md").read_text(encoding="utf-8")

        self.assertIn('"rolljam_standalone"', validate)
        self.assertIn("rolljam_standalone.fap", deploy)
        self.assertIn("RollJam Standalone", docs)


if __name__ == "__main__":
    unittest.main()
