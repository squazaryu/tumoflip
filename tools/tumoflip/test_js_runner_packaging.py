#!/usr/bin/env python3

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


class JsRunnerPackagingTest(unittest.TestCase):
    def setUp(self) -> None:
        self.manifest = (
            REPO_ROOT / "applications/system/js_app/application.fam"
        ).read_text(encoding="utf-8")
        self.fbt_options = (REPO_ROOT / "fbt_options.py").read_text(encoding="utf-8")
        self.system_apps = (
            REPO_ROOT / "applications/system/application.fam"
        ).read_text(encoding="utf-8")
        self.target_json = (REPO_ROOT / "targets/f7/target.json").read_text(
            encoding="utf-8"
        )
        self.lib_sconscript = (REPO_ROOT / "lib/SConscript").read_text(encoding="utf-8")
        self.api_symbols = (
            REPO_ROOT / "targets/f7/api_symbols.csv"
        ).read_text(encoding="utf-8")
        self.loader_apps = (
            REPO_ROOT / "applications/services/loader/loader_applications.c"
        ).read_text(encoding="utf-8")
        self.archive = (
            REPO_ROOT / "applications/main/archive/scenes/archive_scene_browser.c"
        ).read_text(encoding="utf-8")
        self.archive_browser = (
            REPO_ROOT / "applications/main/archive/helpers/archive_browser.h"
        ).read_text(encoding="utf-8")

    def test_js_runner_is_external_menu_app(self) -> None:
        js_app_block = self.manifest.split('appid="js_app"', 1)[1].split("App(", 1)[0]

        self.assertIn("apptype=FlipperAppType.MENUEXTERNAL", js_app_block)
        self.assertIn('name="JS Runner"', js_app_block)
        self.assertIn('fap_category="Scripts"', js_app_block)
        self.assertIn('fap_icon="icon.png"', js_app_block)
        self.assertNotIn("provides=[\"js_app_start\"]", js_app_block)

    def test_tumoflip_dev_build_excludes_js_runner(self) -> None:
        self.assertIn('"js_app"', self.fbt_options)
        self.assertIn("EXCLUDED_EXT_APPS", self.fbt_options)
        self.assertNotIn('"js_app"', self.system_apps)

    def test_mjs_runtime_api_is_not_exported(self) -> None:
        self.assertIn("Version,+,87.17,,", self.api_symbols)
        self.assertNotIn("lib/mjs/", self.api_symbols)
        self.assertNotIn("Function,+,mjs_", self.api_symbols)
        self.assertNotIn('"mjs"', self.target_json)
        self.assertNotIn('"mjs"', self.lib_sconscript)

    def test_app_browser_only_launches_faps(self) -> None:
        self.assertIn('.extension = ".fap"', self.loader_apps)
        self.assertNotIn('.extension = ".fap|.js"', self.loader_apps)
        self.assertNotIn("JS_RUNNER_APP", self.loader_apps)
        self.assertIn('[ArchiveFileTypeAppOrJs] = ".fap"', self.archive_browser)
        self.assertNotIn('return "JS Runner";', self.archive)


if __name__ == "__main__":
    unittest.main()
