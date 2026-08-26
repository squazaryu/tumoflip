#!/usr/bin/env python3
"""Regression contracts for the standard Loader FAP launch path."""

from pathlib import Path
import re
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
LOADER = REPO_ROOT / "applications/services/loader/loader.c"
LOADER_INTERNAL = REPO_ROOT / "applications/services/loader/loader_i.h"
DESKTOP_INTERNAL = REPO_ROOT / "applications/services/desktop/desktop_i.h"
DESKTOP_SETTINGS = REPO_ROOT / "applications/services/desktop/desktop_settings.c"
DESKTOP_SETTINGS_HEADER = REPO_ROOT / "applications/services/desktop/desktop_settings.h"
SETTINGS_SCENE = (
    REPO_ROOT
    / "applications/settings/desktop_settings/scenes/desktop_settings_scene_start.c"
)
RPC_APP = REPO_ROOT / "applications/services/rpc/rpc_app.c"


def function_body(source: str, signature: str) -> str:
    match = re.search(
        rf"{re.escape(signature)}.*?^}}",
        source,
        flags=re.DOTALL | re.MULTILINE,
    )
    if match is None:
        raise AssertionError(f"function not found: {signature}")
    return match.group(0)


class LoaderFapLaunchTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.loader = LOADER.read_text(encoding="utf-8")
        cls.loader_internal = LOADER_INTERNAL.read_text(encoding="utf-8")
        cls.desktop_internal = DESKTOP_INTERNAL.read_text(encoding="utf-8")
        cls.desktop_settings = DESKTOP_SETTINGS.read_text(encoding="utf-8")
        cls.desktop_settings_header = DESKTOP_SETTINGS_HEADER.read_text(encoding="utf-8")
        cls.settings_scene = SETTINGS_SCENE.read_text(encoding="utf-8")
        cls.rpc_app = RPC_APP.read_text(encoding="utf-8")

    def test_removed_preference_is_migrated_without_becoming_current_state(self) -> None:
        self.assertIn("#define DESKTOP_SETTINGS_VER_20 (20)", self.desktop_settings)
        self.assertIn("#define DESKTOP_SETTINGS_VER    (21)", self.desktop_settings)
        self.assertIn("DesktopSettingsV20", self.desktop_settings)
        self.assertIn("desktop_settings_migrate_from_v20", self.desktop_settings)
        self.assertIn("settings_v20->favorite_apps", self.desktop_settings)
        self.assertNotIn("fap_loading_animation", self.desktop_settings_header)

    def test_removed_preference_is_not_exposed_by_desktop_or_settings(self) -> None:
        self.assertNotIn("FAP Loading", self.settings_scene)
        self.assertNotIn("fap_loading_animation", self.settings_scene)
        self.assertNotIn("fap_loading_animation", self.desktop_internal)

    def test_direct_fap_launch_has_no_loading_overlay_state(self) -> None:
        start = function_body(
            self.loader,
            "static LoaderMessageLoaderStatusResult loader_do_start_by_name(",
        )
        exists = start.index("if(storage_file_exists(storage, name)) {")
        first_load = start.index("loader_start_external_app(")
        retry = start.index("loader_start_external_app(", first_load + 1)
        close = start.index("furi_record_close(RECORD_STORAGE);", retry)

        self.assertLess(exists, first_load)
        self.assertLess(first_load, retry)
        self.assertLess(retry, close)
        self.assertNotIn("loading_get_view", start)
        self.assertNotIn("loader_do_show_loading", start)
        self.assertNotIn("loading_shown", start)

    def test_structured_diagnostic_contract_is_public_and_cli_visible(self) -> None:
        loader_header = (
            REPO_ROOT / "applications/services/loader/loader.h"
        ).read_text(encoding="utf-8")
        diagnostics_header = (
            REPO_ROOT / "applications/services/loader/loader_diagnostics.h"
        ).read_text(encoding="utf-8")
        cli = (
            REPO_ROOT / "applications/services/loader/loader_cli.c"
        ).read_text(encoding="utf-8")

        self.assertIn("loader_start_with_diagnostic(", loader_header)
        self.assertIn("LoaderDiagnostic* diagnostic", loader_header)
        for token in (
            "LoaderDiagnosticCodeApiTooOld",
            "LoaderDiagnosticCodeApiTooNew",
            "LoaderDiagnosticCodeTargetMismatch",
            "LoaderDiagnosticCodeInsufficientContiguousMemory",
            "LOADER_DIAGNOSTIC_SCHEMA_VERSION",
            "loader_diagnostic_format(",
        ):
            self.assertIn(token, diagnostics_header)
        self.assertIn("loader_start_with_diagnostic(", cli)
        self.assertIn('printf("DIAG %s\\r\\n", diagnostic_text)', cli)

    def test_compact_profile_keeps_diagnostics_abi_without_heap_snapshot(self) -> None:
        diagnostics = (
            REPO_ROOT / "applications/services/loader/loader_diagnostics.c"
        ).read_text(encoding="utf-8")
        diagnostics_header = (
            REPO_ROOT / "applications/services/loader/loader_diagnostics.h"
        ).read_text(encoding="utf-8")

        self.assertIn("TUMOFLIP_LOADER_DIAGNOSTICS_FULL", diagnostics_header)
        self.assertIn('"schema=%u;code=%u;action=%u;app=%s"', diagnostics)
        self.assertIn("optional manifest and heap-detail collection", diagnostics_header)

    def test_rpc_and_gui_launch_paths_use_the_same_diagnostic_contract(self) -> None:
        self.assertIn("loader_start_with_diagnostic(", self.rpc_app)
        self.assertIn("loader_diagnostic_format(", self.rpc_app)
        self.assertIn("rpc_system_app_set_error_code(rpc_app, (uint32_t)diagnostic.code)", self.rpc_app)
        self.assertIn("rpc_system_app_set_error_text(rpc_app, diagnostic_text)", self.rpc_app)
        self.assertIn(".start.diagnostic = diagnostic", self.loader)
        self.assertRegex(
            self.loader,
            r"loader_show_gui_error\(\s*status, message\.start\.name, error_message,\s*message\.start\.diagnostic\)",
        )

    def test_loader_has_no_fap_loading_overlay_helpers_or_depth(self) -> None:
        self.assertNotIn("loading_depth", self.loader_internal)
        self.assertNotIn("loader_do_show_loading", self.loader)
        self.assertNotIn("loader_do_hide_loading", self.loader)
        self.assertNotIn("loader_is_fap_loading_animation_enabled", self.loader)

    def test_deferred_queue_keeps_its_preexisting_transition_indicator(self) -> None:
        deferred = function_body(
            self.loader,
            "static bool loader_do_deferred_launch(Loader* loader, LoaderDeferredLaunchRecord* record) {",
        )
        self.assertIn(
            "view_holder_set_view(loader->view_holder, loading_get_view(loader->loading));",
            deferred,
        )
        self.assertIn("view_holder_send_to_front(loader->view_holder);", deferred)
        self.assertIn(
            "if(!is_successful) view_holder_set_view(loader->view_holder, NULL);",
            deferred,
        )


if __name__ == "__main__":
    unittest.main()
