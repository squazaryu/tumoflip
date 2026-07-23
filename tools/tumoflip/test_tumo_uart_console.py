#!/usr/bin/env python3

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
APP_DIR = REPO_ROOT / "applications_user/tumo_uart_console"


def read(relative: str) -> str:
    return (APP_DIR / relative).read_text(encoding="utf-8")


class TumoUartConsoleTest(unittest.TestCase):
    def test_app_is_hidden_cockpit_module_with_provenance(self) -> None:
        manifest = read("application.fam")
        upstream = read("UPSTREAM.md")

        self.assertIn('appid="tumo_uart_console"', manifest)
        self.assertIn('name="UART Field Console"', manifest)
        self.assertIn(
            'fap_dist_path="apps_data/module_one_cockpit/modules/tumo_uart_console.fap"',
            manifest,
        )
        self.assertIn('fap_category="Module One/Internal"', manifest)
        self.assertIn('fap_icon="icons/hermes_10px.png"', manifest)
        self.assertTrue((APP_DIR / "icons/hermes_10px.png").is_file())
        self.assertIn("at0m-b0mb/Hermes-FlipperZero", upstream)
        self.assertIn("fdbcc9b0ef8abc6ede36e566f89c345204f4c392", upstream)
        self.assertTrue((APP_DIR / "LICENSE").is_file())

    def test_safety_gate_is_first_and_back_can_exit(self) -> None:
        lifecycle = read("hermes.c")
        safety = read("scenes/hermes_scene_safety.c")
        start = read("scenes/hermes_scene_start.c")

        self.assertIn("scene_manager_next_scene(app->scene_manager, HermesSceneSafety)", lifecycle)
        self.assertIn("3.3V TTL only", safety)
        self.assertIn("Never connect RS-232", safety)
        self.assertIn("Detection keeps TX released", safety)
        self.assertIn("SceneManagerEventTypeBack", safety)
        self.assertIn("view_dispatcher_stop(app->view_dispatcher)", safety)
        self.assertIn("UART Console - 3.3V TTL", start)

    def test_detection_is_listen_only_and_cancel_releases_serial(self) -> None:
        verifier = read("helpers/verifier.c")
        tap = read("helpers/uart_tap.c")
        lifecycle = read("hermes.c")

        self.assertIn(
            "furi_hal_serial_disable_direction(v->serial, FuriHalSerialDirectionTx)",
            verifier,
        )
        self.assertIn("furi_hal_serial_deinit(v->serial)", verifier)
        self.assertIn("furi_hal_serial_control_release(v->serial)", verifier)
        self.assertIn("furi_hal_serial_dma_rx_stop(tap->serial)", tap)
        self.assertIn("furi_hal_serial_deinit(tap->serial)", tap)
        self.assertIn("furi_hal_serial_control_release(tap->serial)", tap)
        self.assertIn("autobaud_stop(app->autobaud)", lifecycle)
        self.assertIn("verifier_stop(app->verifier)", lifecycle)
        self.assertIn("selftest_stop(app->selftest)", lifecycle)
        self.assertIn("uart_tap_close(app->tap)", lifecycle)

    def test_framing_inversion_and_terminal_controls_are_present(self) -> None:
        framing = read("helpers/baud_table.c")
        verifier = read("helpers/verifier.c")
        settings = read("scenes/hermes_scene_settings.c")
        console = read("scenes/hermes_scene_console.c")
        controls = read("scenes/hermes_scene_ctrl.c")

        for label in ("8N1", "8E1", "8O1", "7E1", "8N2", "8E2", "8O2", "7E2"):
            self.assertIn(f'return "{label}"', framing)
        self.assertIn("for(uint8_t inverted = 0; inverted < 2u", verifier)
        self.assertIn('"RX invert"', settings)
        self.assertIn('"TX invert"', settings)
        self.assertIn("session_log_open(", console)
        self.assertIn("script_next_line(app->script)", console)
        self.assertIn('"Send break"', controls)
        self.assertIn('"Run script..."', controls)
        self.assertIn("#define SCRIPT_MAX_BYTES (2048u)", read("helpers/script.h"))
        self.assertIn("#define SCRIPT_MAX_LINES (64u)", read("helpers/script.h"))
        self.assertFalse((APP_DIR / "scripts/example_login.txt").exists())

    def test_cockpit_and_package_routes_match(self) -> None:
        cockpit = (
            REPO_ROOT
            / "applications_user/module_one_cockpit/module_one_cockpit.c"
        ).read_text(encoding="utf-8")
        validator = (
            REPO_ROOT / "tools/tumoflip/validate_release.py"
        ).read_text(encoding="utf-8")
        route = "apps_data/module_one_cockpit/modules/tumo_uart_console.fap"

        self.assertIn('"UART: Field Console"', cockpit)
        self.assertIn(f'EXT_PATH("{route}")', cockpit)
        self.assertIn(f'"{route}"', validator)


if __name__ == "__main__":
    unittest.main()
