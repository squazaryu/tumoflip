#!/usr/bin/env python3

import re
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


class ArfSubGhzFullTest(unittest.TestCase):
    def test_full_is_a_lightweight_hub(self) -> None:
        manifest = (
            REPO_ROOT / "applications_user/arf_subghz_full/application.fam"
        ).read_text(encoding="utf-8")

        self.assertIn('appid="arf_subghz_full"', manifest)
        self.assertIn('sources=["arf_subghz_hub.c"]', manifest)
        self.assertNotIn('"scenes/*.c"', manifest)
        self.assertNotIn('"views/*.c"', manifest)

    def test_child_launchers_have_packaged_apps(self) -> None:
        start_scene = (
            REPO_ROOT / "applications_user/arf_subghz_full/arf_subghz_hub.c"
        ).read_text(encoding="utf-8")
        child_faps = set(re.findall(r'ARF_MODULES_PATH "([^"/]+\.fap)', start_scene))
        app_manifests = "\n".join(
            path.read_text(encoding="utf-8")
            for root in (REPO_ROOT / "applications", REPO_ROOT / "applications_user")
            for path in root.rglob("application.fam")
        )

        self.assertEqual(
            child_faps,
            {
                "arf_car_emulate.fap",
                "arf_counter_bf.fap",
                "arf_keeloq.fap",
                "arf_psa_decrypt.fap",
                "arf_status.fap",
                "proto_pirate.fap",
                "rolljam.fap",
                "subghz_bruteforcer.fap",
            },
        )
        self.assertIn('.target = "Sub-GHz"', start_scene)
        self.assertGreaterEqual(
            app_manifests.count(
                'fap_dist_path="apps_data/arf_subghz_full/modules/{filename}"'
            ),
            len(child_faps),
        )
        for filename in child_faps:
            appid = filename.removesuffix(".fap")
            self.assertIn(f'appid="{appid}"', app_manifests)

    def test_legacy_duplicate_is_removed(self) -> None:
        self.assertFalse((REPO_ROOT / "applications_user/arf_subghz").exists())

    def test_desktop_keeps_standard_subghz_and_exposes_arf_tools(self) -> None:
        loader_menu = (
            REPO_ROOT / "applications/services/loader/loader_menu.c"
        ).read_text(encoding="utf-8")
        self.assertIn('EXT_PATH("apps/ARF Tools")', loader_menu)
        self.assertIn("loader_menu_arf_tools_callback", loader_menu)
        self.assertNotIn("loader_menu_arf_subghz_full_callback", loader_menu)
        self.assertNotIn("loader_menu_esp32_marauder_callback", loader_menu)
        self.assertNotIn('strcmp(FLIPPER_APPS[i].name, "Sub-GHz")', loader_menu)
        self.assertNotIn(
            'EXT_PATH("apps/Module One/ESP32 Wi-Fi/esp32_wifi_marauder.fap")',
            loader_menu,
        )

    def test_desktop_menu_uses_direct_loader_launches(self) -> None:
        loader_menu = (
            REPO_ROOT / "applications/services/loader/loader_menu.c"
        ).read_text(encoding="utf-8")
        loader = (REPO_ROOT / "applications/services/loader/loader.c").read_text(
            encoding="utf-8"
        )

        self.assertIn("loader_start_with_gui_error(loader, name, args)", loader_menu)
        self.assertNotIn("pending_launch", loader_menu)
        self.assertNotIn("view_dispatcher_stop(loader_menu->view_dispatcher)", loader_menu)
        self.assertNotIn("loader_menu_has_pending_launch", loader)

    def test_full_launches_children_without_reopening_itself(self) -> None:
        start_scene = (
            REPO_ROOT / "applications_user/arf_subghz_full/arf_subghz_hub.c"
        ).read_text(encoding="utf-8")

        self.assertIn("Frequency Analyzer", start_scene)
        self.assertNotIn("ARF Analyzer", start_scene)
        self.assertEqual(start_scene.count("loader_enqueue_launch("), 1)
        self.assertNotIn("loader_get_application_launch_path", start_scene)

    def test_frequency_analyzer_fap_starts_directly(self) -> None:
        manifest = (
            REPO_ROOT / "applications_user/arf_subghz_full/application.fam"
        ).read_text(encoding="utf-8")
        app = (
            REPO_ROOT / "applications_user/arf_subghz_full/subghz.c"
        ).read_text(encoding="utf-8")
        hub = (
            REPO_ROOT / "applications_user/arf_subghz_full/arf_subghz_hub.c"
        ).read_text(encoding="utf-8")

        frequency_app = re.search(
            r'App\(\s+appid="arf_frequency_analyzer".*?\n\)', manifest, re.S
        )
        self.assertIsNotNone(frequency_app)
        self.assertNotIn("fap_dist_path", frequency_app.group(0))
        self.assertIn('fap_category="ARF Tools"', frequency_app.group(0))
        self.assertIn(
            "scene_manager_next_scene(subghz->scene_manager, SubGhzSceneFrequencyAnalyzer);",
            app,
        )
        self.assertIn('EXT_PATH("apps/ARF Tools/")', hub)
        self.assertIn(
            'ARF_TOOLS_PATH "arf_frequency_analyzer.fap"',
            hub,
        )


if __name__ == "__main__":
    unittest.main()
