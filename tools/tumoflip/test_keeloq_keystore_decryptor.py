#!/usr/bin/env python3

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


class KeeLoqKeystoreDecryptorTest(unittest.TestCase):
    def test_decryptor_is_visible_arf_tool(self) -> None:
        manifest = (
            REPO_ROOT / "applications_user/keeloq_keystore_decryptor/application.fam"
        ).read_text(encoding="utf-8")

        self.assertIn('appid="keeloq_keystore_decryptor"', manifest)
        self.assertIn('name="KeeLoq Keystore Decryptor"', manifest)
        self.assertIn('fap_category="ARF Tools"', manifest)
        self.assertIn("Lechnio; tumoflip integration", manifest)
        self.assertNotIn("fap_dist_path", manifest)

    def test_decryptor_is_source_only_import(self) -> None:
        app_dir = REPO_ROOT / "applications_user/keeloq_keystore_decryptor"
        forbidden_suffixes = {".fap", ".fal", ".elf", ".dfu", ".bin"}
        found = [
            path.relative_to(REPO_ROOT).as_posix()
            for path in app_dir.rglob("*")
            if path.is_file() and path.suffix.lower() in forbidden_suffixes
        ]

        self.assertEqual(found, [])

    def test_decryptor_documents_sensitive_output(self) -> None:
        readme = (
            REPO_ROOT / "applications_user/keeloq_keystore_decryptor/README.md"
        ).read_text(encoding="utf-8")
        source = (
            REPO_ROOT
            / "applications_user/keeloq_keystore_decryptor/keeloq_keystore_decryptor.c"
        ).read_text(encoding="utf-8")

        self.assertIn("sensitive KeeLoq material", readme)
        self.assertIn("/ext/keystore_decrypted.txt", source)
        self.assertIn("/ext/subghz/assets/keeloq_mfcodes", source)

    def test_release_and_deploy_include_decryptor_as_visible_app(self) -> None:
        validate = (REPO_ROOT / "tools/tumoflip/validate_release.py").read_text(
            encoding="utf-8"
        )
        deploy = (REPO_ROOT / "tools/tumoflip/deploy_module_one_apps.py").read_text(
            encoding="utf-8"
        )
        docs = (REPO_ROOT / "docs/arf-subghz-full.md").read_text(encoding="utf-8")

        self.assertIn('"keeloq_keystore_decryptor"', validate)
        self.assertIn("keeloq_keystore_decryptor.fap", deploy)
        self.assertIn("KeeLoq Keystore Decryptor", docs)


if __name__ == "__main__":
    unittest.main()
