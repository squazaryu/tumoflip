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
        self.assertIn('requires=["gui", "dialogs", "storage"]', manifest)

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
