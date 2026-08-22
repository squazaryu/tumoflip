#!/usr/bin/env python3
"""Regression contracts for the optional Loader FAP loading animation."""

from pathlib import Path
import re
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
LOADER = REPO_ROOT / "applications/services/loader/loader.c"
LOADER_INTERNAL = REPO_ROOT / "applications/services/loader/loader_i.h"
DESKTOP_SETTINGS = REPO_ROOT / "applications/services/desktop/desktop_settings.c"
DESKTOP_SETTINGS_HEADER = REPO_ROOT / "applications/services/desktop/desktop_settings.h"
SETTINGS_SCENE = (
    REPO_ROOT
    / "applications/settings/desktop_settings/scenes/desktop_settings_scene_start.c"
)


def function_body(source: str, signature: str) -> str:
    match = re.search(
        rf"{re.escape(signature)}.*?^}}",
        source,
        flags=re.DOTALL | re.MULTILINE,
    )
    if match is None:
        raise AssertionError(f"function not found: {signature}")
    return match.group(0)


class LoaderFapLoadingTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.loader = LOADER.read_text(encoding="utf-8")
        cls.loader_internal = LOADER_INTERNAL.read_text(encoding="utf-8")
        cls.desktop_settings = DESKTOP_SETTINGS.read_text(encoding="utf-8")
        cls.desktop_settings_header = DESKTOP_SETTINGS_HEADER.read_text(encoding="utf-8")
        cls.settings_scene = SETTINGS_SCENE.read_text(encoding="utf-8")

    def test_setting_defaults_to_enabled_and_migrates_v19(self) -> None:
        self.assertIn("uint8_t fap_loading_animation;", self.desktop_settings_header)
        self.assertIn("#define DESKTOP_SETTINGS_VER_19 (19)", self.desktop_settings)
        self.assertIn("#define DESKTOP_SETTINGS_VER    (20)", self.desktop_settings)
        self.assertIn("desktop_settings_migrate_from_v19", self.desktop_settings)
        self.assertIn("settings->fap_loading_animation = true;", self.desktop_settings)

    def test_desktop_settings_exposes_the_default_enabled_toggle(self) -> None:
        self.assertIn('"FAP Loading"', self.settings_scene)
        self.assertIn("desktop_settings_scene_start_fap_loading_animation_changed", self.settings_scene)
        self.assertIn("const uint32_t fap_loading_animation_value", self.settings_scene)
        self.assertIn("= {1, 0};", self.settings_scene)

    def test_loading_overlay_is_reference_counted_and_preference_gated(self) -> None:
        self.assertIn("uint8_t loading_depth;", self.loader_internal)
        alloc = function_body(self.loader, "static Loader* loader_alloc(")
        preference = function_body(
            self.loader, "static bool loader_is_fap_loading_animation_enabled("
        )
        self.assertIn("loader->loading_depth = 0;", alloc)
        show = function_body(self.loader, "static bool loader_do_show_loading(")
        hide = function_body(self.loader, "static void loader_do_hide_loading(")

        self.assertIn("loader_is_fap_loading_animation_enabled", show)
        self.assertIn("if(!loader_is_fap_loading_animation_enabled()) return false;", show)
        self.assertIn(
            "desktop_is_fap_loading_animation_enabled(desktop)", preference
        )
        self.assertIn("return fap_loading_animation;", preference)
        self.assertIn("furi_assert(loader->loading_depth < UINT8_MAX);", show)
        self.assertIn("view_holder_send_to_front(loader->view_holder);", show)
        self.assertIn("furi_check(loader->loading_depth > 0);", hide)
        self.assertIn("if(loader->loading_depth == 0)", hide)

    def test_external_fap_read_is_bracketed_as_one_span(self) -> None:
        start = function_body(self.loader, "static LoaderMessageLoaderStatusResult loader_do_start_by_name(")
        show = start.index("loading_shown = loader_do_show_loading(loader);")
        first_load = start.index("loader_start_external_app(loader, storage, name, args, error_message, false)")
        retry = start.index("loader_start_external_app(\n                        loader, storage")
        hide = start.index("if(loading_shown) loader_do_hide_loading(loader);")

        self.assertLess(show, first_load)
        self.assertLess(first_load, retry)
        self.assertLess(retry, hide)

    def test_prearmed_overlay_is_removed_when_the_fap_disappears(self) -> None:
        start = function_body(self.loader, "static LoaderMessageLoaderStatusResult loader_do_start_by_name(")
        fap_block = start[start.index("// check Faps") :]
        missing_card_cleanup = fap_block.index(
            "// A card can disappear between preflight and the actual read."
        )
        hide = fap_block.index("if(loading_shown) loader_do_hide_loading(loader);", missing_card_cleanup)

        self.assertLess(missing_card_cleanup, hide)

    def test_direct_fap_prearms_overlay_before_desktop_transition(self) -> None:
        start = function_body(self.loader, "static LoaderMessageLoaderStatusResult loader_do_start_by_name(")
        target_resolution = start.index(
            "const FlipperInternalApplication* internal_app = loader_find_application_by_name(name);"
        )
        prearmed_show = start.index("loading_shown = loader_do_show_loading(loader);")
        desktop_transition = start.index("furi_pubsub_publish(loader->pubsub, &event);")
        external_load = start.index(
            "loader_start_external_app(loader, storage, name, args, error_message, false)"
        )

        self.assertIn("if(!internal_app)", start[target_resolution:prearmed_show])
        self.assertLess(target_resolution, prearmed_show)
        self.assertLess(prearmed_show, desktop_transition)
        self.assertLess(desktop_transition, external_load)

    def test_deferred_launch_uses_the_same_balanced_overlay(self) -> None:
        deferred = re.search(
            r"static bool loader_do_deferred_launch\(Loader\* loader, "
            r"LoaderDeferredLaunchRecord\* record\) \{.*?^}\n\n"
            r"static void loader_do_app_closed",
            self.loader,
            re.DOTALL | re.MULTILINE,
        )
        self.assertIsNotNone(deferred)
        body = deferred.group(0)
        self.assertIn("const bool loading_shown = loader_do_show_loading(loader);", body)
        self.assertIn("if(loading_shown) loader_do_hide_loading(loader);", body)
        self.assertNotIn("loading_get_view(loader->loading)", body)


if __name__ == "__main__":
    unittest.main()
