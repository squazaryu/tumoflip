import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


class ProtocolVisualizerTest(unittest.TestCase):
    def test_manifest_is_visible_arf_tool(self) -> None:
        manifest = (
            REPO_ROOT / "applications_user/protocol_visualizer/application.fam"
        ).read_text(encoding="utf-8")

        self.assertIn('appid="protocol_visualizer"', manifest)
        self.assertIn('name="Protocol Visualizer"', manifest)
        self.assertIn('fap_category="ARF Tools"', manifest)
        self.assertIn('fap_icon="assets/sub1_10px.png"', manifest)
        self.assertIn('fap_icon_assets="assets"', manifest)
        self.assertIn('requires=["gui", "dialogs", "storage"]', manifest)

    def test_file_browser_icons_are_bundled_with_fap(self) -> None:
        app_dir = REPO_ROOT / "applications_user/protocol_visualizer"
        source = (app_dir / "protocol_visualizer.c").read_text(encoding="utf-8")

        self.assertTrue((app_dir / "assets/sub1_10px.png").is_file())
        self.assertTrue((app_dir / "assets/IR_Icon_10x10.png").is_file())
        self.assertIn('#include "protocol_visualizer_icons.h"', source)
        self.assertIn("&I_sub1_10px", source)
        self.assertIn("&I_IR_Icon_10x10", source)

    def test_context_paths_are_loaded_safely(self) -> None:
        source = (
            REPO_ROOT / "applications_user/protocol_visualizer/protocol_visualizer.c"
        ).read_text(encoding="utf-8")

        self.assertIn("static bool pv_path_has_extension", source)
        self.assertIn('pv_path_has_extension(load_path, ".ir")', source)
        self.assertIn('pv_path_has_extension(load_path, ".sub")', source)
        self.assertIn("pv_load_path(app, context)", source)
        self.assertIn("if(context) {", source)

    def test_selected_browser_path_is_copied_before_reuse(self) -> None:
        source = (
            REPO_ROOT / "applications_user/protocol_visualizer/protocol_visualizer.c"
        ).read_text(encoding="utf-8")

        load_path_start = source.index("static bool pv_load_path")
        load_path_end = source.index("static bool pv_select_and_load")
        load_path = source[load_path_start:load_path_end]

        self.assertIn("FuriString* safe_path = furi_string_alloc_set(path)", load_path)
        self.assertIn("const char* load_path = furi_string_get_cstr(safe_path)", load_path)
        self.assertIn("furi_string_free(safe_path)", load_path)
        self.assertNotIn("furi_string_set(app->file_path, path)", load_path)

    def test_file_loaders_validate_known_flipper_filetypes(self) -> None:
        source = (
            REPO_ROOT / "applications_user/protocol_visualizer/protocol_visualizer.c"
        ).read_text(encoding="utf-8")

        self.assertIn('"Flipper SubGhz Key File"', source)
        self.assertIn('"Flipper SubGhz RAW File"', source)
        self.assertIn('"IR signals file"', source)
        self.assertIn('"IR library file"', source)
        self.assertIn("valid_header", source)
        self.assertIn("has_payload", source)
        self.assertIn("has_signal", source)
        self.assertIn("return false", source)

    def test_capture_loader_bounds_line_memory(self) -> None:
        source = (
            REPO_ROOT / "applications_user/protocol_visualizer/protocol_visualizer.c"
        ).read_text(encoding="utf-8")

        self.assertIn("#define PV_LINE_BUF_SIZE 256U", source)
        self.assertIn("bool* line_truncated", source)
        self.assertIn("furi_string_size(output) < PV_LINE_BUF_SIZE - 1U", source)
        self.assertIn("capture->truncated = true", source)

    def test_hub_launches_visible_fap(self) -> None:
        hub = (
            REPO_ROOT / "applications_user/arf_subghz_full/arf_subghz_hub.c"
        ).read_text(encoding="utf-8")

        self.assertIn('"Protocol Visualizer"', hub)
        self.assertIn('ARF_TOOLS_PATH "protocol_visualizer.fap"', hub)

    def test_visualizer_stays_receive_only(self) -> None:
        source = (
            REPO_ROOT / "applications_user/protocol_visualizer/protocol_visualizer.c"
        ).read_text(encoding="utf-8")

        forbidden = [
            "subghz_transmitter",
            "subghz_txrx",
            "furi_hal_subghz_tx",
            "infrared_signal_transmit",
            "infrared_worker_tx",
        ]
        for token in forbidden:
            self.assertNotIn(token, source)


if __name__ == "__main__":
    unittest.main()
