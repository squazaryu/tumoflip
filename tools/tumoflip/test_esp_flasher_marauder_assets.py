import hashlib
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MARAUDER_ASSETS = {
    "applications_user/esp_flasher/packedfws/marauder/S2/esp32_marauder.flipper.bin":
        "e50ccd94f01da3e1887a469facb8c0cf00d7f99adf8993b72b913cfc2c35993c",
    "applications_user/esp_flasher/packedfws/marauder/s3/esp32_marauder.multiboardS3.bin":
        "77d847e58ec3dd94f4893b99fd3669fbf8f6b391175f42714e13130a78507e55",
    "applications_user/esp_flasher/packedfws/marauder/WROOM/esp32_marauder.dev_board_pro.bin":
        "bfc3b75caa35fde5299d4c514ed4ea1870d8d480f78fac81d1e15ea553e78d93",
}


class EspFlasherMarauderAssetsTests(unittest.TestCase):
    def test_bundled_marauder_1143_images_match_the_pinned_upstream_assets(self):
        for relative_path, expected_sha256 in MARAUDER_ASSETS.items():
            with self.subTest(path=relative_path):
                path = ROOT / relative_path
                digest = hashlib.sha256(path.read_bytes()).hexdigest()
                self.assertEqual(digest, expected_sha256)

    def test_esp_flasher_package_version_tracks_the_embedded_image_update(self):
        application_fam = (ROOT / "applications_user/esp_flasher/application.fam").read_text()
        self.assertIn("fap_version=(1, 14)", application_fam)


if __name__ == "__main__":
    unittest.main()
