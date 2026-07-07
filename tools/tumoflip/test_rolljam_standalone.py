#!/usr/bin/env python3

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


class RollJamStandaloneIntegrationTest(unittest.TestCase):
    def test_classic_rolljam_is_canonical_arf_module(self) -> None:
        classic_manifest = (
            REPO_ROOT / "applications_user/rolljam/application.fam"
        ).read_text(encoding="utf-8")
        standalone_manifest = (
            REPO_ROOT / "applications_user/rolljam_standalone/application.fam"
        ).read_text(encoding="utf-8")
        fbt_options = (REPO_ROOT / "fbt_options.py").read_text(encoding="utf-8")

        self.assertIn('appid="rolljam"', classic_manifest)
        self.assertIn('name="RollJam"', classic_manifest)
        self.assertIn('fap_category="ARF Tools"', classic_manifest)
        self.assertIn(
            'fap_dist_path="apps_data/arf_subghz_full/modules/{filename}"',
            classic_manifest,
        )

        self.assertIn('appid="rolljam_standalone"', standalone_manifest)
        self.assertIn('fap_category="ARF Internal"', standalone_manifest)
        self.assertIn(
            'fap_dist_path="apps_data/rolljam_standalone/{filename}"',
            standalone_manifest,
        )
        self.assertIn("EXCLUDED_EXT_APPS", fbt_options)
        self.assertIn('"rolljam_standalone"', fbt_options)

    def test_rolljam_standalone_is_source_only_import(self) -> None:
        app_dir = REPO_ROOT / "applications_user/rolljam_standalone"
        forbidden_suffixes = {".fap", ".fal", ".elf", ".dfu", ".bin"}
        found = [
            path.relative_to(REPO_ROOT).as_posix()
            for path in app_dir.rglob("*")
            if path.is_file() and path.suffix.lower() in forbidden_suffixes
        ]

        self.assertEqual(found, [])

    def test_rolljam_rx_chain_uses_external_plugin_paths(self) -> None:
        source = (
            REPO_ROOT / "applications_user/rolljam_standalone/helpers/rolljam_rx_chain.c"
        ).read_text(encoding="utf-8")
        header = (
            REPO_ROOT
            / "applications_user/rolljam_standalone/protocols/rolljam_protocol_plugins.h"
        ).read_text(encoding="utf-8")

        self.assertIn("plugin_manager_load_single", source)
        self.assertIn("ROLLJAM_PROTOCOL_AM_PLUGIN_PATH", source)
        self.assertIn("ROLLJAM_PROTOCOL_FM_PLUGIN_PATH", source)
        self.assertIn('EXT_PATH("apps_data/rolljam_standalone/plugins")', header)
        self.assertIn('"/rolljam_am_plugin.fal"', header)
        self.assertIn('"/rolljam_fm_plugin.fal"', header)
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

    def test_release_and_deploy_route_classic_rolljam_to_arf_module(self) -> None:
        validate = (REPO_ROOT / "tools/tumoflip/validate_release.py").read_text(
            encoding="utf-8"
        )
        deploy = (REPO_ROOT / "tools/tumoflip/deploy_module_one_apps.py").read_text(
            encoding="utf-8"
        )
        docs = (REPO_ROOT / "docs/arf-subghz-full.md").read_text(encoding="utf-8")

        self.assertIn('f"{appid}.fap": path', validate)
        self.assertNotIn('"rolljam_standalone.fap": ARF_MODULE_PATHS["rolljam"]', validate)
        self.assertIn('"rolljam", "rolljam.fap"', deploy)
        self.assertNotIn('"rolljam_standalone", "rolljam.fap"', deploy)
        self.assertIn("classic RollJam app", docs)
        self.assertIn("Shield Receiver source", docs)


if __name__ == "__main__":
    unittest.main()
