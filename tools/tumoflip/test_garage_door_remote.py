#!/usr/bin/env python3

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]

EXPANDED_AM_PROTOCOLS = {
    "bett": "subghz_protocol_bett",
    "bin_raw": "subghz_protocol_bin_raw",
    "dickert_mahs": "subghz_protocol_dickert_mahs",
    "doitrand": "subghz_protocol_doitrand",
    "elplast": "subghz_protocol_elplast",
    "feron": "subghz_protocol_feron",
    "gangqi": "subghz_protocol_gangqi",
    "hay21": "subghz_protocol_hay21",
    "hollarm": "subghz_protocol_hollarm",
    "holtek": "subghz_protocol_holtek",
    "holtek_ht12x": "subghz_protocol_holtek_th12x",
    "honeywell": "subghz_protocol_honeywell",
    "honeywell_wdb": "subghz_protocol_honeywell_wdb",
    "ido": "subghz_protocol_ido",
    "intertechno_v3": "subghz_protocol_intertechno_v3",
    "jarolift": "subghz_protocol_jarolift",
    "keyfinder": "subghz_protocol_keyfinder",
    "kinggates_stylo_4k": "subghz_protocol_kinggates_stylo_4k",
    "legrand": "subghz_protocol_legrand",
    "magellan": "subghz_protocol_magellan",
    "marantec": "subghz_protocol_marantec",
    "marantec24": "subghz_protocol_marantec24",
    "mastercode": "subghz_protocol_mastercode",
    "nero_radio": "subghz_protocol_nero_radio",
    "nero_sketch": "subghz_protocol_nero_sketch",
    "phoenix_v2": "subghz_protocol_phoenix_v2",
    "power_smart": "subghz_protocol_power_smart",
    "raw": "subghz_protocol_raw",
    "revers_rb2": "subghz_protocol_revers_rb2",
    "roger": "subghz_protocol_roger",
    "secplus_v1": "subghz_protocol_secplus_v1",
    "secplus_v2": "subghz_protocol_secplus_v2",
    "smc5326": "subghz_protocol_smc5326",
    "treadmill37": "subghz_protocol_treadmill37",
}


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

        self.assertNotIn('appid="garage_door_remote_am_plugin"', manifest)
        self.assertIn('appid="garage_door_remote_fm_plugin"', manifest)
        self.assertIn('appid="garage_door_remote_emulate_plugin"', manifest)
        self.assertNotIn('appid="protopirate_am_plugin"', manifest)
        self.assertNotIn('appid="protopirate_fm_plugin"', manifest)
        self.assertNotIn('appid="protopirate_emulate_plugin"', manifest)
        self.assertNotIn("garage_door_remote_am_plugin.fal", app_i)
        self.assertNotIn("garage_door_remote_am_plugin.fal", rx_chain)
        self.assertIn("garage_door_remote_fm_plugin.fal", rx_chain)
        self.assertIn("garage_door_remote_emulate_plugin.fal", emulate_scene)

    def test_embedded_plugin_interfaces_are_garage_door_scoped(self) -> None:
        app_dir = REPO_ROOT / "applications_user/garage_door_remote"

        protocol_header = (
            app_dir / "protocols/protopirate_protocol_plugins.h"
        ).read_text(encoding="utf-8")
        emulate_header = (
            app_dir / "scenes/plugins/protopirate_emulate_plugin.h"
        ).read_text(encoding="utf-8")
        psa_header = (
            app_dir / "scenes/plugins/protopirate_psa_bf_plugin.h"
        ).read_text(encoding="utf-8")
        app_i = (app_dir / "protopirate_app_i.c").read_text(encoding="utf-8")
        rx_chain = (app_dir / "helpers/protopirate_rx_chain.c").read_text(
            encoding="utf-8"
        )
        emulate_scene = (
            app_dir / "scenes/protopirate_scene_emulate.c"
        ).read_text(encoding="utf-8")

        self.assertIn(
            'GDR_PROTOCOL_PLUGIN_APP_ID      "gdr_protocol_plugins"',
            protocol_header,
        )
        self.assertIn(
            'GDR_EMULATE_PLUGIN_APP_ID      "gdr_emulate_plugin"',
            emulate_header,
        )
        self.assertIn(
            'GDR_PSA_BF_PLUGIN_APP_ID      "gdr_psa_bf_plugin"', psa_header
        )
        self.assertIn("GDR_PROTOCOL_PLUGIN_APP_ID", app_i)
        self.assertIn("GDR_PROTOCOL_PLUGIN_APP_ID", rx_chain)
        self.assertIn("GDR_EMULATE_PLUGIN_APP_ID", emulate_scene)
        self.assertNotIn("PROTOPIRATE_PROTOCOL_PLUGIN_APP_ID", app_i)
        self.assertNotIn("PROTOPIRATE_PROTOCOL_PLUGIN_APP_ID", rx_chain)
        self.assertNotIn("PROTOPIRATE_EMULATE_PLUGIN_APP_ID", emulate_scene)

    def test_expanded_am_protocols_reuse_api_88_core_registry(self) -> None:
        app_dir = REPO_ROOT / "applications_user/garage_door_remote"
        manifest = (app_dir / "application.fam").read_text(encoding="utf-8")
        app_i = (app_dir / "protopirate_app_i.c").read_text(encoding="utf-8")
        rx_chain = (app_dir / "helpers/protopirate_rx_chain.c").read_text(
            encoding="utf-8"
        )
        core_registry = (
            REPO_ROOT / "lib/subghz/protocols/protocol_items.c"
        ).read_text(encoding="utf-8")
        api = (REPO_ROOT / "targets/f7/api_symbols.csv").read_text(encoding="utf-8")

        self.assertEqual(len(EXPANDED_AM_PROTOCOLS), 34)
        self.assertNotIn('appid="garage_door_remote_am_plugin"', manifest)
        self.assertIn("*registry = &subghz_protocol_registry;", app_i)
        self.assertIn("chain->registry = &subghz_protocol_registry;", rx_chain)
        self.assertIn(
            "Variable,+,subghz_protocol_registry,const SubGhzProtocolRegistry,", api
        )
        for symbol in EXPANDED_AM_PROTOCOLS.values():
            self.assertEqual(core_registry.count(f"&{symbol},"), 1)

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
